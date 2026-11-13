"""End-to-end checks of the port sessions on the USB console (v0.5, brief 6.2):
AT*TESTCALL, the +++ escape with guard time, ATO, ATH, NO CARRIER, AT*BAUD, the boot
banner and the LED / line hand-back commands. They need only the console.

The console has no DTR or DCD; test_rs232.py checks those on the RS232 port.
"""

import time

import pytest

GUARD = 1.2  # S12 = 50 (1 s) plus a margin


@pytest.fixture(autouse=True)
def clean_state(request):
    if "console" not in request.fixturenames:
        yield
        return
    console = request.getfixturevalue("console")
    _hang_up(console)
    console.raw(b"ATZ\r")
    assert console.command("AT&N8")[-1] == "OK"
    yield
    _hang_up(console)
    console.raw(b"ATZ\r")


def _hang_up(console):
    """Ends a test call left over from a failed test. In command mode (or after +++)
    ATH answers OK; online, the echo call just sends it back."""
    if b"OK" in console.raw(b"ATH\r"):
        return
    time.sleep(GUARD)
    console.raw(b"+++", wait=GUARD + 0.3)
    console.raw(b"ATH\r")


def _call(console, args=""):
    lines = console.command(f"AT*TESTCALL{args}", until=("CONNECT 14400", "ERROR"))
    assert lines[-1] == "CONNECT 14400"


def _escape(console):
    time.sleep(GUARD)
    received = console.raw(b"+++", wait=GUARD + 0.4)
    assert received.endswith(b"OK\r\n"), received


def test_test_call_echoes_data(console):
    _call(console)
    assert console.raw(b"hello, echo", wait=0.4) == b"hello, echo"
    data = bytes(range(32, 127)) * 4
    assert console.raw(data, wait=0.6) == data


def test_escape_ato_and_ath(console):
    _call(console)
    _escape(console)
    lines = console.command("ATI4")
    assert any("OFF HOOK" in line for line in lines)
    lines = console.command("ATO", until=("CONNECT 14400", "ERROR"))
    assert lines[-1] == "CONNECT 14400"
    assert console.raw(b"back online", wait=0.4) == b"back online"
    _escape(console)
    assert console.command("ATH")[-1] == "OK"
    assert console.command("ATO")[-1] == "ERROR"  # no call any more
    assert any("ON HOOK" in line for line in console.command("ATI4"))


def test_plus_without_guard_time_is_data(console):
    _call(console)
    received = console.raw(b"a+++", wait=GUARD + 0.5)
    assert received == b"a+++"  # echoed back, no OK: still online
    assert console.raw(b"x", wait=0.3) == b"x"


def test_s12_changes_the_guard_time(console):
    assert console.command("ATS12=10")[-1] == "OK"  # 0.2 s
    _call(console)
    time.sleep(0.3)
    received = console.raw(b"+++", wait=0.6)
    assert received.endswith(b"OK\r\n"), received
    assert console.command("ATH")[-1] == "OK"


def test_far_end_hangs_up(console):
    _call(console, "=1")
    console.ser.reset_input_buffer()
    time.sleep(1.5)
    # Read until it arrives: on the network console (socket://) in_waiting is only 0 or 1.
    received = b""
    deadline = time.monotonic() + 1.0
    while b"NO CARRIER\r\n" not in received and time.monotonic() < deadline:
        received += console.ser.read(console.ser.in_waiting or 1)
    assert b"NO CARRIER" in received
    assert console.command("AT")[-1] == "OK"  # back in command mode


def test_connect_text_follows_amp_n(console):
    assert console.command("AT&N6")[-1] == "OK"  # 9600
    lines = console.command("AT*TESTCALL", until=("CONNECT 9600", "ERROR"))
    assert lines[-1] == "CONNECT 9600"


def test_test_call_argument_range(console):
    assert console.command("AT*TESTCALL=0")[-1] == "ERROR"
    assert console.command("AT*TESTCALL=X")[-1] == "ERROR"


@pytest.mark.parametrize("index", [0, 38])
def test_56k_connect_escape_and_resume(console, index):
    assert console.command(f"AT&N{index}")[-1] == "OK"
    lines = console.command("AT*TESTCALL", until=("CONNECT 56000", "ERROR"))
    assert lines[-1] == "CONNECT 56000"
    assert console.raw(b"56k echo", wait=0.4) == b"56k echo"
    _escape(console)
    lines = console.command("ATO", until=("CONNECT 56000", "ERROR"))
    assert lines[-1] == "CONNECT 56000"


def test_factory_line_speed_is_14400(console):
    assert console.command("AT&F1")[-1] == "OK"
    assert any("&N8" in line for line in console.command("ATI4"))
    _call(console)


def test_56k_numeric_connect(console):
    assert console.raw(b"AT&N38&U32V0\r").endswith(b"0\r")
    received = console.raw(b"AT*TESTCALL\r")
    assert received.endswith(b"232\r")


def test_baud_setting_is_kept_across_a_restart(console):
    try:
        assert console.command("AT*BAUD=9600")[-1] == "OK"
        assert console.command("AT*BAUD?")[0] == "9600 (fixed)"
        assert console.command("AT*BAUD=1234")[-1] == "ERROR"
        assert console.command("AT*BAUD=230400")[-1] == "ERROR"
        boot = console.restart()
        assert b"RS232: speed fixed 9600 (AT*BAUD)" in boot
        assert console.command("AT*BAUD?")[0] == "9600 (fixed)"
    finally:
        assert console.command("AT*BAUD=AUTO")[-1] == "OK"
    assert console.command("AT*BAUD?")[0].startswith("AUTO (from \"AT\"), now ")


def test_banner_shows_rs232_settings(console):
    boot = console.restart()
    assert b'RS232: speed from "AT" (starts at ' in boot
    assert b"&B1, flow control &H1&R2, &C1 &D2" in boot


def test_help_lists_the_new_commands(console):
    lines = console.command("AT*HELP")
    text = "\n".join(lines)
    assert "AT*BAUD=" in text
    assert "AT*TESTCALL" in text
    assert "AT*HWLED=AUTO" in text
    assert "AT*HWSIG=AUTO" in text


def test_led_and_line_hand_back(console):
    assert console.command("AT*HWLED=ALL")[-1] == "OK"
    assert console.command("AT*HWLED=AUTO")[-1] == "OK"
    lines = console.command("AT*HWSIG=DCD,ON")
    assert lines[-1] == "OK"
    assert any("paused until AT*HWSIG=AUTO" in line for line in lines)
    assert console.command("AT*HWSIG=AUTO")[-1] == "OK"


def test_line_tests_refused_during_a_call(console):
    _call(console)
    _escape(console)
    lines = console.command("AT*HWSIG=DCD,ON")
    assert lines[-1] == "ERROR"
    assert any("not available during a call" in line for line in lines)
    assert console.command("ATH")[-1] == "OK"
