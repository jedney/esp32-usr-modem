"""Independent pppd/IP-stack HTTP+DNS acceptance through Windows COM18.

python test/interop/serial_pppd.py --console-port COM15 --rs232-port COM18 --server-ip <PC LAN IP>
Uses existing WSL Ubuntu pppd 2.4.x as root inside a temporary network namespace.
No global route, DNS, firewall, credential or saved modem setting is changed.
"""
import argparse
import http.server
import json
import os
from pathlib import Path
import queue
import re
import subprocess
import sys
import threading
import time
import uuid

import serial

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/e2e'))
from conftest import Port  # noqa: E402
from test_internet import dial  # noqa: E402


def wsl_path(path):
    path = path.resolve()
    return '/mnt/' + path.drive[0].lower() + path.as_posix()[2:]


def wsl(*command, **kwargs):
    return subprocess.run(['wsl.exe', '-d', 'Ubuntu', '-u', 'root', '--', *command],
                          capture_output=True, timeout=kwargs.pop('timeout', 30), **kwargs)


def reader(stream, pending):
    while data := stream.read1(4096):
        pending.put(data)


class Handler(http.server.BaseHTTPRequestHandler):
    sources = []
    ports = []

    def log_message(self, *_):
        pass

    def do_GET(self):
        self.sources.append(self.client_address[0])
        self.ports.append(self.client_address[1])
        payload = bytes(range(256)) * (32 if self.path == '/small.bin' else 256)
        self.send_response(200)
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--console-port', required=True)
    parser.add_argument('--rs232-port', required=True)
    parser.add_argument('--baud', type=int, default=38400)
    parser.add_argument('--server-ip', required=True)
    parser.add_argument('--web-host', default='usr-modem.local')
    args = parser.parse_args()
    namespace = 'usr-modem-' + uuid.uuid4().hex[:8]
    created = False
    ppp = probe = None
    server = http.server.ThreadingHTTPServer(('0.0.0.0', 32321), Handler)
    threading.Thread(target=server.serve_forever, daemon=True).start()
    report = {}
    with serial.Serial(args.console_port, 115200, timeout=0.05, write_timeout=5) as maintenance, \
            serial.Serial(args.rs232_port, args.baud, timeout=0.02, rtscts=True,
                          write_timeout=5) as host:
        console, terminal = Port(maintenance), Port(host)
        console.command('AT*INTERNET=HANGUP', timeout=12)
        terminal.command('ATQ0V1E0M0&B1&H1&R2&I0&D2&C1F0')
        expected_nat = re.search(r'IP ([0-9.]+)', '\n'.join(console.command('AT*STATUS'))).group(1)
        try:
            result = wsl('ip', 'netns', 'add', namespace)
            if result.returncode:
                raise RuntimeError('Could not create isolated PPP test network namespace')
            created = True
            version = wsl('pppd', '--version')
            report['peer'] = (version.stdout + version.stderr).decode().strip()
            dial(host, '666')
            command = ['wsl.exe', '-d', 'Ubuntu', '-u', 'root', '--', 'ip', 'netns', 'exec',
                       namespace, 'pppd', 'notty', 'nodetach', 'local', 'noauth', 'noipdefault',
                       'defaultroute', 'noccp', 'novj', 'nobsdcomp', 'nodeflate', 'mtu', '1500',
                       'mru', '1500', 'lcp-echo-interval', '0', 'logfd', '2', 'linkname', namespace]
            ppp = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                   stderr=subprocess.PIPE)
            pending = queue.Queue()
            threading.Thread(target=reader, args=(ppp.stdout, pending), daemon=True).start()
            deadline = time.monotonic() + 150
            next_status = 0
            while time.monotonic() < deadline:
                while not pending.empty():
                    host.write(pending.get_nowait())
                data = host.read(host.in_waiting or 1)
                if data:
                    ppp.stdin.write(data)
                    ppp.stdin.flush()
                if ppp.poll() is not None:
                    raise RuntimeError('pppd exited before acceptance (protocol logs omitted)')
                if not probe and time.monotonic() >= next_status:
                    next_status = time.monotonic() + 0.5
                    status = '\n'.join(console.command('AT*INTERNET?'))
                    if 'PPP ready' in status:
                        dns = re.search(r'DNS=.*\(([0-9.]+)\)', status).group(1)
                        if os.environ.get('USR_MODEM_PASSWORD'):
                            from test_web import Web
                            with Web(args.web_host) as web:
                                code, _, _ = web.request('/api/session')
                            if code != 200:
                                raise RuntimeError('Local authenticated HTTP failed during PPP')
                            report['local_authenticated_http'] = 'passed'
                        probe = subprocess.Popen(
                            ['wsl.exe', '-d', 'Ubuntu', '-u', 'root', '--', 'ip', 'netns', 'exec',
                             namespace, 'python3', wsl_path(ROOT / 'test/interop/ppp_probe.py'),
                             '--server', args.server_ip, '--dns', dns],
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                if probe and probe.poll() is not None:
                    out, _ = probe.communicate()
                    if probe.returncode:
                        raise RuntimeError('Independent PPP HTTP/DNS probe failed (traffic omitted)')
                    report.update(json.loads(out))
                    break
            else:
                raise RuntimeError('Independent PPP acceptance timed out')
            if not Handler.sources:
                raise RuntimeError('HTTP server received no connection')
            report['nat_source'] = Handler.sources[-1]
            if any(source != expected_nat for source in Handler.sources):
                raise RuntimeError('HTTP source did not match the modem Wi-Fi NAPT address')
            if any(not 32768 <= port <= 49151 for port in Handler.ports):
                raise RuntimeError('NAPT reused the modem local ephemeral socket range')
            report['mapped_ports'] = Handler.ports
            # SIGTERM makes the actual peer negotiate LCP termination. Keep relaying
            # until DCD drops, proving that cleanup does not require an ESP reset.
            result = wsl('cat', '/run/ppp-' + namespace + '.pid')
            if result.returncode or not result.stdout.split():
                raise RuntimeError('Independent peer PID record is missing')
            wsl('kill', '-TERM', result.stdout.decode().split()[0])
            deadline = time.monotonic() + 5
            while host.cd and time.monotonic() < deadline:
                while not pending.empty():
                    host.write(pending.get_nowait())
                data = host.read(host.in_waiting or 1)
                if data and ppp.poll() is None:
                    ppp.stdin.write(data)
                    ppp.stdin.flush()
            report['hangup'] = 'passed' if not host.cd else 'failed'
            if host.cd:
                raise RuntimeError('Peer termination did not drop DCD')
            print(json.dumps(report), flush=True)
        finally:
            if probe and probe.poll() is None:
                probe.kill()
            console.command('AT*INTERNET=HANGUP')
            if created:
                result = wsl('ip', 'netns', 'pids', namespace)
                for pid in result.stdout.decode().split():
                    wsl('kill', '-TERM', pid)
                wsl('ip', 'netns', 'del', namespace)
            if ppp and ppp.poll() is None:
                ppp.terminate()
                ppp.wait(timeout=5)
            time.sleep(0.2)
            terminal.command('ATZ')
            server.shutdown()
            server.server_close()


if __name__ == '__main__':
    main()
