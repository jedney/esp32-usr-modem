"""Independent lrzsz interoperability against the production core (POSIX/WSL).

Build client_peer.cpp with transfer.cpp, then run this script with --peer PATH.
The PTY adapter can also be used by the Windows serial bridge; no WSL serial
device mapping or network listener is needed.
"""
import argparse
import binascii
import errno
import os
from pathlib import Path
import pty
import select
import subprocess
import tempfile
import time
import tty


def peer_process(argv, cwd, errors):
    master, slave = pty.openpty()
    tty.setraw(slave)
    # lrzsz restores stdin using TCIOFLUSH on exit. Keep protocol output on a
    # separate pipe so that final ACKs cannot be flushed from the PTY output queue.
    proc = subprocess.Popen(argv, stdin=slave, stdout=subprocess.PIPE, stderr=errors, cwd=cwd)
    # Keep the slave alive until the bridge drains the final ACK/OO. Closing the
    # last slave on process exit can discard unread PTY output.
    return proc, master, slave


def run_case(executable, protocol, sending, length=8193, files=1):
    with tempfile.TemporaryDirectory() as folder, tempfile.TemporaryFile() as errors:
        root = Path(folder)
        expected = bytes(i & 255 for i in range(length))
        flag = ['-X', '-X', '-X', '--ymodem', '-Z'][protocol]
        argv = [os.environ.get('USR_LRZSZ_RX', 'rz') if sending else os.environ.get('USR_LRZSZ_TX', 'sz'), flag, '-b', '-q', '-y']
        if protocol == 4 and not sending:
            argv.remove('-Z')  # sz uses ZMODEM by default; it has no -Z option.
        if sending:
            argv.append('-u')
        if sending and protocol < 3:
            argv += ['RECEIVED.BIN']
        if not sending:
            for index in range(files):
                name = root / ('SECOND.BIN' if index else 'TEST.BIN')
                name.write_bytes(expected)
                argv.append(str(name))
            if protocol == 2:
                argv.insert(1, '-k')
        if sending and protocol in (1, 2):
            argv.insert(2, '-c')
        if protocol == 0 and not sending:
            argv.insert(1, '-o')
        receiver, master, slave = peer_process(argv, root, errors)
        engine = subprocess.Popen([str(executable), 'send' if sending else 'receive', str(protocol), str(length), str(files)],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        deadline = time.monotonic() + 25
        trace = []
        try:
            while time.monotonic() < deadline:
                if engine.poll() is not None and receiver.poll() is not None:
                    break
                ready, _, _ = select.select([receiver.stdout, engine.stdout], [], [], .01)
                for endpoint in ready:
                    try:
                        data = os.read(endpoint if isinstance(endpoint, int) else endpoint.fileno(), 4096)
                    except OSError as exc:
                        if exc.errno == errno.EIO:
                            continue
                        raise
                    if not data:
                        continue
                    trace.append(('lrzsz' if endpoint == receiver.stdout else 'core', len(data), data[:16].hex()))
                    trace = trace[-20:]
                    if endpoint == receiver.stdout:
                        if engine.poll() is None:
                            engine.stdin.write(data)
                            engine.stdin.flush()
                    else:
                        os.write(master, data)
            if engine.poll() is None or receiver.poll() is None:
                status = (engine.poll(), receiver.poll())
                for process in (engine, receiver):
                    if process.poll() is None:
                        process.kill()
                engine.wait(); receiver.wait()
                errors.seek(0)
                raise AssertionError('interoperability deadline exceeded: ' + str(status) + str(trace) + errors.read().decode(errors='replace') + engine.stderr.read().decode())
            report = engine.stderr.read().decode()
            errors.seek(0)
            diagnostic = errors.read().decode(errors='replace')
            assert engine.returncode == 0, report + diagnostic
            assert receiver.returncode == 0, report + diagnostic
            if sending:
                for index in range(files):
                    name = 'RECEIVED.BIN' if protocol < 3 else 'SECOND.BIN' if index else 'TEST.BIN'
                    received = (root / name).read_bytes()
                    assert received[:length] == expected, 'received content differs'
                    if protocol >= 3:
                        assert len(received) == length
            else:
                padded = expected
                if protocol < 3:
                    block = 128
                    padded += b'\x1a' * ((-length) % block)
                crc = binascii.crc32(padded * files)
                assert f'crc={crc:08X}' in report, report
            return report.strip()
        finally:
            for process in (engine, receiver):
                if process.poll() is None:
                    process.kill()
                process.wait()
            os.close(master)
            os.close(slave)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--peer', type=Path, required=True)
    args = parser.parse_args()
    executable = args.peer.resolve()
    for protocol, name in enumerate(('Xsum', 'Xcrc', 'X1k', 'Y', 'Z')):
        for sending in (True, False):
            print(name, 'download' if sending else 'upload', run_case(executable, protocol, sending, files=2 if protocol >= 3 else 1), flush=True)
    for protocol in (3, 4):
        print('empty', protocol, run_case(executable, protocol, True, length=0), flush=True)


if __name__ == '__main__':
    main()
