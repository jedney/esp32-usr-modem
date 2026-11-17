"""Basic SSH calls (hobby use only) against a real SSH server with password login.

Needs USR_MODEM_SSH_TEST_HOST, _USER and _PASS in .env (see .env.example); the login name
and password are typed at the modem's prompts and never printed. Phonebook entry 97 is
used and removed afterwards.
"""
import re
import time

import pytest

ENTRY = "97"


def _hangup(console):
    console.raw(b"x", wait=0.1)  # cancel a pending dial, if any
    time.sleep(1.1)
    console.raw(b"+++", wait=1.3)
    console.raw(b"ATH\r", wait=0.3)


def _read_until(console, marker, timeout):
    """Bytes received until marker appears (or the time runs out)."""
    received = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and marker not in received:
        received += console.ser.read(console.ser.in_waiting or 1)
    return received


def _type(console, text):
    console.ser.write(text.encode("utf-8"))


@pytest.fixture
def ssh_console(console, ssh_server):
    _hangup(console)
    assert console.command("ATZ", timeout=8)[-1] == "OK"
    assert console.command("ATE1Q0V1X4M0S7=30")[-1] == "OK"
    host, port, _, _ = ssh_server
    assert console.command(f"AT*PHONEBOOK={ENTRY},{host},{port},SSH")[-1] == "OK"
    try:
        yield console
    finally:
        _hangup(console)
        console.command(f"AT*PHONEDELETE={ENTRY}")
        console.command("ATZ")


def _dial_and_login(console, dial, user, password):
    console.ser.reset_input_buffer()
    _type(console, dial + "\r")
    greeting = _read_until(console, b"login as: ", 30)
    assert b"CONNECT" in greeting, "no CONNECT (response omitted)"
    assert b"login as: " in greeting, "no login prompt"
    _type(console, user + "\r")
    prompt = _read_until(console, b"'s password: ", 10)
    assert prompt.endswith(b"'s password: "), "no password prompt"
    _type(console, password + "\r")
    return password


def test_phonebook_lists_and_exports_the_ssh_entry(ssh_console, ssh_server):
    host, port, _, _ = ssh_server
    listing = "\n".join(ssh_console.command("AT*PHONEBOOK?"))
    assert f"{ENTRY},{host},{port},SSH,0" in listing
    export = ssh_console.command("AT*EXPORT", timeout=10)
    assert any(line.startswith(f"AT*PHONEBOOK={ENTRY},{host},{port},SSH") for line in export)


def test_wrong_password_is_refused_then_login_gives_a_shell(ssh_console, ssh_server):
    _, _, user, password = ssh_server
    _dial_and_login(ssh_console, f"ATDT{ENTRY}", user, password + "-wrong")
    refused = _read_until(ssh_console, b"'s password: ", 15)
    assert b"Access denied" in refused
    assert (password + "-wrong").encode() not in refused
    _type(ssh_console, password + "\r")
    # Keys typed while the login is checked are dropped: wait for the shell prompt.
    assert _read_until(ssh_console, b"$ ", 20).endswith(b"$ "), "no shell prompt"
    _type(ssh_console, "echo ssh-$((6*7))-ok; stty size\r")
    shell = _read_until(ssh_console, b"ssh-42-ok", 15)
    shell += _read_until(ssh_console, b"\n", 3)
    shell += _read_until(ssh_console, b"\n", 3)
    assert b"ssh-42-ok" in shell, "no shell output"
    assert password.encode() not in shell
    assert re.search(rb"\b24 80\b", shell), "terminal size not 80x24"
    time.sleep(1.1)
    escaped = ssh_console.raw(b"+++", wait=1.5)
    assert b"OK" in escaped
    status = "\n".join(ssh_console.command("AT*STATUS", timeout=5))
    assert re.search(r"Call outgoing SSH ", status), "AT*STATUS has no SSH call"
    headroom = re.search(r"loop (-?\d+) B, lwIP (-?\d+) B.*SSH (-?\d+) B", status)
    assert headroom, "AT*STATUS has no stack headroom line"
    loop, lwip, ssh = (int(n) for n in headroom.groups())
    print(f"Stack headroom: loop {loop} B, lwIP {lwip} B, SSH worker {ssh} B")
    assert loop >= 1024 and lwip >= 1024 and ssh >= 1024
    assert ssh_console.command("ATH", timeout=5)[-1] == "OK"


def test_shell_exit_gives_no_carrier(ssh_console, ssh_server):
    _, _, user, password = ssh_server
    _dial_and_login(ssh_console, f"ATDT{ENTRY}", user, password)
    time.sleep(2)
    _type(ssh_console, "exit\r")
    ended = _read_until(ssh_console, b"NO CARRIER", 15)
    assert b"NO CARRIER" in ended


def test_direct_dial_to_port_22_is_ssh(ssh_console, ssh_server):
    host, port, user, _ = ssh_server
    if port != 22:
        pytest.skip("the test server is not on port 22")
    ssh_console.ser.reset_input_buffer()
    _type(ssh_console, f"ATDT{host}:22\r")
    greeting = _read_until(ssh_console, b"login as: ", 30)
    assert b"CONNECT" in greeting and b"login as: " in greeting
    _type(ssh_console, user + "\r")
    assert _read_until(ssh_console, b"'s password: ", 10).endswith(b"'s password: ")
