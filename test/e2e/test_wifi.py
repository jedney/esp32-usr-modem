"""End-to-end checks of Wi-Fi, the mDNS name and the clock (v0.6, brief 6.6, 6.7).

They need the USB console and the Wi-Fi name and password in `.env` (or environment
variables) as USR_MODEM_WIFI_SSID / USR_MODEM_WIFI_PASS; without them they are skipped.
The password is never printed: checks that involve it assert on a plain True/False.

Joining saves the network on the modem (the same one each run).
"""

import datetime
import re
import subprocess
import sys
import time

import pytest

PROMPT = b"Password (Enter for none): "


def _read_until(ser, markers, timeout):
    """Bytes from `ser` until one of `markers` has arrived or `timeout` seconds pass."""
    data = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data += ser.read(ser.in_waiting or 1)
        if any(m in data for m in markers):
            # Let the rest of the line arrive.
            end = time.monotonic() + 0.2
            while time.monotonic() < end:
                data += ser.read(ser.in_waiting or 1)
            return data
    return data


def _absent(text, secret):
    """True if `secret` is not in `text` (bytes or str). Never shows either value."""
    if isinstance(text, bytes):
        text = text.decode("latin-1")
    return secret not in text


def _wait_connected(console, timeout=25):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        lines = console.command("AT*WIFI?")
        if any(re.match(r"Wi-Fi:\s+connected$", line) for line in lines):
            return lines
        time.sleep(1)
    pytest.fail("Wi-Fi not connected")


def _join(console, ssid, password):
    """AT*WIFIJOIN with the password typed at the masked prompt. Returns all output."""
    console.ser.reset_input_buffer()
    console.ser.write(f'AT*WIFIJOIN="{ssid}"\r'.encode("utf-8"))
    before = _read_until(console.ser, [PROMPT, b"ERROR"], 5)
    assert PROMPT in before, "no password prompt"
    console.ser.write(password.encode("utf-8") + b"\r")
    after = _read_until(console.ser, [b"\r\nOK\r\n", b"\r\nERROR\r\n"], 30)
    return before, after


def test_join_with_masked_password(console, wifi):
    ssid, password = wifi
    if '"' in ssid:
        pytest.skip("network name contains a quote")
    lines = console.command("AT*WIFISCAN", timeout=15)
    assert lines[-1] == "OK"
    # Joining replaces the saved network. If the modem is on another network, a wrong
    # .env password would lose that one for good, so refuse instead.
    saved = [x.split(":", 1)[1].strip() for x in console.command("AT*WIFI?")
             if x.startswith("Network:")]
    if saved and saved[0] not in ("", "(none)", ssid):
        pytest.skip("the modem has another network saved; AT*WIFIFORGET first to run this test")
    before, after = _join(console, ssid, password)
    echoed = after.split(b"\r", 1)[0]
    masked = echoed == b"*" * len(password)  # boolean: a failed compare must not print it
    assert masked, "the password was not echoed as stars"
    connected = b"Connected, IP " in after and after.rstrip().endswith(b"OK")
    assert connected, "the join did not connect"
    ok = _absent(before + after, password) if password else True
    assert ok, "the password appeared in the output"


def test_status_shows_the_network_but_not_the_password(console, wifi):
    ssid, password = wifi
    lines = _wait_connected(console)
    text = "\n".join(lines)
    assert f"Network:  {ssid}" in text
    assert ("Password: set" if password else "Password: not set") in text
    assert re.search(r"Address:\s+\d+\.\d+\.\d+\.\d+ \((DHCP|static)\)", text)
    assert re.search(r"Signal:\s+-\d+ dBm, channel \d+", text)
    ok = _absent(text, password) if password else True
    assert ok, "the password appeared in AT*WIFI?"


def test_mdns_name_answers_ping(console, wifi):
    _wait_connected(console)
    lines = console.command("AT*HOSTNAME?")
    match = re.match(r"(\S+) \((\S+\.local)\)", lines[0])
    assert match, lines
    name = match.group(2)
    count = ["-n", "2", "-w", "3000"] if sys.platform == "win32" else ["-c", "2", "-W", "3"]
    for _ in range(3):  # mDNS can take a moment after joining
        # stdin from DEVNULL: without a console, Windows can't hand ping an input handle.
        result = subprocess.run(
            ["ping", *count, name], stdin=subprocess.DEVNULL, capture_output=True, timeout=30
        )
        if result.returncode == 0:
            return
        time.sleep(2)
    pytest.fail(f"ping {name} failed: {result.stdout.decode('latin-1', 'replace')[-300:]}")


def test_clock_is_set_by_ntp_and_close_to_the_pc(console, wifi):
    _wait_connected(console)
    deadline = time.monotonic() + 40
    while True:
        lines = console.command("AT*TIME")
        match = re.match(
            r"(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d):(\d\d) \S+ \(UTC([+-])(\d\d):(\d\d)\)", lines[0]
        )
        if match or time.monotonic() > deadline:
            break
        time.sleep(2)
    assert match, f"clock not set: {lines!r}"
    y, mo, d, h, mi, s = (int(match.group(i)) for i in range(1, 7))
    sign = 1 if match.group(7) == "+" else -1
    offset = datetime.timedelta(hours=int(match.group(8)), minutes=int(match.group(9))) * sign
    local = datetime.datetime(y, mo, d, h, mi, s, tzinfo=datetime.timezone(offset))
    now = datetime.datetime.now(datetime.timezone.utc)
    assert abs((local - now).total_seconds()) < 120


def test_time_zone_change_and_back(console):
    original = console.command("AT*TZ?")[0].split(" (")[0]
    try:
        assert console.command("AT*TZ=utc")[-1] == "OK"
        assert console.command("AT*TZ?")[0].startswith("UTC (UTC0)")
        lines = console.command("AT*TIME")
        assert lines[0].startswith("Clock not set") or "(UTC+00:00)" in lines[0]
        assert any("Pacific/Auckland" in line for line in console.command("AT*TZ=?"))
    finally:
        assert console.command(f"AT*TZ={original}")[-1] == "OK"


@pytest.mark.parametrize(
    "command",
    [
        "AT*WIFIIP=1.2.3.4,255.255.255.0,9.9.9.9",
        "AT*WIFIIP=1.2.3.4",
        "AT*HOSTNAME=bad_name",
        "AT*NTP=bad server",
        "AT*TZ=Nowhere/Special",
        "AT*WIFI=MAYBE",
        "AT*TIME=12:00",
    ],
)
def test_bad_settings_are_refused(console, command):
    assert console.command(command)[-1] == "ERROR"


def test_help_lists_the_wifi_and_clock_commands(console):
    text = "\n".join(console.command("AT*HELP"))
    for name in ("AT*WIFI", "AT*WIFISCAN", "AT*WIFIJOIN", "AT*WIFIFORGET", "AT*WIFIIP",
                 "AT*HOSTNAME", "AT*TIME", "AT*TZ", "AT*NTP"):
        assert name in text


# Commands that only show something, safe to run in any state.
SHOW_COMMANDS = [
    "AT*HELP", "AT*VER", "AT*WIFI?", "AT*WIFIIP?", "AT*HOSTNAME?", "AT*TIME", "AT*TZ?",
    "AT*TZ=?", "AT*NTP?", "AT*BAUD?", "AT*HW?", "AT*HWINFO", "AT*HWPINS",
    "ATI0", "ATI1", "ATI2", "ATI3", "ATI4", "ATI5", "ATI6", "ATI7", "AT&V",
]


def test_password_never_shown(console, wifi):
    """Brief 6.6 / Q46: the Wi-Fi password is never displayed, after joining or at boot."""
    ssid, password = wifi
    if not password:
        pytest.skip("open network: no password to look for")
    _wait_connected(console)
    output = console.restart().decode("latin-1")
    assert "Wi-Fi: " in output
    for command in SHOW_COMMANDS:
        try:
            output += "\n".join(console.command(command, timeout=5))
        except AssertionError:
            pytest.fail(f"{command} gave no result")  # without the output
    output += "\n".join(console.command("AT*WIFISCAN", timeout=15))
    ok = _absent(output, password)
    assert ok, "the Wi-Fi password appeared in the output"


def test_wifi_password_prompt_owns_operation_across_ports(console, rs232):
    """No password is entered and no network change is attempted."""
    for port in (console, rs232):
        port.command('ATE0Q0V1')
    try:
        for owner, other in ((console, rs232), (rs232, console)):
            owner.ser.reset_input_buffer()
            owner.ser.write(b'AT*WIFIJOIN="review-prompt-only"\r')
            assert PROMPT in _read_until(owner.ser, (PROMPT,), 3)
            try:
                assert 'ERROR' in other.command('AT*WIFIJOIN="other-prompt-only"')
                assert 'ERROR' in other.command('AT*WIFISCAN')
            finally:
                owner.ser.write(b'\x1b')
                assert b'ERROR' in _read_until(owner.ser, (b'ERROR',), 3)
            assert 'OK' in owner.command('AT')
    finally:
        for port in (console, rs232):
            port.command('ATE1')  # the factory setting the other tests expect
