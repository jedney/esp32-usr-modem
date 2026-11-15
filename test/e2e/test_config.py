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


@pytest.mark.parametrize("style", ["ANSI", "PLAIN"])
def test_direct_configuration_styles_and_quit(local, style):
    local.command("AT*STYLE=" + style)
    data = local.raw(b"AT*CONFIG\r")
    require(b"Configuration" in data and b"Phone Book" in data, "Missing root menu")
    positions = [data.find(label) for label in (b"Status", b"Phone Book", b"Call History", b"Settings", b"Help")]
    require(all(position >= 0 for position in positions) and positions == sorted(positions), "Wrong root menu order")
    require(b"CONNECT" not in data, "Direct configuration emitted CONNECT")
    if style == "PLAIN":
        require(b"\x1b" not in data, "Plain mode emitted terminal escapes")
    else:
        require(not re.sub(rb"\x1b\[[0-9;]*m", b"", data).count(b"\x1b"), "Menu emitted non-colour escapes")
    send(local, b"q\r")
    require(local.command("AT")[-1] == "OK", "Quit did not restore AT mode")


@pytest.mark.parametrize("style", ["ANSI", "PLAIN"])
def test_idle_menu_does_not_repaint(local, style):
    local.command("AT*STYLE=" + style)
    data = local.raw(b"AT*CONFIG\r", wait=1)
    require(b"Configuration" in data, "Missing root menu")
    data = read(local, 6)
    require(not data, "Idle menu emitted display output")
    data = send(local, b"d\r", 1)
    require(b"Settings" in data, "Menu stopped responding to input")


def test_reserved_dial_escape_resume_and_hangup(local):
    data = local.raw(b"ATDT123\r")
    require(data.count(b"CONNECT") == 1 and b"Configuration" in data, "Reserved dial did not connect once")
    time.sleep(1.1)
    send(local, b"+++", 1.2)
    require(b"CONNECT" in local.raw(b"ATO\r"), "ATO did not resume")
    send(local, b"q\r")
    require(local.command("AT")[-1] == "OK", "Dialed menu did not release call slot")


def test_compact_submenus_field_units_and_hardware_availability(local):
    local.raw(b"AT*CONFIG\r")
    data = send(local, b"da\r")
    require(b"Rings before answering" in data and b"Answer incoming calls on" in data and b"Modem model" in data, "Missing settings")
    require(b"Choose how many rings" not in data and b"Range:" not in data,
            "Menu list includes field help or ranges")
    data = send(local, b"d\r")
    require(b"rings" in data and b"Range: 0-255" in data, "Missing field range or units")
    require(b"Choose how many rings" not in data, "Terminal form includes menu help")
    send(local, b"\x1b")
    send(local, b"\x1b")
    data = send(local, b"xede\r")
    require(b"Hardware Tests" in data and b"Guided tests" in data, "Missing hardware screen")
    data += read(local, 0.1)
    require(b"RS232 control lines" in data and b"loopback" in data and b"bridge" in data, "Missing console port tests")


@pytest.mark.parametrize("style", ["ANSI", "PLAIN"])
def test_letter_tour_and_masked_entry_at_multiple_widths(local, style):
    original_size = local.command("AT*SIZE?")[0]
    local.command("AT*STYLE=" + style)
    try:
        for width in (20, 40, 80, 132):
            local.command(f"AT*SIZE={width},25")
            root = local.raw(b"AT*CONFIG\r")
            require(b"A. Status" in root and b"E. Help" in root, "Incomplete letter menu")
            for path, title in ((b"b", b"Phone Book"), (b"da", b"General Settings"), (b"db", b"Sounds"),
                                (b"ed", b"Diagnostics"), (b"dc", b"Wi-Fi"), (b"dd", b"Clock"),
                                (b"de", b"Updates"), (b"df", b"Reset"), (b"ede", b"Hardware Tests"), (b"edf", b"Client Tests")):
                require(title in send(local, path, 0.2), "Letter tour did not reach a section")
                send(local, b"x" * len(path), 0.2)
            send(local, b"dac", 0.2)
            choices = send(local, b"\x0c", 0.2)
            require(b"A. US Robotics Sportster 14.4k" in choices, "Friendly model choices missing")
            send(local, b"\x1b", 0.2)
            send(local, b"xed", 0.2)
            masked = send(local, b"MenuCanary123", 0.2)
            require(b"MenuCanary123" not in masked and b"***" in masked, "Secret entry was not masked")
            cancelled = send(local, b"\x1b", 0.2)
            require(b"MenuCanary123" not in cancelled, "Cancelled secret leaked")
            send(local, b"\x03", 0.2)
    finally:
        local.ser.write(b"\x03")
        read(local, 0.2)
        local.command("AT*SIZE=" + original_size)


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


def test_compact_about_and_project_repository(local):
    lines = local.command('AT*ABOUT')
    text = '\n'.join(lines)
    require(len(text) < 3500, 'About is too long')
    require('Zimodem: https://' in text and 'xterm.js: https://' in text, 'Project links missing')
    require('Full credits and licences: credits/LICENSES.md' in text, 'Full credits pointer missing')
    require(lines[-2] == 'https://github.com/jedney/esp32-usr-modem', 'About does not close with project URL')
    require(local.command('AT*GITHUB')[-2] == lines[-2], 'GitHub command differs from About')


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
        data = send(rs232, b"ede\r", 1)
        require(b"unavailable: takes over RS232" in data, "RS232 test restriction is absent")
        data = send(rs232, b"l\r", 1)
        require(b"DTR=" not in data, "Unavailable line test took over RS232")
    finally:
        send(rs232, b"\x03", 0.3)
        rs232.command("AT*STYLE=" + previous_style)


def test_usb_modem_letter_menu(usb):
    previous_style = usb.command("AT*STYLE?")[0]
    try:
        for style in ("PLAIN", "ANSI"):
            usb.command("AT*STYLE=" + style)
            root = usb.raw(b"AT*CONFIG\r")
            require(b"A. Status" in root and b"E. Help" in root, "USB menu missing")
            choices = send(usb, b"dac")
            require(b"A. US Robotics Sportster 14.4k" in choices, "USB letter navigation failed")
            send(usb, b"\x03")
            require(usb.command("AT")[-1] == "OK", "USB menu did not release the port")
    finally:
        send(usb, b"\x03")
        usb.command("AT*STYLE=" + previous_style)
