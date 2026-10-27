"""POSIX lrzsz PTY-to-stdio bridge for the Windows COM-port test driver.

Protocol bytes use stdin/stdout exclusively; diagnostics use stderr. The bridge
has no network listener and never accesses a WSL serial device.
"""
import argparse
import os
from pathlib import Path
import select
import subprocess
import sys
import tempfile
import time
from run_lrzsz import peer_process


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('direction', choices=['download', 'upload'])
    parser.add_argument('protocol', type=int, choices=range(5))
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    receiving = args.direction == 'download'
    flag = ['-X', '-X', '-X', '--ymodem', '-Z'][args.protocol]
    command = [os.environ.get('USR_LRZSZ_RX', 'rz') if receiving else os.environ.get('USR_LRZSZ_TX', 'sz'), flag, '-b', '-q', '-y']
    if receiving:
        command += ['-u']
        if args.protocol in (1, 2):
            command.insert(2, '-c')
        if args.protocol < 3:
            command += ['RECEIVED.BIN']
    else:
        if args.protocol == 4:
            command.remove('-Z')
        if args.protocol == 2:
            command += ['-k']
        if args.protocol == 0:
            command += ['-o']
        command += [str(path) for path in sorted(args.directory.glob('*.BIN'))]
    with tempfile.TemporaryFile() as errors:
        proc, master, slave = peer_process(command, args.directory, errors)
        deadline = time.monotonic() + 300
        try:
            while time.monotonic() < deadline:
                ready, _, _ = select.select([sys.stdin.buffer, proc.stdout], [], [], .01)
                for stream in ready:
                    data = os.read(stream.fileno(), 4096)
                    if stream == proc.stdout:
                        if not data and proc.poll() is not None:
                            errors.seek(0)
                            sys.stderr.buffer.write(errors.read())
                            print(f'LRZSZ_EXIT={proc.returncode}', file=sys.stderr, flush=True)
                            return proc.returncode
                        sys.stdout.buffer.write(data)
                        sys.stdout.buffer.flush()
                    elif data:
                        os.write(master, data)
            return 124
        finally:
            if proc.poll() is None:
                proc.kill()
            proc.wait()
            os.close(master)
            os.close(slave)


if __name__ == '__main__':
    raise SystemExit(main())
