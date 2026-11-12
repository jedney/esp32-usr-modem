"""The USB modem port (v2.0): the S3's native USB, carried by the rear USB-C socket.

Run with --usb-port (the TinyUSB serial port, e.g. COM21). The cross-port tests also
need --rs232-port. No console port is needed: everything is driven from USB and RS232.
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


def read_for(port, duration):
    received = bytearray()
    deadline = time.monotonic() + duration
    while time.monotonic() < deadline:
        received.extend(port.ser.read(port.ser.in_waiting or 1))
    return bytes(received)


def wait_for(port, marker, timeout=8):
    received = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        received.extend(port.ser.read(port.ser.in_waiting or 1))
        if marker in received:
            return bytes(received)
    raise AssertionError("Expected serial event absent (traffic omitted for privacy)")


def hang_up(port):
    """Back to command mode with no call, whatever state the port was left in."""
    time.sleep(1.2)
    port.ser.write(b"+++")
    time.sleep(1.3)
    port.ser.write(b"ATH\r")
    time.sleep(0.3)
    port.ser.write(b"ATE1Q0V1\r")
    read_for(port, 0.3)


@pytest.fixture
def usb_idle(usb):
    hang_up(usb)
    try:
        yield usb
    finally:
        hang_up(usb)


def test_usb_port_answers_at_commands(usb_idle):
    usb = usb_idle
    assert usb.command("AT")[-1] == "OK"
    assert any("Sportster" in line for line in usb.command("ATI3"))
    status = "\n".join(usb.command("AT*STATUS"))
    require("USB: open (DTR on)" in status, "AT*STATUS does not show the open USB port")
    assert usb.command("AT*ANSWERPORT=?")[0] == "RS232,USB,CONSOLE,AUTO"


def test_usb_echo_call_carries_a_large_block_and_escapes(usb_idle):
    usb = usb_idle
    usb.ser.write(b"AT*TESTCALL\r")
    wait_for(usb, b"CONNECT")
    read_for(usb, 0.3)
    block = bytes((32 + i % 95) for i in range(16384))
    received = bytearray()
    for start in range(0, len(block), 1024):
        usb.ser.write(block[start:start + 1024])
        received.extend(usb.ser.read(usb.ser.in_waiting))
    deadline = time.monotonic() + 15
    while len(received) < len(block) and time.monotonic() < deadline:
        received.extend(usb.ser.read(usb.ser.in_waiting or 1))
    require(bytes(received) == block, "the echo call changed or lost data")
    time.sleep(1.2)
    usb.ser.write(b"+++")
    wait_for(usb, b"OK", timeout=4)
    assert usb.command("ATH")[-1] == "OK"


def test_usb_dtr_drop_hangs_up(usb_idle):
    usb = usb_idle
    assert usb.command("AT&D2")[-1] == "OK"
    usb.ser.write(b"AT*TESTCALL\r")
    wait_for(usb, b"CONNECT")
    usb.ser.dtr = False  # the PC program closes the port
    time.sleep(0.6)
    usb.ser.dtr = True
    time.sleep(0.3)
    read_for(usb, 0.3)
    # Still online, "AT" would come back as echoed data with no OK.
    assert usb.command("AT")[-1] == "OK"


def test_usb_1200_baud_and_line_toggles_do_not_reboot(usb_idle):
    """Arduino's USB serial class reboots into the bootloader on these; ours must not."""
    usb = usb_idle
    usb.ser.baudrate = 1200
    time.sleep(0.3)
    for dtr, rts in ((False, True), (True, True), (True, False), (False, False)):
        usb.ser.dtr = dtr
        usb.ser.rts = rts
        time.sleep(0.1)
    usb.ser.baudrate = 115200
    usb.ser.dtr = True
    time.sleep(0.5)
    # A reboot would have closed the port (re-enumeration) and lost this command.
    assert usb.command("AT")[-1] == "OK"
    status = "\n".join(usb.command("AT*STATUS"))
    require("USB: open (DTR on)" in status, "USB port not open after the toggles")


def test_usb_and_rs232_share_one_call(usb_idle, rs232):
    usb = usb_idle
    usb.ser.write(b"AT*TESTCALL\r")
    wait_for(usb, b"CONNECT")
    try:
        assert rs232.command("AT*TESTCALL")[-1] == "ERROR"
    finally:
        hang_up(usb)
    assert rs232.command("AT")[-1] == "OK"


@contextlib.contextmanager
def answer_setup(usb):
    """S0=1, quiet, AUTO answer port; restores what it changed."""
    profile = "\n".join(usb.command("ATI4"))
    s0 = re.search(r"S00=(\d+)", profile)
    m = re.search(r"(?<!\S)M(\d)", profile)
    before_port = usb.command("AT*ANSWERPORT?")[0]
    status = "\n".join(usb.command("AT*WIFI?"))
    match = re.search(r"Address:\s*(\d+\.\d+\.\d+\.\d+)", status)
    if not match:
        pytest.skip("incoming calls need the modem connected to the PC's network")
    assert usb.command("AT*ANSWERPORT=AUTO")[-1] == "OK"
    assert usb.command("ATM0S0=1")[-1] == "OK"
    try:
        yield match.group(1)
    finally:
        usb.command("AT*ANSWERPORT=" + before_port)
        usb.command("ATS0=%s" % (s0.group(1) if s0 else "0"))
        usb.command("ATM%s" % (m.group(1) if m else "1"))


def call_in(address):
    connection = socket.create_connection((address, 23), timeout=5)
    connection.settimeout(0.05)
    return connection


def test_auto_answer_goes_to_usb_when_only_usb_has_dtr(usb_idle, rs232):
    usb = usb_idle
    with answer_setup(usb) as address:
        rs232.ser.dtr = False
        time.sleep(0.3)
        connection = call_in(address)
        try:
            wait_for(usb, b"CONNECT")
        finally:
            connection.close()
        wait_for(usb, b"NO CARRIER")
        rs232.ser.dtr = True
        time.sleep(0.3)


def test_auto_answer_prefers_rs232_when_both_have_dtr(usb_idle, rs232):
    usb = usb_idle
    with answer_setup(usb) as address:
        rs232.ser.dtr = True
        time.sleep(0.3)
        read_for(rs232, 0.2)
        connection = call_in(address)
        try:
            wait_for(rs232, b"CONNECT")
            require(b"CONNECT" not in read_for(usb, 0.3), "USB also answered")
        finally:
            connection.close()
        wait_for(rs232, b"NO CARRIER")


def test_usb_unplug_during_a_call_frees_the_modem(usb_idle, request):
    """Manual: pull the rear USB-C cable when asked, then plug it back in (--usb-unplug)."""
    if not request.config.getoption("--usb-unplug"):
        pytest.skip("--usb-unplug not enabled (needs a person at the cable)")
    from serial.tools import list_ports

    def present(name):
        return any(p.device == name for p in list_ports.comports())

    usb = usb_idle
    name = usb.ser.port
    usb.ser.write(b"AT*TESTCALL\r")
    wait_for(usb, b"CONNECT")
    print("\n*** Unplug the USB cable (rear socket) within 30 s, then plug it back in. ***")
    deadline = time.monotonic() + 30
    while time.monotonic() < deadline and present(name):
        time.sleep(0.5)
    require(not present(name), "The USB cable was not unplugged in time")
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline and not present(name):
        time.sleep(0.5)
    require(present(name), "The USB cable was not plugged back in in time")
    time.sleep(3)
    usb.ser.close()
    usb.ser.open()
    usb.ser.dtr = True
    time.sleep(0.5)
    hang_up(usb)
    # The call must be gone: a new one connects, which needs the line to be free.
    usb.ser.write(b"AT*TESTCALL\r")
    wait_for(usb, b"CONNECT")
    time.sleep(1.2)
    usb.ser.write(b"+++")
    wait_for(usb, b"OK", timeout=4)
    assert usb.command("ATH")[-1] == "OK"
