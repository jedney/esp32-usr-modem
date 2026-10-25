"""v0.7 password/OTA/reset checks. Maintenance tests require explicit flags.

No assertion prints a password, raw boot banner or upload output. Do not use pytest
--showlocals: local variables necessarily hold credentials during these tests.
"""

import os
import pathlib
import re
import secrets
import shutil
import socket
import subprocess
import time

import pytest

from conftest import REPO_ROOT
from test_wifi import _join, _read_until, _wait_connected

ALPHABET = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz"


def _ok(console, command):
    ok = console.command(command)[-1] == "OK"
    assert ok, "device command failed (command and output omitted)"


def _boot_password(banner):
    match = re.search(rb"Device password: ([^\r\n]+)", banner)
    return match.group(1).decode("ascii") if match else None


def _save_password(password):
    path = REPO_ROOT / ".env"
    text = path.read_text(encoding="utf-8") if path.exists() else ""
    row = f"USR_MODEM_OTA_PASSWORD='{password}'"
    if re.search(r"^USR_MODEM_OTA_PASSWORD=", text, re.M):
        text = re.sub(r"^USR_MODEM_OTA_PASSWORD=.*$", lambda _: row, text, flags=re.M)
    else:
        text = text.rstrip() + "\n" + row + "\n"
    path.write_text(text, encoding="utf-8", newline="\n")


@pytest.fixture
def managed_device(console, request):
    if not request.config.getoption("--device-tests"):
        pytest.skip("password changes need --device-tests")
    password = getattr(request.config, "_device_test_password", None)
    if password is None:
        candidate = os.environ.get("USR_MODEM_OTA_PASSWORD", "")
        password = candidate if 8 <= len(candidate) <= 32 and all(
            32 <= ord(c) <= 126 for c in candidate
        ) else "".join(secrets.choice(ALPHABET) for _ in range(12))
        request.config._device_test_password = password
    _ok(console, "ATE0Q0V1")
    _ok(console, "AT*PASS=" + password)
    _ok(console, "AT*OTA=ALWAYS")
    try:
        yield console, password
    finally:
        # Leave the modem usable with the configured credential after maintenance.
        _ok(console, "ATE0Q0V1")
        _ok(console, "AT*PASS=" + password)
        _ok(console, "AT*OTA=ALWAYS")
        _ok(console, "ATE1")
        if request.config.getoption("--save-ota-password"):
            _save_password(password)


def _upload(password, host):
    pio = shutil.which("pio")
    if not pio:
        pio = str(pathlib.Path.home() / ".platformio" / "penv" / "Scripts" / "pio.exe")
    env = os.environ.copy()
    env["USR_MODEM_OTA_PASSWORD"] = password
    # Every attempt goes through the normal upload gate, including expected failures.
    env.pop("USR_MODEM_SKIP_TESTS", None)
    result = subprocess.run(
        [pio, "run", "-e", "s3_ota", "-t", "upload", "--upload-port", host],
        cwd=REPO_ROOT, env=env, stdin=subprocess.DEVNULL,
        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300,
    )
    # Do not include the uploader output even when it fails.
    return result.returncode == 0


def _host(console):
    return console.command("AT*HOSTNAME?")[0].split()[0] + ".local"


def _listening(host):
    # An invitation elicits a challenge from an open listener without authenticating,
    # starting an update or touching credentials.
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.settimeout(1)
        # A valid invitation reaches the auth challenge but never answers it.
        sock.sendto(b"0 43210 100 00000000000000000000000000000000\n", (host, 3232))
        try:
            data, _ = sock.recvfrom(256)
            return data.startswith(b"AUTH ")
        except socket.timeout:
            return False


def test_help_lists_device_commands(console):
    text = "\n".join(console.command("AT*HELP"))
    assert all(name in text for name in ("AT*PASS", "AT*OTA", "AT*FACTORY"))


def test_ota_status_has_no_credentials(console):
    lines = console.command("AT*OTA?")
    assert lines[-1] == "OK"
    assert lines[0].startswith("OTA: ")
    assert not any("Device password:" in line for line in lines)


def test_pass_masks_inline_input_and_refuses_repeat(managed_device):
    console, _ = managed_device
    _ok(console, "ATE1")
    replacement = "".join(secrets.choice(ALPHABET) for _ in range(12))
    output = console.raw(("AT*PASS=" + replacement + "\r").encode())
    absent = replacement.encode() not in output
    assert absent, "inline password leaked"
    boundary_correct = output.startswith(b"AT*PASS=" + b"*" * len(replacement) + b"\r")
    assert boundary_correct, "password masking must start after the visible equals sign"
    assert output.rstrip().endswith(b"OK")
    repeat = console.raw(b"A/")
    assert repeat.rstrip().endswith(b"ERROR")
    hidden = _boot_password(console.restart()) is None
    assert hidden, "changed password was shown at boot"


def test_pass_random_shown_once_and_invalid_input_rejected(managed_device):
    console, _ = managed_device
    data = console.raw(b"AT*PASS\r")
    password = _boot_password(data)
    valid = password is not None and len(password) == 12 and all(c in ALPHABET for c in password)
    assert valid, "random replacement has invalid format"
    for command in ("AT*PASS=short", "AT*PASS=" + "x" * 33, "AT*PASS?"):
        rejected = console.command(command)[-1] == "ERROR"
        assert rejected, "invalid password command accepted"
    hidden = _boot_password(console.restart()) is None
    assert hidden, "replacement password was retained for boot display"


def test_demand_window_and_policy_restart(managed_device, wifi):
    console, _ = managed_device
    _wait_connected(console)
    host = _host(console)
    _ok(console, "AT*OTA=DEMAND")
    time.sleep(0.2)
    assert not _listening(host), "OTA listener remained open in demand mode"
    _ok(console, "AT*OTA=ON")
    time.sleep(0.2)
    assert _listening(host), "OTA window did not open"
    _ok(console, "AT*OTA=OFF")
    time.sleep(0.2)
    assert not _listening(host), "OTA window did not close"
    _ok(console, "AT*OTA=ON")
    console.restart()
    text = "\n".join(console.command("AT*OTA?"))
    assert "DEMAND, closed" in text and "Window: 0 seconds" in text
    _wait_connected(console)
    assert not _listening(host), "OTA window survived reboot"


def test_factory_cancel_leaves_settings_and_password_intact(managed_device):
    console, _ = managed_device
    _ok(console, "ATS7=47&W")
    console.ser.write(b"AT*FACTORY\r")
    prompt = _read_until(console.ser, [b"Y/N (Enter): "], 3)
    assert b"Y/N (Enter): " in prompt
    console.ser.write(b"N\r")
    response = _read_until(console.ser, [b"ERROR"], 3)
    assert b"cancelled" in response and b"ERROR" in response
    assert "047" in console.command("ATS7?")
    _ok(console, "ATS7=60&W")


def test_factory_new_password_and_authenticated_first_use(managed_device, wifi, request):
    if not request.config.getoption("--factory-reset"):
        pytest.skip("destructive reset needs --factory-reset")
    if not request.config.getoption("--ota-upload"):
        pytest.skip("authenticated upload needs --ota-upload")
    console, _ = managed_device
    ssid, wifi_password = wifi
    if '"' in ssid:
        pytest.skip("network name contains a quote")
    try:
        _ok(console, "ATS7=47&W")
        _ok(console, "AT&Z3=5551234")
        console.ser.write(b"AT*FACTORY\r")
        prompt = _read_until(console.ser, [b"Y/N (Enter): "], 3)
        assert b"Y/N (Enter): " in prompt
        console.ser.write(b"Y\r")
        boot = _read_until(console.ser, [b"AT*HWTEST tests the hardware.\r\n"], 20)
        first = _boot_password(boot)
        valid = first is not None and len(first) == 12 and all(c in ALPHABET for c in first)
        assert valid, "factory reset did not generate a new password"
        assert b"Wi-Fi: not set up" in boot
        assert "060" in console.command("ATS7?")
        numbers = console.command("AT&Z3?")
        assert numbers == ["OK"]
        same = first == _boot_password(console.restart())
        assert same, "unused first-boot password changed after restart"
        before, after = _join(console, ssid, wifi_password)
        joined = b"Connected, IP " in after and wifi_password.encode() not in before + after
        assert joined, "Wi-Fi restore failed or leaked its password"
        host = _host(console)
        wrong = _upload("Wrong-" + first, host)
        assert not wrong, "OTA accepted a wrong password"
        still_pending = first == _boot_password(console.restart())
        assert still_pending, "failed authentication consumed the first-boot password"
        _wait_connected(console)
        uploaded = _upload(first, host)
        assert uploaded, "authenticated OTA upload failed (output omitted)"
        hidden = _boot_password(console.restart()) is None
        assert hidden, "authenticated OTA did not remove the first-boot password"
        # Rotate and prove the live listener immediately rejects the old password.
        changed = "".join(secrets.choice(ALPHABET) for _ in range(12))
        _ok(console, "AT*PASS=" + changed)
        assert not _upload(first, host), "old password still authenticates after rotation"
        assert _upload(changed, host), "new password cannot upload after rotation"
    finally:
        # Factory reset clears Wi-Fi; restore it even if a later assertion fails.
        lines = console.command("AT*WIFI?")
        if not any(re.match(r"Wi-Fi:\s+connected$", line) for line in lines):
            _, after = _join(console, ssid, wifi_password)
            joined = b"Connected, IP " in after
            assert joined, "Wi-Fi restoration failed"
