"""Linux CI interoperability: a real pppd/kernel IP stack against the portable core.

Run as root with --peer pointing to the compiled internet_peer.cpp executable.
All routes and the PPP interface live in a temporary network namespace.
This validates protocol interoperability; physical NAT is tested separately.
"""
import argparse
import os
from pathlib import Path
import selectors
import signal
import subprocess
import sys
import time
import uuid


def run(*command):
    return subprocess.run(command, capture_output=True, check=True, timeout=10)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--peer', type=Path, required=True)
    args = parser.parse_args()
    if os.geteuid() != 0:
        raise SystemExit('Use root for the isolated pppd namespace')
    namespace = 'usr-modem-' + uuid.uuid4().hex[:8]
    processes = []
    run('ip', 'netns', 'add', namespace)
    try:
        peer = subprocess.Popen([str(args.peer.resolve())], stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append(peer)
        ppp = subprocess.Popen(['ip', 'netns', 'exec', namespace, 'pppd', 'notty', 'nodetach',
                                'local', 'noauth', 'noipdefault', 'nodefaultroute', 'noccp', 'novj',
                                'nobsdcomp', 'nodeflate', 'linkname', namespace, 'logfd', '2'],
                               stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append(ppp)
        probe_code = '''import socket,time
payload=bytes(range(256))*4+b"+++AT*CONFIG\\r\\x11\\x13"
deadline=time.monotonic()+12
while time.monotonic()<deadline:
 try:
  with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as s:
   s.settimeout(1)
   s.sendto(payload,("10.64.64.1",7))
   response,_=s.recvfrom(2048)
   assert response==payload
   break
 except OSError:
  time.sleep(.1)
else:
 raise RuntimeError("PPP UDP round trip failed")
print("passed")
'''
        probe = subprocess.Popen(['ip', 'netns', 'exec', namespace, sys.executable, '-c', probe_code],
                                 stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        processes.append(probe)
        selector = selectors.DefaultSelector()
        for source, destination in ((peer.stdout, ppp.stdin), (ppp.stdout, peer.stdin)):
            os.set_blocking(source.fileno(), False)
            selector.register(source, selectors.EVENT_READ, destination)
        terminating = False
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            for key, _ in selector.select(0.05):
                data = os.read(key.fileobj.fileno(), 4096)
                if not data:
                    selector.unregister(key.fileobj)
                    continue
                key.data.write(data)
                key.data.flush()
            if not terminating and probe.poll() is not None:
                if probe.returncode:
                    raise RuntimeError('Independent kernel PPP UDP test failed')
                pid = int(Path('/run/ppp-' + namespace + '.pid').read_text().split()[0])
                os.kill(pid, signal.SIGTERM)
                terminating = True
            if terminating and peer.poll() is not None:
                if peer.returncode:
                    raise RuntimeError('Portable peer failed during termination')
                print('Independent pppd/kernel IP: binary UDP and LCP termination passed')
                return
            if not terminating and (peer.poll() is not None or ppp.poll() is not None):
                raise RuntimeError('PPP peer exited before acceptance')
        raise RuntimeError('Independent PPP test timed out')
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
        pids = run('ip', 'netns', 'pids', namespace).stdout.decode().split()
        for pid in pids:
            subprocess.run(['kill', '-TERM', pid], capture_output=True)
        for process in processes:
            try:
                process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                process.kill()
        run('ip', 'netns', 'del', namespace)


if __name__ == '__main__':
    main()
