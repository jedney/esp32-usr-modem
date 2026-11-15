"""End-to-end test fixtures.

These tests talk to a real modem over serial ports:
  --console-port   the USB maintenance console (UART0, 115200), e.g. COM5
  --rs232-port     the RS232 port through the Unitek adapter, e.g. COM18
  --usb-port       the USB modem port (native USB / rear USB-C), e.g. COM21

--console-port also takes the network console, socket://usr-modem.local:2001. The fixture
logs in with USR_MODEM_PASSWORD (from `.env` or the environment; never printed) and logs
in again after a test restarts the modem.

Tests that need a port that was not given are skipped, so the suite is safe to run
anywhere.

Wi-Fi tests need the network name and password. They come from a `.env` file at
the repository root (git-ignored; see `.env.example`) or from environment variables of
the same names, which win. They are never printed.
"""

import os
import pathlib
import re
import time

import pytest

REPO_ROOT = pathlib.Path(__file__).resolve().parents[2]

# Unsolicited console lines: Wi-Fi connected/lost, clock set and logins. Not part of a
# command's response, so command() leaves them out.
NOTICE = re.compile(r"^(Wi-Fi: (connected to|lost|can't join) |Clock: set by NTP|Login: )")


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


def pytest_configure(config):
    # pytest's long tracebacks (its default) print the arguments of every frame, and the
    # fixtures and helpers here take the Wi-Fi and device passwords as arguments. Short
    # tracebacks and no local variables, whatever the command line asks for.
    if config.option.tbstyle in ("auto", "long"):
        config.option.tbstyle = "short"
    config.option.showlocals = False


def pytest_addoption(parser):
    parser.addoption("--web-tests", action="store_true", help="test authenticated web interface")
    parser.addoption("--browser-upload", action="store_true", help="install tested firmware through HTTP")
    parser.addoption("--flood-tests", action="store_true",
                     help="browser limit and idle connection floods on ports 23 and 80")
    parser.addoption("--upload-tests", action="store_true",
                     help="interrupted web upload (writes nothing that can be installed)")
    parser.addoption("--usb-unplug", action="store_true",
                     help="USB unplug during a call (a person pulls the cable when asked)")
    parser.addoption("--web-host", default="usr-modem.local", help="modem HTTP hostname or IPv4")
    parser.addoption("--call-server-ip", default=None, help="PC LAN address reachable from modem")
    parser.addoption("--call-server-port", default=32321, type=int, help="local call test TCP port")
    parser.addoption("--live-internet", action="store_true",
                     help="allow tests that need the Internet (a real DNS lookup)")
    parser.addoption("--live-bbs", action="store_true", help="allow a live outgoing BBS call")
    parser.addoption("--udp-tests", action="store_true", help="PC UDP 32322 is already reachable for Internet echo acceptance")
    parser.addoption("--console-port", default=None,
                     help="USB console serial port, or socket://<host>:2001 (network console)")
    parser.addoption("--net-console", default=None, help="modem host or IPv4 for test_net_console.py")
    parser.addoption("--rs232-port", default=None, help="RS232 serial port (Unitek adapter)")
    parser.addoption("--rs232-baud", default=38400, type=int, help="RS232 host speed")
    parser.addoption("--usb-port", default=None, help="USB modem port (TinyUSB serial)")
    parser.addoption("--device-tests", action="store_true", help="allow device password changes")
    parser.addoption("--factory-reset", action="store_true", help="allow erasing all modem settings")
    parser.addoption("--ota-upload", action="store_true", help="allow real OTA uploads (tests first)")
    parser.addoption("--save-password", action="store_true",
                     help="save final device password (OTA and web) in .env")


def _read_until(ser, marker, timeout):
    """Reads until `marker` has arrived; returns everything read, or None on time-out."""
    received = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        received += ser.read(ser.in_waiting or 1)
        if marker in received:
            return received
    return None


def _read_quiet(ser, quiet, timeout):
    """Reads until nothing has arrived for `quiet` seconds (or `timeout` in all)."""
    received = b""
    deadline = time.monotonic() + timeout
    last = time.monotonic()
    while time.monotonic() < deadline and time.monotonic() - last < quiet:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            received += chunk
            last = time.monotonic()
    return received


NET_PROMPT = b"Password: "
NET_LOGGED_IN = b"Logged in. Recent console output follows.\r\n"


def net_console_connect(url, password, timeout=45.0):
    """Opens the network console at `url` and logs in (which closes any earlier client).
    Retries while the modem restarts or joins Wi-Fi. Returns (serial port, replayed
    output). The password is never printed."""
    deadline = time.monotonic() + timeout
    while True:
        ser = None
        try:
            ser = serial.serial_for_url(url, timeout=0.1, write_timeout=5)
            got = _read_until(ser, NET_PROMPT, 1.5)
            if got is None:  # pyserial empties its input on open: an empty line prompts again
                ser.write(b"\r")
                got = _read_until(ser, NET_PROMPT, 3.0)
            if got is not None:
                ser.write(password.encode("utf-8") + b"\r")
                if _read_until(ser, NET_LOGGED_IN, 5.0) is not None:
                    return ser, _read_quiet(ser, 0.5, 10.0)
                raise AssertionError("Network console refused the device password")
        except (OSError, serial.SerialException):
            pass
        if ser is not None:
            ser.close()
        if time.monotonic() >= deadline:
            raise AssertionError(f"No network console log-in at {url}")
        time.sleep(1.0)


class Port:
    """Line-oriented helper around a pyserial port."""

    def __init__(self, ser, url=None, password=None):
        self.ser = ser
        self.url = url  # socket://...: the network console
        self._password = password

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
        network saved, the banner waits up to 8 s for it.

        On the network console: AT*RESTART, then log in again and return the replayed
        boot output."""
        if self.url:
            return self._restart_network(banner, max(timeout, 45.0))
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

    def _restart_network(self, banner, timeout):
        self.ser.reset_input_buffer()
        self.ser.write(b"AT*RESTART\r")
        if _read_until(self.ser, b"Y/N", 3.0) is None:
            raise AssertionError("AT*RESTART did not ask for confirmation")
        self.ser.write(b"Y\r")
        time.sleep(2.0)  # the modem restarts 0.5 s after OK; don't log in to the old one
        self.ser.close()
        self.ser, received = net_console_connect(self.url, self._password, timeout)
        start = received.find(banner)
        if start < 0 or b"hardware." not in received[start:]:
            raise AssertionError("No complete boot banner after reset (output omitted for privacy)")
        return received


def _open(request, option, baud):
    port_name = request.config.getoption(option)
    if not port_name:
        pytest.skip(f"{option} not given")
    if serial is None:
        pytest.skip("pyserial not installed (pip install -r test/e2e/requirements.txt)")
    ser = serial.Serial(port_name, baud, timeout=0.1, write_timeout=5)
    time.sleep(0.2)
    return ser


@pytest.fixture
def console(request):
    url = request.config.getoption("--console-port")
    if url and url.startswith("socket://"):
        if serial is None:
            pytest.skip("pyserial not installed (pip install -r test/e2e/requirements.txt)")
        password = os.environ.get("USR_MODEM_PASSWORD", "")
        if not password:
            pytest.skip("USR_MODEM_PASSWORD not set (.env or environment)")
        ser, _ = net_console_connect(url, password)
        port = Port(ser, url, password)
    else:
        port = Port(_open(request, "--console-port", 115200))
    yield port
    port.ser.close()


@pytest.fixture
def rs232(request):
    ser = _open(request, "--rs232-port", request.config.getoption("--rs232-baud"))
    ser.rtscts = True
    yield Port(ser)
    ser.close()


@pytest.fixture
def usb(request):
    # USB has no line speed or CTS: open it plainly, with DTR on (the port is "open").
    ser = _open(request, "--usb-port", 115200)
    ser.dtr = True
    time.sleep(0.1)
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
