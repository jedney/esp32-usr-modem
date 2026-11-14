"""Local configuration. No credential rotation, erases or external BBS calls."""
import os
import re
import socket
import time

import pytest


def require(condition, message):
    if not condition:
        raise AssertionError(message + " (traffic omitted for privacy)")


def read(port, seconds=0.5):
    data = bytearray()
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data.extend(port.ser.read(port.ser.in_waiting or 1))
    return bytes(data)


def send(port, data, seconds=0.5):
    port.ser.write(data)
    return read(port, seconds)


@pytest.fixture
def local(console):
    console.raw(b"\x1bq\r", wait=0.2)
    # A just-uploaded modem may still be in its bounded eight-second Wi-Fi boot wait.
    profile = "\n".join(console.command("ATI4", timeout=15))
    letters = dict(re.findall(r"(?<!\S)([EMVQ])(\d+)", profile))
    regs = dict(re.findall(r"S(\d{2})=(\d+)", profile))
    style = console.command("AT*STYLE?")[0]
    console.command("ATE1Q0V1M0S0=0S12=50")
    try:
        yield console
    finally:
        console.ser.write(b"\x03")
        read(console, 0.1)
        console.ser.write(b"q\r")
        read(console, 0.2)
        time.sleep(1.1)
        console.ser.write(b"+++")
        time.sleep(1.1)
        console.raw(b"ATH\r", wait=0.2)
        console.command("AT*STYLE=" + style)
        restore = "AT" + "".join(key + value for key, value in letters.items())
        restore += "S0=" + str(int(regs.get("00", "0")))
        restore += "S12=" + str(int(regs.get("12", "50")))
        console.raw(restore.encode() + b"\r", wait=0.2)


@pytest.mark.parametrize("style", ["ANSI", "CP437", "PLAIN"])
def test_direct_configuration_styles_and_quit(local, style):
    local.command("AT*STYLE=" + style)
    data = local.raw(b"AT*CONFIG\r")
    require(b"MODEM CONFIGURATION" in data and b"Phonebook" in data, "Missing root menu")
    positions = [data.find(label) for label in (b"About", b"Hardware Tests", b"Client Tests", b"Command Reference")]
    require(all(position >= 0 for position in positions) and positions == sorted(positions), "Wrong A-D menu order")
    require(b"CONNECT" not in data, "Direct configuration emitted CONNECT")
    if style == "PLAIN":
        require(b"\x1b" not in data, "Plain mode emitted terminal escapes")
    elif style == "CP437":
        require(b"\xc9\xcd" in data, "Missing serial CP437 line drawing")
    send(local, b"q\r")
    require(local.command("AT")[-1] == "OK", "Quit did not restore AT mode")


@pytest.mark.parametrize("style", ["ANSI", "CP437", "PLAIN"])
def test_idle_menu_does_not_repaint(local, style):
    local.command("AT*STYLE=" + style)
    data = local.raw(b"AT*CONFIG\r", wait=1)
    require(b"MODEM CONFIGURATION" in data, "Missing root menu")
    data = read(local, 6)
    require(not data.replace(b"\x1b[18t", b""), "Idle menu emitted display output")
    data = send(local, b"3\r", 1)
    require(b"Settings" in data, "Menu stopped responding to input")


def test_reserved_dial_escape_resume_and_hangup(local):
    data = local.raw(b"ATDT123\r")
    require(data.count(b"CONNECT") == 1 and b"MODEM CONFIGURATION" in data, "Reserved dial did not connect once")
    time.sleep(1.1)
    send(local, b"+++", 1.2)
    require(b"CONNECT" in local.raw(b"ATO\r"), "ATO did not resume")
    send(local, b"q\r")
    require(local.command("AT")[-1] == "OK", "Dialed menu did not release call slot")


def test_submenus_help_and_hardware_availability(local):
    local.raw(b"AT*CONFIG\r")
    data = send(local, b"3\r")
    require(b"Auto-answer rings" in data and b"destination" in data and b"Model" in data, "Missing settings")
    data = send(local, b"4\r")
    require(b"0 disables auto-answer" in data, "Missing setting help")
    send(local, b"\x1b")
    send(local, b"\x1b")
    data = send(local, b"B\r")
    require(b"Hardware Tests" in data and b"Guided tests" in data, "Missing hardware screen")
    data += read(local, 0.1)
    require(b"RS232 lines" in data and b"Loopback" in data and b"Bridge" in data, "Missing console port tests")


def test_menu_rejects_incoming_caller(local):
    status = "\n".join(local.command("AT*WIFI?"))
    match = re.search(r"Address:\s*(\d+\.\d+\.\d+\.\d+)", status)
    if not match:
        pytest.skip("modem needs a reachable Wi-Fi address")
    local.raw(b"AT*CONFIG\r")
    with socket.create_connection((match.group(1), 23), timeout=3) as peer:
        peer.settimeout(2)
        try:
            data = peer.recv(256)
        except ConnectionResetError:
            data = b""
        require(data == b"", "Caller was not rejected during configuration")
    require(b"RING" not in read(local, 0.2), "Incoming call interrupted configuration")


def test_configuration_commands_export_and_credentials(local):
    for command in ("AT*STATUS", "AT*LOG", "AT*CALLLOG", "AT*RESETINFO", "AT*SOUNDS", "AT*REFERENCE", "AT*ABOUT"):
        require(local.command(command, timeout=5)[-1] == "OK", "Configuration command failed")
    lines = local.command("AT*EXPORT", timeout=6)
    text = "\n".join(lines)
    require("AT*RESTOREEND=" in text and "AT*PROFILE=" in text, "Export lacks restore commands")
    require("AT*PASS" not in text and "AT*WIFIJOIN" not in text, "Export contains credential commands")
    for key in ("USR_MODEM_WIFI_PASS", "USR_MODEM_PASSWORD"):
        secret = os.environ.get(key, "")
        require(not secret or secret not in text, "Export disclosed a credential")


def test_restart_confirmation_can_be_cancelled(local):
    data = local.raw(b"AT*RESTART\r")
    require(b"Y/N" in data, "Restart did not ask for confirmation")
    data = send(local, b"N\r")
    require(b"ERROR" in data, "Restart cancellation did not finish")
    require(local.command("AT")[-1] == "OK", "Cancelled restart lost AT session")


def test_local_network_tools_and_sound_cancellation(local):
    status = "\n".join(local.command("AT*WIFI?"))
    if "Address:" not in status:
        pytest.skip("network tools require Wi-Fi")
    require(local.command("AT*DNS=127.0.0.1", timeout=12)[-1] == "OK", "DNS tool failed")
    require(local.command("AT*PING=127.0.0.1", timeout=12)[-1] == "OK", "Loopback ICMP tool failed")
    data = local.raw(b"AT*PLAY=SWEEP\r", wait=0.3)
    require(b"Playing" in data, "Sound preview did not start")
    data = send(local, b"x")
    require(b"ERROR" in data, "Sound preview did not cancel")
    require(local.command("AT")[-1] == "OK", "Cancelled preview lost AT session")


def test_rs232_hardware_takeover_tests_are_unavailable(rs232):
    previous_style = rs232.command("AT*STYLE?")[0]
    rs232.command("AT*STYLE=PLAIN")
    try:
        rs232.raw(b"AT*CONFIG\r")
        data = send(rs232, b"B\r", 1)
        require(b"unavailable: takes over RS232" in data, "RS232 test restriction is absent")
        data = send(rs232, b"C\r", 1)
        require(b"DTR=" not in data, "Unavailable line test took over RS232")
    finally:
        send(rs232, b"\x03", 0.3)
        rs232.command("AT*STYLE=" + previous_style)
