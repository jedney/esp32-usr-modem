"""Real HTTP/Digest/WebSocket checks; no real credential changes or flash writes.

The existing USR_MODEM_PASSWORD is reused in memory. Traffic is never included
in assertion messages. --web-tests opts in; upload acceptance is separate.
"""
import base64
import contextlib
import gzip
import hashlib
import json
import os
from pathlib import Path
import re
import socket
import struct
import time
import urllib.error
import urllib.request

import pytest


def require(value, message):
    if not value:
        pytest.fail(message + " (traffic omitted for privacy)", pytrace=False)


class Web:
    def __init__(self, host):
        self.host = host
        self.base = "http://" + host
        passwords = urllib.request.HTTPPasswordMgrWithDefaultRealm()
        passwords.add_password("usr-modem", self.base, "admin", os.environ["USR_MODEM_PASSWORD"])
        self.digest = urllib.request.HTTPDigestAuthHandler(passwords)
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                                self.digest)

    def request(self, path, data=None, headers=None, auth=True, timeout=10):
        request = urllib.request.Request(self.base + path, data=data, headers=headers or {})
        if data is not None and auth:
            # Large unauthenticated bodies can reset a TCP connection before a
            # retrying client receives its 401. Supply a fresh Digest up front.
            code, response_headers, _ = self.request("/api/session", auth=False)
            require(code == 401, "Missing Digest challenge")
            challenge = response_headers["WWW-Authenticate"].split(" ", 1)[1]
            fields = urllib.request.parse_keqv_list(urllib.request.parse_http_list(challenge))
            authorization = self.digest.get_authorization(request, fields)
            require(authorization is not None, "Cannot construct Digest upload authorization")
            request.add_unredirected_header("Authorization", "Digest " + authorization)
        try:
            opener = self.opener if auth else urllib.request.build_opener(urllib.request.ProxyHandler({}))
            response = opener.open(request, timeout=timeout)
        except urllib.error.HTTPError as error:
            response = error
        return response.code, response.headers, response.read()

    def ticket(self):
        code, _, body = self.request("/api/session")
        require(code == 200, "Session login failed")
        return json.loads(body)["token"]

    @contextlib.contextmanager
    def ws(self, token=None, origin=None):
        peer = WS(self.host, token if token is not None else self.ticket(), origin)
        try:
            yield peer
        finally:
            peer.close()
            time.sleep(0.15)


class WS:
    def __init__(self, host, token, origin=None):
        self.socket = socket.create_connection((host, 80), timeout=4)
        self.buffer = b""
        key = base64.b64encode(os.urandom(16)).decode()
        text = (f"GET /ws?token={token} HTTP/1.1\r\nHost: {host}\r\n"
                f"Origin: {origin or 'http://' + host}\r\nUpgrade: websocket\r\n"
                f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                "Sec-WebSocket-Version: 13\r\n\r\n")
        self.socket.sendall(text.encode())
        while b"\r\n\r\n" not in self.buffer:
            part = self.socket.recv(4096)
            if not part:
                break
            self.buffer += part
        head, _, self.buffer = self.buffer.partition(b"\r\n\r\n")
        self.accepted = b" 101 " in head.split(b"\r\n")[0]

    def exact(self, n):
        while len(self.buffer) < n:
            data = self.socket.recv(4096)
            if not data:
                raise EOFError("WebSocket closed")
            self.buffer += data
        data, self.buffer = self.buffer[:n], self.buffer[n:]
        return data

    def send(self, data, opcode=2, final=True):
        mask = os.urandom(4)
        n = len(data)
        head = bytes([(0x80 if final else 0) | opcode, 0x80 | (n if n < 126 else 126)])
        if n >= 126:
            head += struct.pack("!H", n)
        self.socket.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def collect(self, seconds=0.5):
        text, messages = bytearray(), []
        end = time.monotonic() + seconds
        self.socket.settimeout(0.1)
        while time.monotonic() < end:
            try:
                # Commit consumption only after the entire frame has arrived.
                # A timeout between header and body must retain both, especially
                # when the larger coloured UI spans several TCP reads.
                header = 2
                total = None
                if len(self.buffer) >= 2:
                    first, second = self.buffer[:2]
                    n = second & 127
                    if n == 126:
                        header = 4
                        if len(self.buffer) >= header:
                            n = struct.unpack("!H", self.buffer[2:4])[0]
                    elif n == 127:
                        header = 10
                        if len(self.buffer) >= header:
                            n = struct.unpack("!Q", self.buffer[2:10])[0]
                    if len(self.buffer) >= header:
                        total = header + n
                if total is None or len(self.buffer) < total:
                    part = self.socket.recv(4096)
                    if not part:
                        raise EOFError("WebSocket closed")
                    self.buffer += part
                    continue
                data = self.buffer[header:total]
                self.buffer = self.buffer[total:]
                opcode = first & 15
                if opcode == 1:
                    messages.append(json.loads(data))
                elif opcode == 2:
                    text.extend(data)
            except socket.timeout:
                pass
        return bytes(text), messages

    def enter(self):
        """Every browser starts read-only; E opens the configuration screen when free."""
        text, messages = self.collect(0.4)
        self.send(b"e")
        more, later = self.collect(0.4)
        return text + more, messages + later

    def close(self):
        self.socket.close()


@pytest.fixture
def web(request, console):
    if not request.config.getoption("--web-tests"):
        pytest.skip("--web-tests not enabled")
    if not os.environ.get("USR_MODEM_PASSWORD"):
        pytest.skip("Device credential not configured")
    require(console.command("AT*WEB?", timeout=15)[-1] == "OK", "Web command unavailable")
    return Web(request.config.getoption("--web-host"))


def test_assets_require_digest_and_have_correct_gzip_mime(web):
    for path in ("/", "/app.js", "/upload.js", "/link.js", "/geometry.js", "/ui-data.js", "/api/session"):
        code, _, _ = web.request(path, auth=False)
        require(code == 401, "Unauthenticated resource exposed")
    code, headers, body = web.request("/app.js")
    require(code == 200 and headers.get("Content-Encoding") == "gzip", "Missing gzip asset")
    require(b"connectModem" in gzip.decompress(body), "Wrong page asset")
    require("script-src 'self'" in headers.get("Content-Security-Policy", ""), "Missing CSP")
    for path in ("/call-history.bin", "/image-id.txt", "/../call-history.bin", "/%2e%2e/call-history.bin"):
        require(web.request(path)[0] == 404, "Non-web file exposed")


def test_wrong_password_locks_out_for_a_second_without_stalling(web):
    passwords = urllib.request.HTTPPasswordMgrWithDefaultRealm()
    passwords.add_password("usr-modem", web.base, "admin", "not-the-password")
    wrong = urllib.request.build_opener(urllib.request.ProxyHandler({}),
                                        urllib.request.HTTPDigestAuthHandler(passwords))
    start = time.monotonic()
    try:
        code = wrong.open(web.base + "/api/session", timeout=10).code
    except urllib.error.HTTPError as error:
        code = error.code
    require(code == 401, "Wrong password accepted")
    # No sleep in the server task: the 401s (and urllib's retries) come back at once.
    require(time.monotonic() - start < 0.8, "Wrong password stalled the web server")
    # Inside the lock-out even the right password is refused; a second later it works.
    require(web.request("/api/session")[0] == 401, "Lock-out did not refuse the next attempt")
    time.sleep(1.1)
    require(web.request("/api/session")[0] == 200, "Lock-out lasted longer than a second")


def test_wrong_host_is_rejected_before_auth(web):
    require(web.request("/api/session", headers={"Host": "attacker.invalid"}, auth=False)[0] == 403,
            "Foreign Host was accepted")


def test_large_browser_headers_are_validated_before_compacting(web):
    code, _, body = web.request("/api/session", headers={"User-Agent": "browser-test-" + "x" * 1800})
    require(code == 200 and "token" in json.loads(body), "Browser-sized headers failed")


def test_web_disable_closes_owner_and_serial_can_reenable(web, console):
    try:
        with web.ws() as peer:
            peer.enter()
            require(console.command("AT*WEB=OFF")[-1] == "OK", "Web OFF failed")
            time.sleep(0.3)
            require(console.command("AT")[-1] == "OK", "Stopping web damaged modem loop")
            require(b"MODEM CONFIGURATION" in console.raw(b"AT*CONFIG\r"), "Stopping web retained reservation")
            console.raw(b"\x03", wait=0.2)
    finally:
        console.raw(b"\x03", wait=0.1)
        console.command("AT*WEB=ON", timeout=15)
    require(web.request("/api/session")[0] == 200, "Web did not restart")


def test_ticket_origin_single_use_and_initial_snapshots(web):
    ticket = web.ticket()
    with web.ws(ticket, "http://attacker.invalid") as peer:
        require(not peer.accepted, "Foreign browser Origin accepted")
    with web.ws(ticket) as peer:
        require(peer.accepted, "Valid upgrade rejected or foreign Origin consumed ticket")
        text, messages = peer.collect(1)
        require({"hello", "led", "wifi", "clock", "call"}.issubset({m["t"] for m in messages}),
                "Initial status snapshot incomplete")
        require(b"Read-only" in text and b"MODEM CONFIGURATION" not in text,
                "Browser did not start read-only")
        peer.send(b"e")
        text, messages = peer.collect(0.6)
        require(b"MODEM CONFIGURATION" in text, "Shared browser menu missing")
        require(any(m.get("t") == "access" and m.get("edit") for m in messages),
                "E did not grant edit access")
    with web.ws(ticket) as peer:
        require(not peer.accepted, "Reused ticket accepted")


def test_open_browser_claims_nothing(web, console):
    with web.ws() as peer:
        require(peer.accepted, "Browser refused")
        peer.collect(0.4)
        require(b"MODEM CONFIGURATION" in console.raw(b"AT*CONFIG\r"), "Idle browser holds the modem")
        console.raw(b"\x03", wait=0.2)


def test_one_browser_menu_and_second_browser_read_only(web, console):
    with web.ws() as first:
        require(first.accepted, "Browser refused")
        first.enter()
        with web.ws() as second:
            text, messages = second.collect(0.5)
            require(b"Read-only" in text, "Second browser took menu ownership")
            require(console.command("AT*CONFIG")[-1] == "ERROR", "Serial menu stole browser slot")
            second.send(b"e")
            _, messages = second.collect(0.3)
            require(not any(m.get("t") == "access" and m.get("edit") for m in messages),
                    "Second browser took the menu with E")
            second.send(b"3\r")
            text, _ = second.collect(0.3)
            require(b"Settings" not in text, "Read-only input invoked menu")
        first.send(b"q\r")
        first.collect(0.4)
        require(b"MODEM CONFIGURATION" in console.raw(b"AT*CONFIG\r"), "Quit did not release slot")
        console.raw(b"\x03")


def test_browser_status_during_console_menu_cannot_run_commands(web, console):
    console.raw(b"AT*CONFIG\r", wait=0.2)
    try:
        with web.ws() as peer:
            text, _ = peer.collect(0.5)
            require(b"Read-only" in text, "Browser stole serial menu")
            peer.send(b"AT*PASS\rAT*HWBRIDGE\r")
            text, _ = peer.collect(0.3)
            require(b"Device password:" not in text and b"Bridge:" not in text,
                    "Read-only browser executed commands")
    finally:
        console.raw(b"\x03", wait=0.2)


@pytest.fixture
def manual_rs232(rs232):
    automatic = rs232.ser.rtscts
    try:
        rs232.ser.rtscts = False  # driver flow control otherwise owns RTS
        rs232.ser.dtr = rs232.ser.rts = True
        yield rs232
    finally:
        rs232.ser.dtr = rs232.ser.rts = True
        rs232.ser.rtscts = automatic


def test_browser_hardware_line_toggles_restore_on_disconnect(web, manual_rs232):
    rs232 = manual_rs232
    with web.ws() as peer:
        peer.enter()
        for keys in (b"B\r", b"C\r"):
            peer.send(keys)
            text, _ = peer.collect(0.4)
        require(b"Live RS232" in text, "Browser line test did not start")
        peer.send(b"drc")
        peer.collect(0.4)
        require(rs232.ser.cd and rs232.ser.ri and rs232.ser.cts, "Output toggles not seen on COM18")
        rs232.ser.dtr = False
        rs232.ser.rts = False
        text, _ = peer.collect(1.0)
        require(b"DTR off" in text and b"RTS off" in text, "Live DTR/RTS did not follow COM18")
        rs232.ser.dtr = rs232.ser.rts = True
    time.sleep(0.3)
    require(not rs232.ser.cd and not rs232.ser.ri, "Browser disconnect left DCD/RI asserted")


def test_upload_rejects_missing_csrf_before_writing(web):
    version = (Path(__file__).resolve().parents[2] / "VERSION").read_text(encoding="utf-8").strip()
    code, _, _ = web.request(f"/api/update?target=firmware&version={version}", data=bytes(24),
                            headers={"Origin": web.base, "Content-Type": "application/octet-stream",
                                     "X-Image-SHA256": "0" * 64})
    require(code == 403, "Missing CSRF upload accepted")


def test_browser_bridge_to_com18_both_directions(web, rs232):
    with web.ws() as peer:
        peer.enter()
        for keys in (b"B\r", b"E\r"):
            peer.send(keys)
            text, _ = peer.collect(0.4)
        require(b"Bridge:" in text, "Browser bridge did not start")
        rs232.ser.write(b"from-COM18")
        text, _ = peer.collect(0.5)
        require(b"from-COM18" in text, "COM18 bytes did not reach browser")
        rs232.ser.reset_input_buffer()
        peer.send(b"from-browser")
        output = bytearray()
        end = time.monotonic() + 2
        while time.monotonic() < end and b"from-browser" not in output:
            output.extend(rs232.ser.read(rs232.ser.in_waiting or 1))
        require(b"from-browser" in output, "Browser bytes did not reach COM18")
        peer.send(b"\x18")
        text, _ = peer.collect(0.4)
        require(b"Bridge closed" in text, "Bridge stop did not restore port")


def test_http_firmware_install_reboots_into_same_tested_version(web, console, request):
    if not request.config.getoption("--browser-upload"):
        pytest.skip("--browser-upload not enabled")
    root = Path(__file__).resolve().parents[2]
    image = (root / ".pio/build/s3/firmware.bin").read_bytes()
    version = (root / "VERSION").read_text().strip()
    require(f"USRMODEM/1/FIRMWARE/{version}\n".encode() in image, "Build normal firmware first")
    with web.ws() as peer:
        _, messages = peer.enter()
        hello = next((m for m in messages if m.get("t") == "hello"), None)
        require(hello is not None, "No authenticated upload session")
        code, _, _ = web.request(f"/api/update?target=firmware&version={version}", data=image,
            headers={"Origin": web.base, "Content-Type": "application/octet-stream",
                     "X-CSRF-Token": hello["csrf"], "X-Image-SHA256": hashlib.sha256(image).hexdigest()}, timeout=180)
        require(code == 200, "HTTP firmware install failed")
    time.sleep(12)
    require(any(f"v{version}" in line for line in console.command("AT*VER", timeout=15)),
            "Wrong firmware after HTTP reboot")


def test_read_only_socket_csrf_cannot_claim_an_upload(web):
    with web.ws() as first:
        first.enter()
        with web.ws() as second:
            _, messages = second.collect(0.4)
            hello = next(m for m in messages if m.get("t") == "hello")
            code, _, _ = web.request("/api/update?target=firmware&version=0.11", data=bytes(24),
                headers={"Origin": web.base, "Content-Type": "application/octet-stream",
                         "X-CSRF-Token": hello["csrf"], "X-Image-SHA256": "0" * 64})
            require(code == 409, "Read-only session acquired update reservation")


def test_incoming_rs232_call_keeps_browser_read_only_and_signals_follow(web, console, rs232):
    profile = "\n".join(console.command("ATI4"))
    old = dict(re.findall(r"(?<!\S)(&[CD]|[EMQV])(\d+)", profile))
    old_s0 = re.search(r"S00=(\d+)", profile)
    receiver = console.command("AT*ANSWERPORT?")[0]
    caller = None
    try:
        console.command("ATE1Q0V1M0S0=0&C1&D2")
        console.command("AT*ANSWERPORT=RS232")
        rs232.ser.dtr = True
        caller = socket.create_connection((web.host, 23), timeout=4)
        observed = set()
        end = time.monotonic() + 1.2
        while time.monotonic() < end:
            observed.add(rs232.ser.ri)
            time.sleep(0.02)
        require(observed == {False, True}, "RI did not follow the UK double ring")
        with web.ws() as peer:
            text, _ = peer.collect(0.4)
            require(b"Read-only" in text, "Incoming call did not reserve browser access")
            require(b"CONNECT" in rs232.raw(b"ATA\r", wait=0.5), "RS232 ATA failed")
            require(rs232.ser.cd, "DCD did not follow the incoming carrier")
            caller.sendall(b"network-to-host")
            require(b"network-to-host" in rs232.raw(b"", wait=0.4), "Incoming payload not delivered to RS232")
            rs232.ser.dtr = False
            time.sleep(0.4)
            require(not rs232.ser.cd, "DTR drop did not clear incoming DCD")
    finally:
        if caller:
            caller.close()
        rs232.ser.dtr = False
        time.sleep(0.3)
        rs232.ser.dtr = True
        console.command("AT*ANSWERPORT=" + receiver, timeout=15)
        restore = "AT" + "".join(key + value for key, value in old.items())
        if old_s0:
            restore += "S0=" + str(int(old_s0.group(1)))
        console.raw(restore.encode() + b"\r", wait=0.2)


def test_browser_forget_requires_confirmation_and_cancel_keeps_network(web):
    with web.ws() as peer:
        peer.enter()
        peer.send(b"7\r")
        peer.collect(0.3)
        peer.send(b"5\r")
        text, _ = peer.collect(0.3)
        require(b"Forget network and disconnect browser?" in text, "Missing browser network warning")
        peer.send(b"n\r")
        peer.collect(0.3)
        require(web.request("/api/session")[0] == 200, "Cancelling forget lost the saved network")


def test_browser_guided_stop_returns_control_without_starting_leds(web, console):
    with web.ws() as peer:
        peer.enter()
        for keys in (b"B\r", b"1\r"):
            peer.send(keys)
            text, _ = peer.collect(0.3)
        require(b"Enter = start" in text, "Guided test did not reach its first prompt")
        peer.send(b"\x18")
        text, _ = peer.collect(0.4)
        require(b"Summary" in text and b"skipped" in text, "Stop did not cancel guided test")
    require(b"MODEM CONFIGURATION" in console.raw(b"AT*CONFIG\r"), "Cancelled test retained reservation")
    console.raw(b"\x03", wait=0.1)


def test_browser_quit_restores_actual_panel_leds(web):
    with web.ws() as peer:
        _, initial = peer.enter()
        peer.send(b"q\r")
        _, messages = peer.collect(0.7)
        masks = [m["m"] for m in initial + messages if m.get("t") == "led"]
        require(masks and masks[-1] & 0x40, "Menu exit left the connected Wi-Fi LED off")


def test_application_heartbeat_does_not_reach_terminal_input(web):
    with web.ws() as peer:
        peer.collect(0.3)
        peer.send(b'{"t":"ping"}', opcode=1)
        text, messages = peer.collect(0.3)
        require(any(m.get("t") == "pong" for m in messages), "No application heartbeat reply")
        require(b"ping" not in text, "Heartbeat reached terminal input")
