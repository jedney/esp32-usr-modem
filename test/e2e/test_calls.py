"""Calls through a local PC server; no RS232 wiring needed.

Set --call-server-ip to the PC address reachable from the modem. These tests do
not change firewall settings. --live-bbs additionally checks ATDT1 against the BBS.
"""
import contextlib
import ctypes
import os
import socket
import threading
import time

import pytest

SERVER_PORT = 32321


def _hangup(console):
    console.raw(b"x", wait=0.1)  # cancel a pending dial, if any
    time.sleep(1.1)
    console.raw(b"+++", wait=1.3)
    console.raw(b"ATH\r", wait=0.2)
    console.raw(b"ATZ\r", wait=0.2)
    console.raw(b"ATE1Q0V1X4\r", wait=0.2)


@pytest.fixture
def call_console(console):
    _hangup(console)
    before_model = console.command("AT*MODEL?")[0].split()[0]
    before_throttle = console.command("AT*THROTTLE?")[0]
    before_ring_delay = console.command("AT*RINGDELAY?")[0]
    before_connect_delay = console.command("AT*CONNECTDELAY?")[0]
    assert console.command("AT*MODEL=14400")[-1] == "OK"
    assert console.command("AT*THROTTLE=OFF")[-1] == "OK"
    assert console.command("AT&F1M0S7=5", timeout=8)[-1] == "OK"
    try:
        yield console
    finally:
        _hangup(console)
        console.command("AT*MODEL=" + before_model)
        console.command("AT*THROTTLE=" + before_throttle)
        console.command("AT*RINGDELAY=" + before_ring_delay)
        console.command("AT*CONNECTDELAY=" + before_connect_delay)
        console.command("ATZ")


@pytest.fixture
def server_ip(request):
    global SERVER_PORT
    SERVER_PORT = request.config.getoption("--call-server-port")
    address = request.config.getoption("--call-server-ip")
    if not address:
        pytest.skip("local call server needs --call-server-ip (PC LAN address)")
    return address


@contextlib.contextmanager
def server(handler):
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", SERVER_PORT))
    listener.listen(1)
    listener.settimeout(0.2)
    errors = []
    stopped = threading.Event()

    def run():
        try:
            while not stopped.is_set():
                try:
                    connection, _ = listener.accept()
                    break
                except socket.timeout:
                    continue
            else:
                return
            with connection:
                connection.settimeout(5)
                handler(connection)
        except (OSError, AssertionError) as exc:
            if not stopped.is_set():
                errors.append(type(exc).__name__)

    thread = threading.Thread(target=run, daemon=True)
    thread.start()
    try:
        yield listener.getsockname()[1]
    finally:
        stopped.set()
        listener.close()
        thread.join(timeout=6)
        assert not errors, "local call server failed (traffic omitted)"


def echo(connection):
    while True:
        data = connection.recv(2048)
        if not data:
            return
        connection.sendall(data)


@contextlib.contextmanager
def refusal_server():
    """Reject on a listening Windows socket so the program-scoped rule applies.

    SO_CONDITIONAL_ACCEPT and WSAAccept/CF_REJECT are documented at
    https://learn.microsoft.com/windows/win32/winsock/so-conditional-accept
    No application connection is accepted and no firewall policy is changed.
    """
    with socket.socket() as listener:
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        if os.name != "nt":
            listener.bind(("0.0.0.0", SERVER_PORT))
            yield SERVER_PORT  # non-listening POSIX port: kernel sends refusal
            return

        winsock = ctypes.WinDLL("Ws2_32.dll")
        condition_type = ctypes.WINFUNCTYPE(
            ctypes.c_int, *([ctypes.c_void_p] * 7), ctypes.c_size_t
        )
        winsock.WSAAccept.argtypes = [
            ctypes.c_size_t, ctypes.c_void_p, ctypes.c_void_p,
            condition_type, ctypes.c_size_t,
        ]
        winsock.WSAAccept.restype = ctypes.c_size_t
        winsock.WSAGetLastError.restype = ctypes.c_int
        stopped, rejected = threading.Event(), threading.Event()
        errors = []

        @condition_type
        def reject(*args):
            rejected.set()
            return 1  # CF_REJECT

        listener.setsockopt(socket.SOL_SOCKET, 0x3002, 1)  # SO_CONDITIONAL_ACCEPT
        listener.bind(("0.0.0.0", SERVER_PORT))
        listener.listen(1)
        listener.setblocking(False)

        def run():
            while not stopped.is_set():
                result = winsock.WSAAccept(listener.fileno(), None, None, reject, 0)
                if result != ctypes.c_size_t(-1).value:
                    socket.close(result)
                    errors.append("unexpected acceptance")
                    return
                error = winsock.WSAGetLastError()
                if error not in (10035, 10061):  # WSAEWOULDBLOCK / WSAECONNREFUSED
                    errors.append(str(error))
                    return
                stopped.wait(0.01)

        thread = threading.Thread(target=run, daemon=True)
        thread.start()
        try:
            yield SERVER_PORT
        finally:
            stopped.set()
            thread.join(timeout=2)
            assert not thread.is_alive(), "refusal server did not stop"
            assert not errors, "conditional refusal server failed"
            assert rejected.is_set(), "refusal server received no request"


def dial(console, host, port, speed="14400"):
    lines = console.command(f"ATDT{host}:{port}", timeout=8,
                            until=("CONNECT " + speed, "ERROR", "BUSY", "NO ANSWER", "NO DIAL TONE", "NO CARRIER"))
    assert lines[-1] == "CONNECT " + speed


def escape(console):
    time.sleep(1.1)
    received = console.raw(b"+++", wait=1.4)
    escaped = received.endswith(b"OK\r\n")
    del received
    assert escaped, "escape failed (traffic omitted)"


def test_raw_call_all_bytes_and_remote_drop(call_console, server_ip):
    console = call_console
    # Save/restore a temporary exact phonebook key, including leading zeros.
    key = "99999999999"
    before = [line for line in console.command("AT*PHONEBOOK?") if line.startswith(key + ",")]
    try:
        with server(echo) as port:
            assert console.command(f"AT*PHONEBOOK={key},{server_ip},{port},RAW")[-1] == "OK"
            assert console.command("ATDT" + key, timeout=8, until=("CONNECT 14400", "ERROR", "NO ANSWER"))[-1] == "CONNECT 14400"
            data = bytes(range(256)) * 8
            received = console.raw(data, wait=1.0)
            matched = received == data
            del received
            assert matched, "binary echo mismatch (traffic omitted)"
            escape(console)
            assert console.command("ATH")[-1] == "OK"
    finally:
        _hangup(console)
        console.command("AT*PHONEDELETE=" + key)
        if before:
            console.command("AT*PHONEBOOK=" + before[0])


def test_telnet_negotiation_ansi_naws_binary_and_ff(call_console, server_ip):
    console = call_console
    payload = b"A\xffB\x00C"
    got = bytearray()
    def telnet_peer(connection):
        request = bytes([255,253,0,255,251,0,255,253,24,255,253,31,255,250,24,1,255,240])
        # Deliberately split IAC and subnegotiation across TCP writes.
        for byte in request:
            connection.sendall(bytes([byte]))
            time.sleep(0.003)
        deadline = time.monotonic() + 4
        expected_type = b"\xff\xfa\x18\x00ANSI\xff\xf0"
        expected_size = b"\xff\xfa\x1f\x00P\x00\x18\xff\xf0"
        while time.monotonic() < deadline and (expected_type not in got or expected_size not in got):
            got.extend(connection.recv(256))
        assert expected_type in got and expected_size in got
        expected = payload.replace(b"\xff", b"\xff\xff")
        # Host data can arrive alongside the initial negotiation replies.
        received = bytearray(got)
        while expected not in received:
            data = connection.recv(256)
            assert data, "peer closed before the synthetic payload"
            received.extend(data)
        connection.sendall(expected + b"done")
        time.sleep(0.5)
    with server(telnet_peer) as port:
        dial(console, server_ip, port)
        received = console.raw(payload, wait=1.0)
        matched = payload in received
        del received
        assert matched, "Telnet echo mismatch (traffic omitted)"
        time.sleep(0.6)
        assert console.command("AT")[-1] == "OK"


def test_connection_refused(call_console, server_ip):
    console = call_console
    with refusal_server() as port:
        lines = console.command(f"ATDT{server_ip}:{port}", timeout=8,
                                until=("BUSY", "ERROR", "NO ANSWER"))
        assert lines[-1] == "BUSY"

def test_dns_failure(call_console):
    console = call_console
    lines = console.command("ATDTmissing-v08.invalid:23", timeout=8,
                            until=("NO DIAL TONE", "NO ANSWER", "ERROR"))
    assert lines[-1] == "NO DIAL TONE"


def test_connection_deadline_and_any_key_cancel(call_console, server_ip):
    console = call_console
    assert console.command("ATS7=0")[-1] == "OK"
    with server(echo) as port:
        lines = console.command(f"ATDT{server_ip}:{port}", timeout=5,
                                until=("NO ANSWER", "ERROR", "NO CARRIER"))
        assert lines[-1] == "NO ANSWER"
    assert console.command("ATM1S6=5S7=5")[-1] == "OK"
    console.ser.reset_input_buffer()
    # 192.0.2.1 (RFC 5737 TEST-NET-1) never answers, so the connection stays pending.
    console.ser.write(b"ATDT192.0.2.1:23\r")
    time.sleep(0.1)
    result = console.raw(b"x", wait=0.5)
    cancelled = b"NO CARRIER" in result
    del result  # do not expose a possible reboot banner in assertion output
    assert cancelled
    assert console.command("AT")[-1] == "OK"


def test_wifi_loss_hangs_up_online_call(call_console, server_ip):
    from test_wifi import _wait_connected
    console = call_console
    try:
        with server(echo) as port:
            dial(console, server_ip, port)
            escape(console)
            result = console.raw(b"AT*WIFI=OFF\r", wait=1.0)
            disconnected = b"NO CARRIER" in result
            del result
            assert disconnected
            assert console.command("ATDT1", until=("NO DIAL TONE", "ERROR"))[-1] == "NO DIAL TONE"
    finally:
        console.command("AT*WIFI=ON")
        _wait_connected(console)


def test_model_changes_during_call_keep_ato_speed(call_console, server_ip):
    console = call_console
    assert console.command("AT*MODEL=56000")[-1] == "OK"
    with server(echo) as port:
        dial(console, server_ip, port, "56000")
        escape(console)
        assert console.command("AT*MODEL=28800")[-1] == "OK"
        assert console.command("ATO", until=("CONNECT 56000", "ERROR"))[-1] == "CONNECT 56000"
        escape(console)
        assert console.command("ATH")[-1] == "OK"
    with server(echo) as port:
        dial(console, server_ip, port, "28800")
        escape(console)
        console.command("ATH")


def test_streamed_handshake_completes_before_connect(call_console, server_ip):
    console = call_console
    assert console.command("AT&F1M1L2S6=1S7=30")[-1] == "OK"
    assert console.command("AT*RINGDELAY=1500")[-1] == "OK"
    assert console.command("AT*CONNECTDELAY=1000")[-1] == "OK"

    def patient_echo(connection):
        connection.settimeout(35)
        echo(connection)

    with server(patient_echo) as port:
        started = time.monotonic()
        result = console.command(f"ATDT{server_ip}:{port}", timeout=35,
                                 until=("CONNECT 14400", "CONNECT 28800", "CONNECT 33600", "CONNECT 56000", "NO CARRIER", "NO ANSWER", "ERROR"))
        assert result[-1] == "CONNECT 14400"
        # Ringing, handshake and the 1000 ms pause all come before CONNECT.
        assert time.monotonic() - started >= 17, "CONNECT preceded the handshake and delay"
        connected_at = time.monotonic()
        console.ser.write(b"streamed")
        received = b""
        deadline = connected_at + 4
        while b"streamed" not in received and time.monotonic() < deadline:
            received += console.ser.read(console.ser.in_waiting or 1)
        matched = b"streamed" in received
        del received
        assert matched, "streamed call echo mismatch (traffic omitted)"
        escape(console)
        assert console.command("ATH")[-1] == "OK"


def test_call_delays_saved_across_restart_and_factory_profile_retains_them(call_console):
    console = call_console
    assert console.command("AT*RINGDELAY=2345")[-1] == "OK"
    assert console.command("AT*CONNECTDELAY=678")[-1] == "OK"
    console.restart()
    assert console.command("AT*RINGDELAY?")[0] == "2345"
    assert console.command("AT*CONNECTDELAY?")[0] == "678"
    assert console.command("AT&F1")[-1] == "OK"
    assert console.command("AT*RINGDELAY?")[0] == "2345"
    assert console.command("AT*CONNECTDELAY?")[0] == "678"


def test_model_saved_across_restart_and_factory_profile_retains_it(call_console):
    console = call_console
    assert console.command("AT*MODEL=33600")[-1] == "OK"
    console.restart()
    assert console.command("AT*MODEL?")[0].startswith("33600 ")
    assert console.command("AT&F1")[-1] == "OK"
    assert console.command("AT*MODEL?")[0].startswith("33600 ")
    assert console.command("ATI3")[0] == "Sportster 33.6k Series"


def test_live_amiga_retro_bbs(call_console, request):
    if not request.config.getoption("--live-bbs"):
        pytest.skip("live BBS needs --live-bbs")
    console = call_console
    assert console.command("AT*MODEL=14400")[-1] == "OK"
    assert console.command("ATDT1", timeout=65,
                           until=("CONNECT 14400", "BUSY", "NO ANSWER", "NO DIAL TONE", "NO CARRIER", "ERROR"))[-1] == "CONNECT 14400"
    greeting = console.raw(b"\r", wait=3)
    assert greeting, "BBS sent no data"
    escape(console)
    console.command("ATH")
