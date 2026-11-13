"""Incoming port-23 calls from the PC to the modem. COM15 and Wi-Fi suffice.

No PC listener or firewall change is needed. External RS232 remains opt-in through
the existing separate suite; incoming auto-answer uses the explicit CONSOLE choice.
"""
import contextlib
import re
import socket
import time

import pytest


def require(condition, message):
    # Keep serial buffers out of pytest's rewritten assertion diagnostics.
    if not condition:
        raise AssertionError(message + " (traffic omitted for privacy)")


def read_serial(console, duration=0.3):
    received = bytearray()
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        received.extend(console.ser.read(console.ser.in_waiting or 1))
    return bytes(received)


def wait_serial(console, marker, timeout=8):
    received = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        received.extend(console.ser.read(console.ser.in_waiting or 1))
        if marker in received:
            return bytes(received)
    raise AssertionError("Expected serial event absent (traffic omitted for privacy)")


def recover(console):
    console.ser.write(b"x")  # cancels a pending answer handshake
    time.sleep(1.2)
    console.ser.write(b"+++")
    time.sleep(1.3)
    console.ser.write(b"ATH\r")
    time.sleep(0.2)
    console.ser.write(b"ATE1Q0V1\r")
    read_serial(console)


class Peer:
    """Small Telnet peer: preserve payload and wire bytes across partial IAC reads."""
    def __init__(self, connection):
        self.connection = connection
        self.wire = bytearray()
        self.payload = bytearray()
        self.state = 0

    def send(self, payload):
        self.connection.sendall(payload.replace(b"\xff", b"\xff\xff"))

    def read(self, duration=0.25):
        deadline = time.monotonic() + duration
        while time.monotonic() < deadline:
            try:
                data = self.connection.recv(4096)
            except socket.timeout:
                continue
            if not data:
                break
            self.wire.extend(data)
            for byte in data:
                if self.state == 0:
                    if byte == 255:
                        self.state = 1
                    else:
                        self.payload.append(byte)
                elif self.state == 1:
                    if byte == 255:
                        self.payload.append(byte)
                        self.state = 0
                    elif byte in (251, 252, 253, 254):
                        self.state = 2
                    elif byte == 250:
                        self.state = 3
                    else:
                        self.state = 0
                elif self.state == 2:
                    self.state = 0
                elif self.state == 3:
                    if byte == 255:
                        self.state = 4
                elif self.state == 4:
                    self.state = 0 if byte == 240 else 3
        return bytes(self.payload)


@pytest.fixture
def answer_console(console):
    # Normalize transport first, as other serial suites do. Capture active settings
    # after recovery and restore everything this fixture subsequently changes.
    recover(console)
    profile = "\n".join(console.command("ATI4"))
    letters = dict(re.findall(r"(?<!\S)([EMVQX])(\d+)", profile))
    registers = dict(re.findall(r"S(\d{2})=(\d+)", profile))
    before_port = console.command("AT*ANSWERPORT?")[0]
    before_model = console.command("AT*MODEL?")[0].split()[0]
    status = "\n".join(console.command("AT*WIFI?"))
    match = re.search(r"Address:\s*(\d+\.\d+\.\d+\.\d+)", status)
    if not match:
        pytest.skip("incoming calls need the modem connected to the PC's network")
    address = match.group(1)
    assert console.command("AT*ANSWERPORT=CONSOLE")[-1] == "OK"
    assert console.command("AT*MODEL=14400")[-1] == "OK"
    assert console.command("ATE1Q0V1X4M0S0=0S12=50")[-1] == "OK"
    peers = []

    @contextlib.contextmanager
    def caller():
        connection = socket.create_connection((address, 23), timeout=5)
        connection.settimeout(0.05)
        peers.append(connection)
        try:
            yield Peer(connection)
        finally:
            connection.close()

    try:
        yield console, caller
    finally:
        for connection in peers:
            connection.close()
        recover(console)
        console.command("AT*WIFI=ON")
        console.command("AT*ANSWERPORT=" + before_port)
        console.command("AT*MODEL=" + before_model)
        restoration = "AT" + "".join(k + v for k, v in letters.items())
        restoration += "S0=" + str(int(registers.get("00", "0")))
        restoration += "S12=" + str(int(registers.get("12", "50")))
        console.raw(restoration.encode("ascii") + b"\r", wait=0.2)


def answer(console):
    console.ser.write(b"ATA\r")
    return wait_serial(console, b"CONNECT 14400")


def escape(console):
    time.sleep(1.15)
    console.ser.write(b"+++")
    wait_serial(console, b"OK\r\n", timeout=3)  # the whole line, or its end looks like caller data


def test_manual_answer_binary_telnet_and_local_hangup(answer_console):
    console, caller = answer_console
    with caller() as peer:
        wait_serial(console, b"RING")
        answer(console)
        payload = bytes(range(256)) * 4
        peer.send(payload)
        incoming = wait_serial(console, payload)
        require(payload in incoming, "incoming binary data mismatch")
        console.ser.write(payload)
        require(peer.read(1.0) == payload, "outgoing binary data mismatch")
        escape(console)
        assert console.command("ATH")[-1] == "OK"
    assert console.command("ATA")[-1] == "ERROR"


@pytest.mark.parametrize("rings", [1, 3])
def test_auto_answer_exact_threshold(answer_console, rings):
    console, caller = answer_console
    assert console.command(f"ATS0={rings}")[-1] == "OK"
    console.ser.reset_input_buffer()
    started = time.monotonic()
    with caller():
        response = wait_serial(console, b"CONNECT 14400", timeout=12)
        elapsed = time.monotonic() - started
        require(response.count(b"RING") == rings, "incorrect ring threshold")
        require(response.count(b"CONNECT") == 1, "duplicate/missing CONNECT")
        assert elapsed >= (rings - 1) * 2.9
    wait_serial(console, b"NO CARRIER")
    assert console.command("AT")[-1] == "OK"


def test_s0_zero_and_pending_ath_recover(answer_console):
    console, caller = answer_console
    with caller():
        response = read_serial(console, 3.5)
        require(response.count(b"RING") == 2, "manual ring cadence incorrect")
        require(b"CONNECT" not in response, "S0=0 answered automatically")
        assert console.command("ATH")[-1] == "OK"
    with caller():
        wait_serial(console, b"RING")
        answer(console)


def test_remote_commands_and_guarded_escape_never_dispatch(answer_console):
    console, caller = answer_console
    attack = b"AT\rATA\rATH\rAT*HELP\rAT*PASS=canary\rAT*FACTORY\rAT*CONFIG\rATDT123\rAT*HWTEST\r"
    with caller() as peer:
        wait_serial(console, b"RING")
        peer.send(attack)  # queued before ATA, still data
        read_serial(console, 0.2)
        console.ser.write(b"ATA\r")
        response = wait_serial(console, attack[-12:])
        require(b"CONNECT 14400" in response and attack in response, "preanswer data mismatch")
        require(peer.read() == b"", "unexpected caller-facing text")
        time.sleep(1.15)
        peer.send(b"+++")
        response = wait_serial(console, b"+++", timeout=3)
        require(b"OK" not in response, "remote escape changed local mode")
        time.sleep(1.15)
        escape(console)
        peer.read()  # discard the local escape bytes forwarded as ordinary data
        peer.payload.clear()
        peer.send(attack)
        require(read_serial(console, 0.5) == b"", "caller bytes reached local command mode")
        assert console.command("AT*MODEL?")[0].startswith("14400")
        assert console.command("AT*ANSWERPORT?")[0] == "CONSOLE"
        console.ser.write(b"ATO\r")
        response = wait_serial(console, attack[-12:])
        require(b"CONNECT 14400" in response and attack in response, "escaped data mismatch")
        require(peer.read() == b"", "remote command produced a response")


def test_second_caller_and_setting_change_rejected(answer_console):
    console, caller = answer_console
    with caller() as first:
        wait_serial(console, b"RING")
        assert console.command("AT*ANSWERPORT=RS232")[-1] == "ERROR"
        with caller() as second:
            time.sleep(0.15)
            try:
                rejected = second.connection.recv(32) == b""
            except ConnectionResetError:
                rejected = True
            assert rejected, "second caller was not rejected"
        answer(console)
        first.send(b"original caller")
        wait_serial(console, b"original caller")


def test_fragmented_telnet_options_and_control_commands(answer_console):
    console, caller = answer_console
    with caller() as peer:
        wait_serial(console, b"RING")
        request = bytes([255, 253, 0, 255, 251, 0, 255, 253, 24, 255, 253, 31,
                         255, 250, 24, 1, 255, 240, 255, 243, 255, 244])
        for byte in request:
            peer.connection.sendall(bytes([byte]))
            time.sleep(0.003)
        peer.read(0.5)
        require(b"\xff\xfa\x18\x00ANSI\xff\xf0" in peer.wire, "ANSI negotiation failed")
        require(b"\xff\xfa\x1f\x00P\x00\x18\xff\xf0" in peer.wire, "NAWS negotiation failed")
        answer(console)
        peer.send(b"still connected")
        wait_serial(console, b"still connected")


def test_wifi_loss_and_listener_recovery(answer_console):
    from test_wifi import _wait_connected
    console, caller = answer_console
    try:
        with caller():
            wait_serial(console, b"RING")
            answer(console)
            escape(console)
            before = "\n".join(console.command("ATI4"))
            require("Q0" in before and "OFF HOOK" in before, "call setup was not verbose/online")
            console.ser.reset_input_buffer()
            console.ser.write(b"AT*WIFI=OFF\r")
            response = read_serial(console, 5)
            if b"NO CARRIER" not in response:
                flags = {name: marker in response for name, marker in {
                    "ok": b"OK", "error": b"ERROR", "reboot": b"USR Modem Emulator",
                    "panic": b"Guru Meditation", "assert": b"assert failed"}.items()}
                raise AssertionError(f"Wi-Fi hangup event missing; safe flags={flags}")
            require(response.count(b"NO CARRIER") == 1, "duplicate carrier-loss results")
    finally:
        console.command("AT*WIFI=ON")
        _wait_connected(console)
    with caller():
        wait_serial(console, b"RING")
        answer(console)


def test_answerport_survives_restart(answer_console):
    console, caller = answer_console
    console.restart()
    assert console.command("AT*ANSWERPORT?")[0] == "CONSOLE"
    assert console.command("ATM0S0=0")[-1] == "OK"
    with caller():
        wait_serial(console, b"RING")
        answer(console)


def test_q2_suppresses_cancelled_answer_handshake(answer_console):
    console, caller = answer_console
    assert console.command("ATE0Q2M1")[-1] == "OK"
    with caller():
        require(read_serial(console, 0.2) == b"", "Q2 emitted RING")
        console.ser.write(b"ATA\r")
        time.sleep(0.15)
        console.ser.write(b"x")
        require(read_serial(console, 0.5) == b"", "Q2 emitted answer cancellation result")
        assert console.command("ATE1Q0M0")[-1] == "OK"
