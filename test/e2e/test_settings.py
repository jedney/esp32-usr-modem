"""End-to-end checks of the AT command parser and stored settings on the USB console
(spec 3.2, 3.5, 3.6, 3.7).

These tests change settings. Each one starts and ends with ATZ (back to the stored
template) and runs with the factory model, restoring the saved model after. The restart
test saves the factory &F1 template to Y0 when it finishes.
"""

import pytest


@pytest.fixture(autouse=True)
def stored_settings(request):
    if "console" not in request.fixturenames:
        yield
        return
    console = request.getfixturevalue("console")
    # The identity checks expect the factory model; put the saved one back after.
    before_model = console.command("AT*MODEL?")[0].split()[0]
    assert console.command("AT*MODEL=14400")[-1] == "OK"
    console.raw(b"ATZ\r")
    try:
        yield
    finally:
        console.raw(b"ATZ\r")
        console.command("AT*MODEL=" + before_model)


def test_echo_off_and_on(console):
    assert console.command("ATE0")[-1] == "OK"
    received = console.raw(b"AT\r")
    assert received == b"\r\nOK\r\n"  # no echo of "AT"
    assert console.command("ATE1")[-1] == "OK"
    assert console.raw(b"AT\r").startswith(b"AT\r")


def test_numeric_results(console):
    assert console.raw(b"ATV0\r").endswith(b"0\r")
    assert console.raw(b"ATJ\r").endswith(b"4\r")  # ERROR
    assert console.command("ATV1")[-1] == "OK"


def test_quiet_mode(console):
    received = console.raw(b"ATQ1\r")
    assert b"OK" not in received
    assert b"OK" not in console.raw(b"AT\r")
    assert console.command("ATQ0")[-1] == "OK"


def test_several_commands_on_one_line(console):
    assert console.command("ATE1 V1 X3 S7=45")[-1] == "OK"
    assert console.command("ATS7?") == ["045", "OK"]
    lines = console.command("ATI4")
    assert any("X3" in line for line in lines)


def test_first_bad_command_stops_the_line(console):
    assert console.command("ATS7=30J S7=40")[-1] == "ERROR"
    assert console.command("ATS7?") == ["030", "OK"]


def test_repeat_with_a_slash(console):
    console.command("ATI3")
    received = console.raw(b"A/")
    assert b"Sportster 14.4k Series" in received
    assert received.endswith(b"OK\r\n")


def test_identity(console):
    assert console.command("ATI3") == ["Sportster 14.4k Series", "OK"]
    lines = console.command("ATI7")
    assert "Product type           US/Canada External" in lines
    assert console.command("ATI8")[-1] == "ERROR"


def test_current_settings_screen(console):
    lines = console.command("ATI4")
    assert "Sportster 14.4k Series Settings..." in lines
    assert "BAUD=115200  PARITY=N  WORDLEN=8" in lines
    assert any(line.startswith("S00=") for line in lines)


def test_fax_classes_are_refused(console):
    assert console.command("AT+FCLASS=0")[-1] == "OK"
    assert console.command("AT+FCLASS=1")[-1] == "ERROR"
    assert console.command("AT+FCLASS?") == ["0", "OK"]


def test_password_separator_visible_without_changing_credentials(console):
    assert console.command("ATE1")[-1] == "OK"
    output = console.raw(b"AT*PASS=short\r")  # deliberately invalid; no credential write
    boundary_correct = output.startswith(b"AT*PASS=*****\r")
    assert boundary_correct, "masking must start after the visible equals sign"
    assert output.rstrip().endswith(b"ERROR")
    assert console.raw(b"A/").rstrip().endswith(b"ERROR")


def test_factory_templates(console):
    assert console.command("AT&F0")[-1] == "OK"
    assert any(line.startswith("&A1  &B1  &C1  &D2  &G0  &H0") for line in console.command("ATI4"))
    assert console.command("AT&F1")[-1] == "OK"
    assert any(line.startswith("&A0  &B1  &C1  &D2  &G0  &H1") for line in console.command("ATI4"))


def test_project_commands_still_work(console):
    assert console.command("AT*VER")[-1] == "OK"
    lines = console.command("AT*HELP")
    assert any("AT*DIALTEST" in line for line in lines)
    assert console.command("AT*NOPE")[-1] == "ERROR"


def test_saved_settings_survive_a_restart(console):
    """AT&V shows the saved profile after a restart."""
    try:
        assert console.command("AT&F1 S7=45 X3 &W")[-1] == "OK"
        console.command("AT&F1")  # change the active settings so the restart matters
        boot = console.restart()
        assert b"Settings: saved settings" in boot
        lines = console.command("AT&V")
        assert any(line.startswith("S07=045") for line in lines)
        assert "B0  E1  F1  L2  M1  Q0  V1  X3  Y0" in lines
    finally:
        console.command("AT&F1&W")
