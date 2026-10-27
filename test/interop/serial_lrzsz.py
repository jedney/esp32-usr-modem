"""Independent wired transfers through Windows pyserial and WSL lrzsz.

Run: python test/interop/serial_lrzsz.py --port COM18 --baud 38400
Writes only generated test fixtures under the ignored .pio/ directory. Does not
save modem settings, reset credentials or install images.
"""
import argparse
import binascii
from pathlib import Path
import queue
import re
import subprocess
import threading
import time
import serial

ROOT = Path(__file__).resolve().parents[2]


def wsl_path(path):
    resolved = path.resolve()
    return '/mnt/' + resolved.drive[0].lower() + resolved.as_posix()[2:]


def reader(stream, pending):
    while True:
        data = stream.read1(4096)
        if not data:
            break
        pending.put(data)


def run_case(ser, protocol, downloading, length=8193):
    directory = ROOT / '.pio' / 'client-acceptance' / f'{protocol}-' / ('download' if downloading else 'upload')
    directory.mkdir(parents=True, exist_ok=True)
    expected = bytes(i & 255 for i in range(length))
    if not downloading:
        (directory / 'TEST.BIN').write_bytes(expected)
    variant = ['SUM', 'CRC', '1K', '', ''][protocol]
    kind = 'XXXYZ'[protocol]
    command = f'AT*{("S" if downloading else "R")}{kind}'
    if downloading:
        command += '=TEST,' + (variant + ',' if protocol < 3 else '') + str(length)
    elif protocol < 3:
        command += '=' + variant
    ser.reset_input_buffer()
    ser.write(command.encode() + b'\r')
    pending = queue.Queue()
    command_line = ['wsl.exe', '-d', 'Ubuntu', '--', 'env']
    local_peer = ROOT / '.pio/tools/lrzsz-installed/bin'
    if (local_peer / 'lrz').is_file() and (local_peer / 'lsz').is_file():
        command_line += ['USR_LRZSZ_RX=' + wsl_path(local_peer / 'lrz'),
                         'USR_LRZSZ_TX=' + wsl_path(local_peer / 'lsz')]
    command_line += ['python3',
                             wsl_path(ROOT / 'test/interop/stdio_lrzsz.py'),
                             'download' if downloading else 'upload', str(protocol), wsl_path(directory)]
    proc = subprocess.Popen(command_line,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    worker = threading.Thread(target=reader, args=(proc.stdout, pending), daemon=True)
    worker.start()
    received = bytearray()
    deadline = time.monotonic() + 150
    try:
        while time.monotonic() < deadline:
            while not pending.empty():
                ser.write(pending.get_nowait())
            data = ser.read(ser.in_waiting or 1)
            if data:
                received.extend(data)
                if len(received) > 16384:
                    del received[:-16384]
                if proc.poll() is None:
                    try:
                        proc.stdin.write(data)
                        proc.stdin.flush()
                    except (BrokenPipeError, OSError):
                        pass
            if (proc.poll() is not None and not worker.is_alive() and pending.empty()
                    and re.search(rb'Transfer SUCCESS:[^\r\n]+CRC32=[0-9A-F]{8}\r\n', received)):
                break
        else:
            raise AssertionError('wired transfer timed out (protocol output omitted)')
        peer_report = proc.stderr.read().decode(errors='replace')
        assert 'LRZSZ_EXIT=0' in peer_report, f'lrzsz bridge exit {proc.returncode}: ' + peer_report
        match = re.search(rb'Transfer SUCCESS: bytes=(\d+) ms=(\d+) cps=(\d+) retries=(\d+) errors=(\d+) files=(\d+) CRC32=([0-9A-F]+)', received)
        assert match, 'missing transfer statistics'
        if downloading:
            target = directory / ('RECEIVED.BIN' if protocol < 3 else 'TEST64K.BIN' if length == 65536 else 'TEST1M.BIN' if length == 1048576 else 'TEST.BIN')
            data = target.read_bytes()
            assert data[:length] == expected, 'download payload mismatch'
            if protocol >= 3:
                assert len(data) == length
        else:
            padded = expected + (b'\x1a' * ((-length) % 128) if protocol < 3 else b'')
            assert int(match[1]) == len(padded)
            assert int(match[7], 16) == binascii.crc32(padded), 'upload payload CRC mismatch'
        return match[0].decode()
    finally:
        if proc.poll() is None:
            proc.kill()
        proc.wait()
        worker.join(timeout=2)
        try:
            proc.stdin.close()
        except OSError:
            pass


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--baud', type=int, default=38400)
    parser.add_argument('--protocol', type=int, choices=range(5))
    args = parser.parse_args()
    with serial.Serial(args.port, args.baud, timeout=.01, rtscts=True, write_timeout=5) as ser:
        ser.write(b'ATE0Q0V1\r')
        time.sleep(.3)
        ser.reset_input_buffer()
        for protocol in (range(5) if args.protocol is None else [args.protocol]):
            for downloading in (True, False):
                print(args.port, protocol, 'download' if downloading else 'upload',
                      run_case(ser, protocol, downloading), flush=True)
                time.sleep(.2)


if __name__ == '__main__':
    main()
