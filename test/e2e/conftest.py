"""End-to-end test fixtures.

These tests talk to a real modem over serial ports:
  --console-port   the USB maintenance console (UART0, 115200), e.g. COM5
  --rs232-port     the RS232 port through the Unitek adapter, e.g. COM7 (later versions)

Tests that need a port that was not given are skipped, so the suite is safe to run
anywhere.

Wi-Fi tests (v0.6) need the network name and password. They come from a `.env` file at
the repository root (git-ignored; see `.env.example`) or from environment variables of
the same names, which win. They are never printed.
"""

import os
import pathlib
import re
import time

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

# Unsolicited console lines (v0.6): Wi-Fi connected/lost and clock set. Not part of a
# command's response, so command() leaves them out.
NOTICE = re.compile(r"^(Wi-Fi: (connected to|lost|can't join) |Clock: set by NTP)")


def load_env(path=REPO_ROOT / ".env"):
    """Reads KEY=VALUE lines into os.environ (existing variables are kept)."""
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if key.startswith("export "):
            key = key[len("export ") :].strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        os.environ.setdefault(key, value)


load_env()

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None


def pytest_addoption(parser):
    parser.addoption("--web-tests", action="store_true", help="test authenticated web interface")
    parser.addoption("--browser-upload", action="store_true", help="install tested firmware through HTTP")
    parser.addoption("--web-host", default="usr-modem.local", help="modem HTTP hostname or IPv4")
    parser.addoption("--call-server-ip", default=None, help="PC LAN address reachable from modem")
    parser.addoption("--call-server-port", default=32321, type=int, help="local call test TCP port")
    parser.addoption("--live-bbs", action="store_true", help="allow a live outgoing BBS call")
    parser.addoption("--console-port", default=None, help="USB console serial port")
    parser.addoption("--rs232-port", default=None, help="RS232 serial port (Unitek adapter)")
    parser.addoption("--rs232-baud", default=38400, type=int, help="RS232 host speed")
    parser.addoption("--device-tests", action="store_true", help="allow device password changes")
    parser.addoption("--factory-reset", action="store_true", help="allow erasing all modem settings")
    parser.addoption("--ota-upload", action="store_true", help="allow real OTA uploads (tests first)")
    parser.addoption("--save-ota-password", action="store_true", help="save final OTA password in .env")


class Port:
    """Line-oriented helper around a pyserial port."""

    def __init__(self, ser):
        self.ser = ser

    def command(self, text, timeout=3.0, until=("OK", "ERROR")):
        """Send an AT command and return the response lines up to OK/ERROR."""
        self.ser.reset_input_buffer()
        self.ser.write(text.encode("ascii") + b"\r")
        lines = []
        deadline = time.monotonic() + timeout
        buffer = b""
        while time.monotonic() < deadline:
            buffer += self.ser.read(self.ser.in_waiting or 1)
            while b"\n" in buffer:
                raw, buffer = buffer.split(b"\n", 1)
                line = raw.decode("ascii", "replace").strip()
                if line and line != text and not NOTICE.match(line):
                    lines.append(line)
                    if line in until:
                        return lines
        # A sensitive command or an unsolicited first-boot banner may contain secrets.
        raise AssertionError("No final AT command result (response omitted for privacy)")

    def raw(self, data, wait=0.6):
        """Send bytes as they are and return every byte received within `wait` seconds."""
        self.ser.reset_input_buffer()
        self.ser.write(data)
        received = b""
        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            received += self.ser.read(self.ser.in_waiting or 1)
        return received

    def restart(self, banner=b"USR Modem Emulator v", timeout=15.0):
        """Reset the ESP32 through the USB-serial chip's RTS line (EN), as esptool does,
        and return the boot output once the banner has been printed. With a Wi-Fi
        network saved, the banner waits up to 8 s for it (v0.6)."""
        self.ser.dtr = False  # IO0 high: normal boot, not the bootloader
        self.ser.rts = True  # EN low: reset
        time.sleep(0.1)
        self.ser.reset_input_buffer()
        self.ser.rts = False
        received = b""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            received += self.ser.read(self.ser.in_waiting or 1)
            if banner in received and received.rstrip().endswith(b"hardware."):
                return received
        raise AssertionError("No complete boot banner after reset (output omitted for privacy)")


def _open(request, option, baud):
    port_name = request.config.getoption(option)
    if not port_name:
        pytest.skip(f"{option} not given")
    if serial is None:
        pytest.skip("pyserial not installed (pip install -r test/e2e/requirements.txt)")
    ser = serial.Serial(port_name, baud, timeout=0.1)
    time.sleep(0.2)
    return ser


@pytest.fixture
def console(request):
    ser = _open(request, "--console-port", 115200)
    yield Port(ser)
    ser.close()


@pytest.fixture
def rs232(request):
    ser = _open(request, "--rs232-port", request.config.getoption("--rs232-baud"))
    ser.rtscts = True
    yield Port(ser)
    ser.close()


@pytest.fixture
def wifi():
    """(name, password) of the Wi-Fi network for the tests, from .env or the environment."""
    ssid = os.environ.get("USR_MODEM_WIFI_SSID", "")
    password = os.environ.get("USR_MODEM_WIFI_PASS", "")
    if not ssid:
        pytest.skip("USR_MODEM_WIFI_SSID not set (.env or environment)")
    return ssid, password
