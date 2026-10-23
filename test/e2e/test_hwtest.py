"""End-to-end checks of the AT*HW hardware-test commands (v0.3, brief 6.14).

These need only the USB console; nothing has to be wired to the S3 (the commands drive
the pins whether or not anything is connected). The bridge test also needs the RS232
port (--rs232-port) through the bench DB9 or the DB25 and the Unitek adapter.
"""

import time

import pytest


def test_help_lists_hardware_tests(console):
    lines = console.command("AT*HELP")
    assert lines[-1] == "OK"
    assert any("AT*HWTEST" in line for line in lines)


def test_hw_question_lists_commands(console):
    lines = console.command("AT*HW?")
    assert lines[-1] == "OK"
    assert any(line.startswith("AT*HWLED=") for line in lines)


def test_pins_are_shown(console):
    lines = console.command("AT*HWPINS")
    assert lines[-1] == "OK"
    assert any("DB25 20" in line for line in lines)


def test_info_shows_chip_and_memory(console):
    lines = console.command("AT*HWINFO")
    assert lines[-1] == "OK"
    text = "\n".join(lines)
    assert "ESP32-S3" in text
    assert "PSRAM" in text


@pytest.mark.parametrize("arg", ["CD", "ARQ", "fax", "ALL", "OFF"])
def test_led_commands(console, arg):
    assert console.command(f"AT*HWLED={arg}")[-1] == "OK"


def test_led_bad_name_is_error(console):
    assert console.command("AT*HWLED=XX")[-1] == "ERROR"


def test_volume_range(console):
    assert console.command("AT*HWVOL=50")[-1] == "OK"
    assert console.command("AT*HWVOL=101")[-1] == "ERROR"
    lines = console.command("AT*HWVOL?")
    assert lines[0].startswith("Test volume: 50%")
    assert console.command("AT*HWVOL=75")[-1] == "OK"


def test_short_tone_plays_and_finishes(console):
    lines = console.command("AT*HWTONE=1000,200", timeout=5)
    assert lines[-1] == "OK"


def test_tone_out_of_range_is_error(console):
    assert console.command("AT*HWTONE=9000")[-1] == "ERROR"


def test_signal_set_and_clear(console):
    assert console.command("AT*HWSIG=DCD,ON")[-1] == "OK"
    assert console.command("AT*HWSIG=DCD,OFF")[-1] == "OK"
    assert console.command("AT*HWSIG=DSR,ON")[-1] == "ERROR"  # hard-wired


def test_unknown_hw_command_is_error(console):
    assert console.command("AT*HWBOGUS")[-1] == "ERROR"


def _read_for(ser, seconds):
    deadline = time.monotonic() + seconds
    data = b""
    while time.monotonic() < deadline:
        data += ser.read(ser.in_waiting or 1)
    return data


def test_bridge_passes_data_both_ways(console, rs232):
    """Console <-> RS232 pass-through: proves TXD/RXD through the MAX3232 and cable."""
    baud = rs232.ser.baudrate
    console.ser.reset_input_buffer()
    console.ser.write(f"AT*HWBRIDGE={baud}\r".encode())
    assert b"Bridge" in _read_for(console.ser, 0.5)
    rs232.ser.reset_input_buffer()

    rs232.ser.write(b"from-pc\r\n")
    assert b"from-pc" in _read_for(console.ser, 0.5)

    console.ser.write(b"to-pc")
    assert b"to-pc" in _read_for(rs232.ser, 0.5)

    console.ser.write(b"\x18")  # Ctrl-X ends the bridge
    assert b"OK" in _read_for(console.ser, 0.5)
