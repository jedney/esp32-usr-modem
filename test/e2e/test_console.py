"""End-to-end checks of the USB maintenance console (v0.1)."""

import pathlib
import time

import pytest

VERSION = (pathlib.Path(__file__).resolve().parents[2] / "VERSION").read_text().strip()


def test_at_ok(console):
    assert console.command("AT")[-1] == "OK"


def test_version_matches_repo(console):
    lines = console.command("AT*VER")
    assert lines[-1] == "OK"
    assert lines[0].startswith(f"v{VERSION} ")


def test_dialtest_amiga_retro_numeric(console):
    lines = console.command("AT*DIALTEST 170064142064")
    assert lines == ["kind=ip host=170.64.142.64 port=23", "OK"]


def test_dialtest_amiga_retro_hostname(console):
    lines = console.command("AT*DIALTEST www.amigaretro.com:23")
    assert lines == ["kind=host host=www.amigaretro.com port=23", "OK"]


def test_dialtest_internet_number(console):
    lines = console.command("AT*DIALTEST 666")
    assert lines == ["kind=special number=666 special=ppp", "OK"]


def test_dialtest_invalid(console):
    assert console.command("AT*DIALTEST 999999999999")[-1] == "ERROR"


# Information replies start on a line of their own (S3 S4 first in verbal mode), not on
# the line that echoes the command.
@pytest.mark.parametrize("query", [
    "AT*VER", "AT*WEB?", "AT*NETCONSOLE?", "AT*STYLE?", "AT*SIZE?", "AT*STATUS", "AT*SOUNDS",
    "AT*RESETINFO", "AT*INTERNET?", "AT*OTA?", "AT*REFERENCE", "AT*ABOUT", "AT*LOG",
    "AT*CALLLOG", "AT*EXPORT", "AT*HELP"])
def test_reply_starts_on_its_own_line(console, query):
    assert console.command("ATE1V1")[-1] == "OK"
    received = console.raw(query.encode("ascii") + b"\r", wait=1.5)
    assert received.startswith(query.encode("ascii") + b"\r\r\n"), received[:40]


def test_long_reply_does_not_hold_up_the_rs232_port(console, rs232):
    """AT*ABOUT (about 9.4 KB, the longest reply) goes out of the console at 115200 while
    the modem carries on; the RS232 port answers at once."""
    assert rs232.command("AT")[-1] == "OK"
    console.ser.reset_input_buffer()
    console.ser.write(b"AT*ABOUT\r")
    time.sleep(0.05)  # the reply has started
    started = time.monotonic()
    assert rs232.command("AT")[-1] == "OK"
    assert time.monotonic() - started < 0.3
    console.raw(b"", wait=1.0)  # let the reply finish


def test_unknown_command_is_error(console):
    # J and G are not Sportster commands. (Since v0.4 "ATXYZZY" is valid: X, Y, Z.)
    assert console.command("ATJ")[-1] == "ERROR"
    assert console.command("ATG1")[-1] == "ERROR"
