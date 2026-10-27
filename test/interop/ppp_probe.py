"""Run inside an isolated Linux network namespace with the real pppd interface."""
import argparse
import hashlib
import json
import socket
import struct
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', required=True)
    parser.add_argument('--dns', required=True)
    args = parser.parse_args()
    # Route/interface setup may finish just after pppd reports negotiation.
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as probe:
                probe.connect((args.server, 32321))
                source = probe.getsockname()[0]
            if source.startswith('10.'):
                break
        except OSError:
            pass
        time.sleep(0.1)
    else:
        raise RuntimeError('PPP did not establish an IP route')
    request = urllib.request.Request(f'http://{args.server}:32321/test.bin')
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    start = time.monotonic()
    with opener.open(request, timeout=90) as response:
        body = response.read()
    expected = bytes(range(256)) * 256
    if body != expected:
        raise RuntimeError('HTTP content mismatch')
    def small_download(_):
        # Independent concurrent TCP flows must retain separate NAPT mappings.
        small_opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
        with small_opener.open(f'http://{args.server}:32321/small.bin', timeout=45) as response:
            data = response.read()
        if data != bytes(range(256)) * 32:
            raise RuntimeError('Concurrent HTTP content mismatch')
        return len(data)
    with ThreadPoolExecutor(max_workers=2) as workers:
        concurrent_bytes = sum(workers.map(small_download, range(2)))
    query = struct.pack('!HHHHHH', 0x5513, 0x0100, 1, 0, 0, 0)
    query += b'\x07example\x03com\0' + struct.pack('!HH', 1, 1)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as dns:
        dns.settimeout(15)
        dns.sendto(query, (args.dns, 53))
        answer, _ = dns.recvfrom(2048)
    if len(answer) < 12 or answer[:2] != query[:2] or not (answer[2] & 0x80) or answer[3] & 15:
        raise RuntimeError('DNS query did not succeed')
    print(json.dumps({'http_bytes': len(body), 'http_sha256': hashlib.sha256(body).hexdigest(),
                      'seconds': round(time.monotonic() - start, 2), 'dns': 'passed',
                      'client_ip': source, 'concurrent_http_bytes': concurrent_bytes}), flush=True)


if __name__ == '__main__':
    main()
