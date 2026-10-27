"""Binary PPP/SLIP and actual Wi-Fi NAPT through COM18; COM15 manages calls.

--call-server-ip selects the PC's reachable LAN address for controlled UDP traffic.
Independent Linux pppd/IP-stack HTTP acceptance is in test/interop/serial_pppd.py.
No captured packet, PAP credential or boot output is printed on failures.
"""
import contextlib
import ipaddress
import re
import socket
import struct
import threading
import time

import pytest


def checksum(data):
    if len(data) & 1:
        data += b'\0'
    total = sum(struct.unpack('!' + 'H' * (len(data) // 2), data))
    while total >> 16:
        total = (total & 65535) + (total >> 16)
    return (~total) & 65535


def ppp_crc(data):
    crc = 65535
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc


class Peer:
    def __init__(self, ser, slip=False):
        self.ser, self.slip = ser, slip
        self.pending, self.frames, self.partial = bytearray(), [], bytearray()
        self.escape = False
        self.local = '10.64.64.2'
        self.dns = None

    def send(self, protocol, payload):
        if self.slip:
            encoded = payload.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc')
            self.ser.write(b'\xc0' + encoded + b'\xc0')
        else:
            body = struct.pack('!BBH', 255, 3, protocol) + payload
            body += struct.pack('<H', ppp_crc(body) ^ 65535)
            encoded = bytearray(b'\x7e')
            for byte in body:
                encoded.extend((125, byte ^ 32) if byte < 32 or byte in (125, 126) else (byte,))
            self.ser.write(encoded + b'\x7e')

    def control(self, protocol, code, ident, options=b''):
        self.send(protocol, struct.pack('!BBH', code, ident, len(options) + 4) + options)

    def receive(self, timeout=5):
        deadline = time.monotonic() + timeout
        delimiter = 192 if self.slip else 126
        while time.monotonic() < deadline:
            if self.frames:
                return self.frames.pop(0)
            data = self.ser.read(self.ser.in_waiting or 1)
            for byte in data:
                if byte == delimiter:
                    if self.partial and not self.escape:
                        body = bytes(self.partial)
                        if self.slip:
                            self.frames.append((0x21, body))
                        elif len(body) >= 6 and ppp_crc(body) == 0xf0b8 and body[:2] == b'\xff\x03':
                            self.frames.append((int.from_bytes(body[2:4], 'big'), body[4:-2]))
                    self.partial.clear()
                    self.escape = False
                elif self.escape:
                    if self.slip:
                        if byte not in (220, 221):
                            self.partial.clear()
                            self.escape = False
                            continue
                        byte = 192 if byte == 220 else 219
                    else:
                        byte ^= 32
                    self.partial.append(byte)
                    self.escape = False
                elif byte == (219 if self.slip else 125):
                    self.escape = True
                elif len(self.partial) < 4096:
                    self.partial.append(byte)
        raise AssertionError('Internet peer response timed out (traffic omitted)')

    def negotiate(self):
        self.control(0xc021, 1, 41, bytes.fromhex('010405dc020600000000'))
        lcp_ack = ipcp_ack = peer_ipcp = False
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            protocol, body = self.receive()
            if len(body) < 4:
                continue
            code, ident, length = struct.unpack('!BBH', body[:4])
            options = body[4:length]
            if protocol == 0xc021:
                if code == 1:
                    self.control(protocol, 2, ident, options)
                    if not lcp_ack:
                        self.control(0xc021, 1, 41, bytes.fromhex('010405dc020600000000'))
                elif code == 2 and ident == 41 and not lcp_ack:
                    lcp_ack = True
                    self.control(0x8021, 1, 42, bytes.fromhex('030600000000810600000000'))
            elif protocol == 0x8021:
                if code == 1:
                    self.control(protocol, 2, ident, options)
                    peer_ipcp = True
                elif code == 3:
                    self.control(protocol, 1, 43, options)
                    for offset in range(0, len(options), 6):
                        if options[offset:offset+2] == b'\x03\x06':
                            self.local = str(ipaddress.IPv4Address(options[offset+2:offset+6]))
                        if options[offset:offset+2] == b'\x81\x06':
                            self.dns = str(ipaddress.IPv4Address(options[offset+2:offset+6]))
                elif code == 2 and ident == 43:
                    ipcp_ack = True
            if lcp_ack and peer_ipcp and ipcp_ack:
                return
        raise AssertionError('PPP did not finish LCP/IPCP (traffic omitted)')


def dial(ser, number):
    ser.reset_input_buffer()
    ser.write(('ATDT' + number + '\r').encode())
    received = bytearray()
    deadline = time.monotonic() + 10
    # Read one byte at a time: never discard PPP bytes following CONNECT.
    while time.monotonic() < deadline:
        received.extend(ser.read(1))
        if re.search(rb'CONNECT[^\r\n]*\r\n$', received):
            return
        if b'ERROR\r\n' in received or b'NO DIALTONE\r\n' in received:
            raise AssertionError('Internet dial refused (response omitted)')
    raise AssertionError('No Internet CONNECT (response omitted)')


@pytest.fixture
def ports(console, rs232):
    console.command('AT*INTERNET=HANGUP', timeout=12)
    rs232.ser.dtr = rs232.ser.rts = True
    time.sleep(0.2)
    rs232.command('ATQ0V1E0M0&B1&H1&R2&I0&D2&C1F0')
    try:
        yield console, rs232
    finally:
        rs232.ser.rts = True
        rs232.ser.rtscts = True
        console.command('AT*INTERNET=HANGUP')
        time.sleep(0.2)
        rs232.ser.reset_input_buffer()
        rs232.command('ATZ')


@pytest.mark.parametrize('number', ['666', '667', '5517546', '5517545'])
def test_internet_aliases_binary_mode_and_clean_hangup(ports, number):
    console, host = ports
    dial(host.ser, number)
    peer = Peer(host.ser, number in ('667', '5517545'))
    if not peer.slip:
        peer.negotiate()
    assert host.ser.cd
    status = '\n'.join(console.command('AT*INTERNET?'))
    assert ('SLIP ready' if peer.slip else 'PPP ready') in status
    assert console.command('ATDT667', until=('BUSY',))[-1] == 'BUSY'
    if peer.slip:
        host.ser.dtr = False
        time.sleep(0.5)
        host.ser.dtr = True
    else:
        peer.control(0xc021, 5, 99)
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            protocol, body = peer.receive()
            if protocol == 0xc021 and body[:2] == bytes((6, 99)):
                break
        else:
            raise AssertionError('PPP termination acknowledgement missing')
    deadline = time.monotonic() + 3
    while host.ser.cd and time.monotonic() < deadline:
        time.sleep(0.05)
    assert not host.ser.cd
    host.ser.reset_input_buffer()
    assert host.command('AT')[-1] == 'OK'


def udp_packet(source, destination, source_port, destination_port, payload):
    src, dst = socket.inet_aton(source), socket.inet_aton(destination)
    udp = struct.pack('!HHHH', source_port, destination_port, len(payload) + 8, 0) + payload
    pseudo = src + dst + struct.pack('!BBH', 0, 17, len(udp))
    udp = udp[:6] + struct.pack('!H', checksum(pseudo + udp) or 65535) + udp[8:]
    ip = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(udp), 81, 0, 64, 17, 0, src, dst)
    ip = ip[:10] + struct.pack('!H', checksum(ip)) + ip[12:]
    return ip + udp


@contextlib.contextmanager
def udp_echo():
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind(('0.0.0.0', 32322))
    listener.settimeout(0.2)
    sources, errors, stop = [], [], threading.Event()

    def run():
        while not stop.is_set():
            try:
                data, address = listener.recvfrom(2048)
                sources.append(address[0])
                listener.sendto(data, address)
            except socket.timeout:
                continue
            except OSError:
                if not stop.is_set():
                    errors.append(True)
                return
    worker = threading.Thread(target=run, daemon=True)
    worker.start()
    try:
        yield sources
        assert not errors
    finally:
        stop.set()
        worker.join(timeout=1)
        listener.close()


@pytest.mark.parametrize('number', ['666', '667'])
def test_internet_actual_udp_nat_binary_integrity_and_rts_pause(ports, request, number):
    if not request.config.getoption('--udp-tests'):
        pytest.skip('opt-in PC UDP echo needs pre-existing UDP 32322 reachability')
    address = request.config.getoption('--call-server-ip')
    if not address:
        pytest.skip('NAT echo needs --call-server-ip (PC LAN address)')
    console, host = ports
    modem_ip = re.search(r'IP ([0-9.]+)', '\n'.join(console.command('AT*STATUS'))).group(1)
    with udp_echo() as sources:
        dial(host.ser, number)
        peer = Peer(host.ser, number == '667')
        if not peer.slip:
            peer.negotiate()
        payload = bytes(range(256)) * 4 + b'+++AT*FACTORY\rAT*CONFIG\r\x11\x13'
        host.ser.rtscts = False  # PL2303 automatic RTS overrides manual line control.
        host.ser.rts = False
        time.sleep(0.03)
        peer.send(0x21, udp_packet(peer.local, address, 48765, 32322, payload))
        time.sleep(0.3)
        assert host.ser.in_waiting == 0
        assert 'ready' in '\n'.join(console.command('AT*INTERNET?'))
        host.ser.rts = True
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            protocol, packet = peer.receive(timeout=8)
            if protocol == 0x21:
                break
        else:
            raise AssertionError('No translated IP response')
        assert packet[9] == 17
        ihl = (packet[0] & 15) * 4
        assert checksum(packet[:ihl]) == 0
        assert packet[16:20] == socket.inet_aton(peer.local)
        udp = packet[ihl:]
        pseudo = packet[12:20] + struct.pack('!BBH', 0, 17, len(udp))
        assert not int.from_bytes(udp[6:8], 'big') or checksum(pseudo + udp) == 0
        assert udp[8:] == payload
        assert sources and sources[-1] == modem_ip


def test_ppp_negotiation_timeout_releases_host(ports):
    console, host = ports
    dial(host.ser, '666')
    deadline = time.monotonic() + 33
    while host.ser.cd and time.monotonic() < deadline:
        host.ser.read(host.ser.in_waiting or 1)
    assert not host.ser.cd
    assert 'timeout' in '\n'.join(console.command('AT*INTERNET?'))
    host.ser.reset_input_buffer()
    assert host.command('AT')[-1] == 'OK'


def ipv4_packet(source, destination, protocol, payload):
    src, dst = socket.inet_aton(source), socket.inet_aton(destination)
    header = struct.pack('!BBHHHBBH4s4s', 0x45, 0, 20 + len(payload), 82, 0, 64, protocol, 0, src, dst)
    return header[:10] + struct.pack('!H', checksum(header)) + header[12:] + payload


def ip_response(peer, protocol, timeout=10):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        kind, packet = peer.receive(timeout)
        if kind == 0x21 and len(packet) >= 20 and packet[9] == protocol:
            assert checksum(packet[:(packet[0] & 15) * 4]) == 0
            return packet
    raise AssertionError('No translated IP response')


@pytest.mark.parametrize('number', ['666', '667'])
def test_internet_nat_icmp_binary_rts_and_udp_dns(ports, number):
    console, host = ports
    dial(host.ser, number)
    peer = Peer(host.ser, number == '667')
    if not peer.slip:
        peer.negotiate()
    status = '\n'.join(console.command('AT*INTERNET?'))
    dns = re.search(r'DNS=.*\(([0-9.]+)\)', status).group(1)
    payload = bytes(range(256)) * 4 + b'+++AT*FACTORY\rAT*CONFIG\r\x11\x13'
    echo = struct.pack('!BBHHH', 8, 0, 0, 0x5513, 1) + payload
    echo = echo[:2] + struct.pack('!H', checksum(echo)) + echo[4:]
    host.ser.rtscts = False
    host.ser.rts = False
    time.sleep(0.03)
    peer.send(0x21, ipv4_packet(peer.local, dns, 1, echo))
    time.sleep(0.3)
    assert host.ser.in_waiting == 0
    assert 'ready' in '\n'.join(console.command('AT*INTERNET?'))
    host.ser.rts = True
    packet = ip_response(peer, 1)
    ihl = (packet[0] & 15) * 4
    response = packet[ihl:]
    assert response[:2] == b'\0\0' and checksum(response) == 0
    assert response[4:] == struct.pack('!HH', 0x5513, 1) + payload
    assert packet[16:20] == socket.inet_aton(peer.local)
    query = struct.pack('!HHHHHH', 0x5513, 0x0100, 1, 0, 0, 0)
    query += b'\x07example\x03com\0' + struct.pack('!HH', 1, 1)
    peer.send(0x21, udp_packet(peer.local, dns, 48765, 53, query))
    packet = ip_response(peer, 17)
    udp = packet[(packet[0] & 15) * 4:]
    assert struct.unpack('!HH', udp[:4]) == (53, 48765)
    assert udp[8:10] == query[:2] and udp[10] & 0x80 and not (udp[11] & 15)
    pseudo = packet[12:20] + struct.pack('!BBH', 0, 17, len(udp))
    assert not int.from_bytes(udp[6:8], 'big') or checksum(pseudo + udp) == 0


@pytest.mark.parametrize('number', ['666', '667'])
def test_console_internet_call_managed_from_rs232(console, rs232, number):
    console.command('AT*INTERNET=HANGUP', timeout=12)
    rs232.ser.rts = rs232.ser.dtr = True
    console.command('ATQ0V1E0M0&B1&H1&R2&I0&D2&C1F0')
    try:
        dial(console.ser, number)
        peer = Peer(console.ser, number == '667')
        if not peer.slip:
            peer.negotiate()
        assert 'ready' in '\n'.join(rs232.command('AT*INTERNET?'))
        assert rs232.command('AT*INTERNET=HANGUP')[-1] == 'OK'
        time.sleep(0.2)
        console.ser.reset_input_buffer()
        assert console.command('AT')[-1] == 'OK'
    finally:
        rs232.command('AT*INTERNET=HANGUP')
        time.sleep(0.2)
        console.command('ATZ')


def test_internet_settings_survive_restart_and_export(console, rs232):
    console.command('AT*INTERNET=HANGUP')
    before = '\n'.join(console.command('AT*INTERNET?'))
    pair = re.search(r'IP=([0-9.]+),([0-9.]+)', before).groups()
    dns = 'AUTO' if 'DNS=AUTO' in before else re.search(r'DNS=fixed \(([0-9.]+)\)', before).group(1)
    try:
        assert console.command('AT*INTERNET=IP,10.64.64.5,10.64.64.6')[-1] == 'OK'
        assert console.command('AT*INTERNET=DNS,1.1.1.1')[-1] == 'OK'
        console.restart()
        status = '\n'.join(rs232.command('AT*INTERNET?'))
        assert 'IP=10.64.64.5,10.64.64.6' in status and 'DNS=fixed (1.1.1.1)' in status
        exported = '\n'.join(console.command('AT*EXPORT'))
        assert 'AT*INTERNET=IP,10.64.64.5,10.64.64.6' in exported
        assert 'AT*INTERNET=DNS,1.1.1.1' in exported
    finally:
        console.command('AT*INTERNET=IP,' + ','.join(pair))
        console.command('AT*INTERNET=DNS,' + dns)


def test_internet_wifi_loss_cleans_up_and_allows_redial(ports):
    from test_wifi import _wait_connected
    console, host = ports
    try:
        dial(host.ser, '667')
        console.command('AT*WIFI=OFF')
        deadline = time.monotonic() + 3
        while host.ser.cd and time.monotonic() < deadline:
            time.sleep(0.05)
        assert not host.ser.cd
        assert 'network lost' in '\n'.join(console.command('AT*INTERNET?'))
    finally:
        console.command('AT*WIFI=ON')
        _wait_connected(console)
    host.ser.reset_input_buffer()
    dial(host.ser, '667')
    assert 'SLIP ready' in '\n'.join(console.command('AT*INTERNET?'))
