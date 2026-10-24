"""End-to-end checks of the RS232 host port (v0.5, brief 6.2) through the Unitek
adapter. Roadmap v0.5 "done when": these pass at 300-115200 baud.

Needs both ports and the RS232 section wired (MAX3232s and the bench DB9 or the DB25):
    python -m pytest test/e2e --console-port COM15 --rs232-port COMy

The console sets the modem up between tests (ATZ, AT*BAUD=AUTO); the RS232 port is the
computer. Speeds change only on the PC side: the modem must follow from "AT" alone.
"""

import time

import pytest

RATES = [300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
GUARD = 1.2


@pytest.fixture(autouse=True)
def clean_state(request):
    if "rs232" not in request.fixturenames:
        yield
        return
    console = request.getfixturevalue("console")
    rs232 = request.getfixturevalue("rs232")
    _reset(console, rs232)
    yield
    _reset(console, rs232)


def _reset(console, rs232):
    rs232.ser.dtr = True
    rs232.ser.baudrate = 38400
    if b"OK" not in rs232.raw(b"ATH\r", wait=0.5):  # still online in a test call
        time.sleep(GUARD)
        rs232.raw(b"+++", wait=GUARD + 0.3)
        rs232.raw(b"ATH\r")
    console.raw(b"AT*BAUD=AUTO\r")
    console.raw(b"ATZ\r")


def _at(rs232, baud, text="AT", timeout=None):
    rs232.ser.baudrate = baud
    time.sleep(0.05)
    # 32 ms for the capture to hand over the burst, plus the characters both ways.
    wait = timeout or (0.3 + 2 * (len(text) + 12) * 10 / baud)
    return rs232.command(text, timeout=wait + 1.0)


@pytest.mark.parametrize("baud", RATES)
def test_speed_is_detected_from_at(console, rs232, baud):
    lines = _at(rs232, baud)
    assert lines[-1] == "OK"
    lines = _at(rs232, baud, "ATI4")
    assert f"BAUD={baud}  PARITY=N  WORDLEN=8" in lines


def test_speed_changes_down_and_up(console, rs232):
    for baud in (115200, 1200, 57600, 300, 38400):
        assert _at(rs232, baud)[-1] == "OK"
    assert console.command("AT*BAUD?")[0] == 'AUTO (from "AT"), now 38400'


def test_lower_case_at(console, rs232):
    assert _at(rs232, 9600, "at")[-1] == "OK"


def test_fixed_speed_ignores_other_speeds(console, rs232):
    assert console.command("AT*BAUD=9600")[-1] == "OK"
    assert _at(rs232, 9600)[-1] == "OK"
    rs232.ser.baudrate = 38400
    assert b"OK" not in rs232.raw(b"AT\r", wait=0.8)
    assert _at(rs232, 9600)[-1] == "OK"


def test_w_stores_the_port_speed(console, rs232):
    try:
        assert _at(rs232, 19200, "AT&W0")[-1] == "OK"
        lines = console.command("ATI5")
        assert "BAUD=19200  PARITY=N  WORDLEN=8" in lines
    finally:
        console.command("AT&F1&W0")


def test_cts_on_and_dcd_follows_the_call(console, rs232):
    assert _at(rs232, 38400)[-1] == "OK"
    assert rs232.ser.cts  # &H1: on while the modem can take data
    assert not rs232.ser.cd  # &C1, no call
    lines = rs232.command("AT*TESTCALL", until=("CONNECT 33600", "ERROR"))
    assert lines[-1] == "CONNECT 33600"
    time.sleep(0.1)
    assert rs232.ser.cd
    time.sleep(GUARD)
    assert rs232.raw(b"+++", wait=GUARD + 0.4).endswith(b"OK\r\n")
    assert rs232.ser.cd  # still connected in online-command mode
    assert rs232.command("ATH")[-1] == "OK"
    time.sleep(0.1)
    assert not rs232.ser.cd
    assert rs232.command("AT&C0")[-1] == "OK"
    time.sleep(0.1)
    assert rs232.ser.cd  # &C0: always on


def test_dtr_drop_hangs_up(console, rs232):
    assert _at(rs232, 38400)[-1] == "OK"
    rs232.command("AT*TESTCALL", until=("CONNECT 33600", "ERROR"))
    rs232.ser.reset_input_buffer()
    rs232.ser.dtr = False
    time.sleep(0.3)
    assert b"NO CARRIER" in rs232.ser.read(rs232.ser.in_waiting or 1)
    rs232.ser.dtr = True
    assert rs232.command("AT")[-1] == "OK"


def test_dtr_ignored_with_amp_d0(console, rs232):
    assert _at(rs232, 38400, "AT&D0")[-1] == "OK"
    rs232.command("AT*TESTCALL", until=("CONNECT 33600", "ERROR"))
    rs232.ser.dtr = False
    time.sleep(0.3)
    rs232.ser.dtr = True
    assert rs232.raw(b"still here", wait=0.4) == b"still here"


@pytest.mark.parametrize("baud", [9600, 115200])
def test_bulk_echo_with_hardware_flow_control(console, rs232, baud):
    """More data than the modem's buffers hold, sent as fast as the port allows: RTS/CTS
    must keep every byte."""
    assert _at(rs232, baud)[-1] == "OK"
    rs232.command("AT*TESTCALL", until=("CONNECT 33600", "ERROR"))
    data = bytes((i * 7 + 3) % 256 for i in range(16384)).replace(b"+", b"-")
    received = b""
    deadline = time.monotonic() + len(data) * 10 / baud * 2 + 5
    rs232.ser.write_timeout = 0
    sent = 0
    while len(received) < len(data) and time.monotonic() < deadline:
        if sent < len(data):
            sent += rs232.ser.write(data[sent : sent + 1024]) or 0
        received += rs232.ser.read(rs232.ser.in_waiting or 1)
    assert len(received) == len(data)
    assert received == data


def test_line_tests_refused_on_rs232(console, rs232):
    assert _at(rs232, 38400)[-1] == "OK"
    for cmd in ("AT*HWLINES", "AT*HWLOOP", "AT*HWBRIDGE", "AT*HWSIG=DCD,ON"):
        lines = rs232.command(cmd)
        assert lines[-1] == "ERROR"
        assert any("not available on the RS232 port" in line for line in lines)
    assert rs232.command("AT*HWPINS")[-1] == "OK"  # the other tests do run


def test_console_call_blocks_rs232_call(console, rs232):
    assert _at(rs232, 38400)[-1] == "OK"
    console.command("AT*TESTCALL", until=("CONNECT 33600", "ERROR"))
    try:
        assert rs232.command("AT*TESTCALL")[-1] == "ERROR"  # one call at a time
    finally:
        time.sleep(GUARD)
        console.raw(b"+++", wait=GUARD + 0.3)
        console.raw(b"ATH\r")
