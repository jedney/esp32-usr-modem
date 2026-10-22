"""End-to-end checks of the USB maintenance console (v0.1)."""

import pathlib

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


def test_unknown_command_is_error(console):
    assert console.command("ATXYZZY")[-1] == "ERROR"
