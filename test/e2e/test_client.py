"""Client diagnostics on real serial clients. Binary transfer interoperability is
in test/interop; these cases require only pytest and pyserial.
"""
import re
import time
import pytest


def receive(ser, needle, timeout=5):
    data = bytearray()
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        data.extend(ser.read(ser.in_waiting or 1))
        if needle in data:
            return bytes(data)
    raise AssertionError('client diagnostic response missing (raw output omitted)')


def start(port, command):
    port.ser.reset_input_buffer()
    port.ser.write(command.encode('ascii') + b'\r')


@pytest.mark.parametrize('fixture', ['console', 'rs232'])
def test_client_size_keys_and_return_to_at(request, fixture):
    port = request.getfixturevalue(fixture)
    assert 'OK' in port.command('ATE0Q0V1', timeout=15)
    start(port, 'AT*TEST SIZE')
    receive(port.ser, b'\x1b[6n')
    for part in (b'\x1b[', b'25;', b'80R'):
        port.ser.write(part)
        time.sleep(.02)
    assert b'Measured 80 columns x 25 rows' in receive(port.ser, b'OK')
    time.sleep(.1)
    start(port, 'AT*TEST KEYS')
    receive(port.ser, b'30 s timeout')
    port.ser.write(b'\x1b[A\x7f\r\n')
    data = receive(port.ser, b'0D 0A')
    assert b'1B 5B 41' in data
    port.ser.write(b'\x1d')
    receive(port.ser, b'Capture ended')
    time.sleep(.1)
    assert 'OK' in port.command('AT')


def test_client_manual_size_fallback(console):
    start(console, 'AT*TEST SIZE')
    receive(console.ser, b'Enter visible columns,rows:', timeout=3)
    console.ser.write(b'80,24\r')
    receive(console.ser, b'Manual size recorded')
    time.sleep(.1)
    start(console, 'AT*TEST SUMMARY')
    assert b'80,24' in receive(console.ser, b'OK')


def test_client_active_test_reserves_slot(console, rs232):
    start(console, 'AT*TEST KEYS')
    receive(console.ser, b'30 s timeout')
    try:
        assert 'ERROR' in rs232.command('AT*TEST BELL')
        assert 'ERROR' in rs232.command('AT*CONFIG')
    finally:
        console.ser.write(b'\x1d')
        receive(console.ser, b'Capture ended')


def test_client_format_bell_confirmation(console):
    start(console, 'AT*TEST FORMAT')
    data = receive(console.ser, b'OK')
    assert b'115200 8N1' in data
    time.sleep(.1)
    start(console, 'AT*TEST BELL')
    assert b'\7' in receive(console.ser, b'Esc cancels')
    console.ser.write(b'n')
    receive(console.ser, b'Manual display/bell: fail')


def test_client_apply_cancel_does_not_persist(console):
    start(console, 'AT*TEST SUMMARY')
    data = receive(console.ser, b'OK')
    saved = re.search(rb'Saved: ([A-Z0-9,]+)', data).group(1)
    time.sleep(.1)
    start(console, 'AT*TEST SET=CP437,80,24,127')
    receive(console.ser, b'OK')
    time.sleep(.1)
    start(console, 'AT*TEST APPLY')
    receive(console.ser, b'Y confirms')
    console.ser.write(b'n')
    receive(console.ser, b'saved settings retained')
    time.sleep(.1)
    start(console, 'AT*TEST SUMMARY')
    data = receive(console.ser, b'OK')
    assert b'Saved: ' + saved in data
    time.sleep(.1)
    start(console, 'AT*TEST SET=' + saved.decode())
    receive(console.ser, b'OK')


def saved_settings(port):
    start(port, 'AT*TEST SUMMARY')
    data = receive(port.ser, b'OK')
    time.sleep(.1)
    return re.search(rb'Saved: ([A-Z0-9,]+)', data).group(1)


def save_settings(port, settings):
    start(port, 'AT*TEST SET=' + settings.decode())
    receive(port.ser, b'OK')
    time.sleep(.1)
    start(port, 'AT*TEST APPLY')
    receive(port.ser, b'Y confirms')
    port.ser.write(b'y')
    receive(port.ser, b'Client settings saved')
    time.sleep(.1)


def test_client_saved_port_profiles_survive_restart_and_restore(console, rs232):
    console.command('ATE0Q0V1')
    before = [saved_settings(port) for port in (console, rs232)]
    chosen = [b'ANSI,80,24,8', b'CP437,132,50,127']
    try:
        for port, settings in zip((console, rs232), chosen):
            save_settings(port, settings)
        console.restart()
        for port, settings in zip((console, rs232), chosen):
            assert saved_settings(port) == settings
    finally:
        for port, settings in zip((console, rs232), before):
            save_settings(port, settings)


def test_client_throughput_records_rts_pause(rs232):
    ser = rs232.ser
    try:
        ser.rtscts = False
        ser.rts = True
        start(rs232, 'AT*TEST TX=2')
        receive(ser, b'Ctrl-] cancels')
        time.sleep(.4)
        ser.rts = False
        time.sleep(.4)
        ser.rts = True
        data = receive(ser, b'Timed test complete', timeout=5)
        match = re.search(rb'RTS-pauses=(\d+) paused-ms=(\d+)', data)
        assert match and int(match[1]) >= 1 and int(match[2]) >= 200
    finally:
        ser.rts = True
        ser.rtscts = True


def test_client_cable_toggles_and_restores_lines(rs232):
    ser = rs232.ser
    before = (ser.cd, ser.ri, ser.cts)
    start(rs232, 'AT*TEST CABLE')
    states = set()
    deadline = time.monotonic() + 8
    data = bytearray()
    while time.monotonic() < deadline:
        states.add((ser.cd, ser.ri, ser.cts))
        data.extend(ser.read(ser.in_waiting or 1))
        if b'Timed test complete' in data:
            break
    assert b'Timed test complete' in data
    assert any(cd for cd, _, _ in states)
    assert any(ri for _, ri, _ in states)
    assert any(not cts for _, _, cts in states)
    assert ser.dsr
    time.sleep(.1)
    assert (ser.cd, ser.ri, ser.cts) == before
