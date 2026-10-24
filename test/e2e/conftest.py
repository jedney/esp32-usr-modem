"""End-to-end test fixtures.

These tests talk to a real modem over serial ports:
  --console-port   the USB maintenance console (UART0, 115200), e.g. COM5
  --rs232-port     the RS232 port through the Unitek adapter, e.g. COM7 (later versions)

Tests that need a port that was not given are skipped, so the suite is safe to run
anywhere.
"""

import time

import pytest

try:
    import serial  # pyserial
except ImportError:  # pragma: no cover
    serial = None


def pytest_addoption(parser):
    parser.addoption("--console-port", default=None, help="USB console serial port")
    parser.addoption("--rs232-port", default=None, help="RS232 serial port (Unitek adapter)")
    parser.addoption("--rs232-baud", default=38400, type=int, help="RS232 host speed")


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
                if line and line != text:
                    lines.append(line)
                    if line in until:
                        return lines
        raise AssertionError(f"No final result for {text!r}; got {lines!r}")

    def raw(self, data, wait=0.6):
        """Send bytes as they are and return every byte received within `wait` seconds."""
        self.ser.reset_input_buffer()
        self.ser.write(data)
        received = b""
        deadline = time.monotonic() + wait
        while time.monotonic() < deadline:
            received += self.ser.read(self.ser.in_waiting or 1)
        return received

    def restart(self, banner=b"USR Modem Emulator v", timeout=8.0):
        """Reset the ESP32 through the USB-serial chip's RTS line (EN), as esptool does,
        and return the boot output once the banner has been printed."""
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
        raise AssertionError(f"No boot banner after reset; got {received[-300:]!r}")


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
