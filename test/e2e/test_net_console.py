"""End-to-end checks of the network console (the maintenance console on TCP port 2001).

Run with --net-console <modem host or IPv4>, e.g. --net-console usr-modem.local. The device
password comes from USR_MODEM_PASSWORD (`.env` or the environment) and is never printed.
The stall and on/off tests also need --rs232-port. One test restarts the modem.
"""

import os
import socket
import time

import pytest

from conftest import NET_LOGGED_IN, NET_PROMPT, Port, net_console_connect

PORT = 2001


@pytest.fixture
def host(request):
    name = request.config.getoption("--net-console")
    if not name:
        pytest.skip("--net-console not given")
    return name


@pytest.fixture
def password():
    value = os.environ.get("USR_MODEM_PASSWORD", "")
    if not value:
        pytest.skip("USR_MODEM_PASSWORD not set (.env or environment)")
    return value


@pytest.fixture
def net(host, password):
    ser, replay = net_console_connect(f"socket://{host}:{PORT}", password)
    port = Port(ser, f"socket://{host}:{PORT}", password)
    port.replay = replay
    yield port
    port.ser.close()


def receive(sock, marker=None, wait=3.0):
    """Bytes from a raw socket until `marker` (or `wait` seconds, or the far end closes).
    Returns (data, closed)."""
    sock.settimeout(0.1)
    data = b""
    deadline = time.monotonic() + wait
    while time.monotonic() < deadline and (marker is None or marker not in data):
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            continue
        except ConnectionError:
            return data, True
        if not chunk:
            return data, True
        data += chunk
    return data, False


def connect(host, receive_buffer=None):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # Without it, Windows holds each line until the last is acknowledged, and while the
    # modem can't send to a client that stops reading, several long commands reach it at
    # once; their replies together outgrow the console's 16 KB UART buffer, which then
    # waits for the cable. The stall test is about the network client, not the cable.
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    if receive_buffer:
        sock.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, receive_buffer)
    sock.settimeout(5)
    sock.connect((host, PORT))
    return sock


def log_in(sock, password):
    data, _ = receive(sock, NET_PROMPT)
    assert data == NET_PROMPT
    sock.sendall(password.encode("utf-8") + b"\r")
    data, _ = receive(sock, NET_LOGGED_IN)
    assert NET_LOGGED_IN in data, "the network console refused the device password"


def wait_for_lock_out():
    time.sleep(1.2)  # the lock-out after a wrong password is one second


def test_nothing_but_the_prompt_before_log_in(host):
    sock = connect(host)
    try:
        data, closed = receive(sock, wait=2.0)
        assert data == NET_PROMPT and not closed
        sock.sendall(b"AT\r")  # not a command yet: a (wrong) password
        data, _ = receive(sock, NET_PROMPT)
        assert b"OK" not in data and b"Wrong password." in data
    finally:
        sock.close()
        wait_for_lock_out()


def test_wrong_passwords_lock_out_and_three_close(host, password):
    sock = connect(host)
    try:
        receive(sock, NET_PROMPT)
        sock.sendall(b"not-the-password\r")
        data, _ = receive(sock, b"Password: ")
        assert b"Wrong password." in data
        sock.sendall(password.encode("utf-8") + b"\r")  # within the second: refused unchecked
        data, _ = receive(sock, b"Password: ")
        assert b"Wrong password." in data and NET_LOGGED_IN not in data
        wait_for_lock_out()
        sock.sendall(b"still-not-it\r")
        data, closed = receive(sock, wait=3.0)
        assert b"Wrong password: closed." in data and closed
    finally:
        sock.close()
    wait_for_lock_out()
    sock = connect(host)
    try:
        log_in(sock, password)
    finally:
        sock.close()


def test_second_client_takes_over_when_it_logs_in(host, password):
    first = connect(host)
    second = None
    try:
        log_in(first, password)
        receive(first, wait=0.5)  # the replay
        second = connect(host)
        data, _ = receive(second, NET_PROMPT)
        assert data == NET_PROMPT
        first.sendall(b"AT\r")  # the first still works while the second is at the prompt
        data, _ = receive(first, b"OK\r\n")
        assert b"OK\r\n" in data
        second.sendall(b"not-the-password\r")  # a wrong password throws nobody out
        data, _ = receive(second, NET_PROMPT)
        assert b"Wrong password." in data
        first.sendall(b"AT\r")
        data, closed = receive(first, b"OK\r\n")
        assert b"OK\r\n" in data and not closed
        wait_for_lock_out()
        second.sendall(password.encode("utf-8") + b"\r")
        data, _ = receive(second, NET_LOGGED_IN)
        assert NET_LOGGED_IN in data
        data, closed = receive(first, wait=3.0)
        assert b"logged in from another connection; closed." in data and closed
        second.sendall(b"AT\r")
        data, _ = receive(second, b"OK\r\n")
        assert b"OK\r\n" in data
    finally:
        first.close()
        if second is not None:
            second.close()


def test_at_commands_and_status(net):
    assert net.command("AT")[-1] == "OK"
    assert net.command("AT*NETCONSOLE?") == ["ON", "OK"]
    lines = net.command("AT*STATUS")
    assert any(line.startswith("Network console: port 2001, ") and line.endswith(" logged in")
               for line in lines)


def test_boot_log_is_replayed_after_a_restart(net):
    received = net.restart()  # AT*RESTART, then logs in again
    assert b"ESP32-S3, flash" in received
    assert b"Maintenance console." in received
    assert net.command("AT")[-1] == "OK"


def test_client_that_stops_reading_does_not_stall_the_modem(host, password, rs232):
    assert rs232.command("AT")[-1] == "OK"
    sock = connect(host, receive_buffer=2048)
    try:
        log_in(sock, password)
        receive(sock, wait=0.5)  # the replay
        # The client asks for about 9.4 KB a second (AT*ABOUT) and reads nothing: about 190 KB,
        # more than the 16 KB log plus the TCP buffers, even if Windows grows its window.
        sock.settimeout(1.0)
        for _ in range(20):
            sock.sendall(b"AT*ABOUT\r")
            started = time.monotonic()
            assert rs232.command("AT", timeout=3.0)[-1] == "OK"
            assert time.monotonic() - started < 1.0, "the modem waited for the network client"
            # Slower than UART0 sends (11.5 KB/s at 115200), with room for two replies
            # arriving together: more output at once than its 16 KB buffer waits for the
            # cable, whatever the network client does.
            time.sleep(1.0)
        time.sleep(4.0)  # the console finishes writing (115200 bps)
        data, closed = receive(sock, wait=5.0)
        assert not closed
        assert b"bytes skipped]" in data  # more than the 16 KB log was missed
        sock.sendall(b"AT\r")
        data, _ = receive(sock, b"OK\r\n", wait=10.0)
        assert data.endswith(b"OK\r\n")
    finally:
        sock.close()


def test_turning_it_off_closes_the_client(host, password, net, rs232):
    try:
        net.ser.write(b"AT*NETCONSOLE=OFF\r")
        received = b""
        deadline = time.monotonic() + 3.0
        closed = False
        while time.monotonic() < deadline and not closed:
            try:
                received += net.ser.read(net.ser.in_waiting or 1)
            except Exception:  # pyserial: "socket disconnected"
                closed = True
        assert b"OK" in received and closed
        with pytest.raises(OSError):
            connect(host).close()  # nobody listens
    finally:
        assert rs232.command("AT*NETCONSOLE=ON")[-1] == "OK"
    ser, _ = net_console_connect(f"socket://{host}:{PORT}", password, timeout=15.0)
    ser.close()
