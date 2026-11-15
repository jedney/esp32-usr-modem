"""Real HTTP/login/WebSocket checks; maintenance writes require explicit flags.

The existing USR_MODEM_PASSWORD is reused in memory. Traffic is never included
in assertion messages. --web-tests opts in; upload acceptance is separate.
"""
import base64
import concurrent.futures
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
import threading
import urllib.error
import urllib.parse
import urllib.request

import pytest


def require(value, message):
    if not value:
        pytest.fail(message + " (traffic omitted for privacy)", pytrace=False)


class NoRedirect(urllib.request.HTTPRedirectHandler):
    """Report 303s as they are: the tests check where the modem sends the browser."""

    def redirect_request(self, *args, **kwargs):
        return None


def session_cookie(headers):
    """The usr_session value a response sets, "" if it expires the cookie, None if absent."""
    value = headers.get("Set-Cookie") or ""
    if not value.startswith("usr_session="):
        return None
    return "" if "Max-Age=0" in value else value.split(";", 1)[0].split("=", 1)[1]


class Web:
    """One browser: the login form, then the session cookie on every request."""

    def __init__(self, host, login=True):
        self.host = host
        self.base = "http://" + host
        self.cookie = None
        self.opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect)
        if login:
            code, location = self.login()
            require(code == 303 and location == "/", "Browser login failed")

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.logout()

    def login(self, user="admin", password=None, origin=None, headers=None):
        """Posts the login form. Returns (status, Location) and keeps any new cookie."""
        if password is None:
            password = os.environ["USR_MODEM_PASSWORD"]
        body = urllib.parse.urlencode({"username": user, "password": password}).encode()
        sent = {"Content-Type": "application/x-www-form-urlencoded", "Origin": origin or self.base}
        sent.update(headers or {})
        code, response, _ = self.request("/api/login", data=body, headers=sent)
        cookie = session_cookie(response)
        if cookie is not None:
            self.cookie = cookie or None
        return code, response.get("Location")

    def auth(self):
        code, _, body = self.request("/api/auth")
        return json.loads(body) if code == 200 else None

    def logout(self):
        """Ends this browser's session; harmless when it has already ended."""
        with contextlib.suppress(OSError, ValueError):
            auth = self.auth()
            code, response, _ = self.request("/api/logout", data=b"", headers={
                "Origin": self.base, "X-CSRF-Token": auth["csrf"] if auth else ""})
            if session_cookie(response) == "":
                self.cookie = None
            return code
        return None

    def request(self, path, data=None, headers=None, auth=True, timeout=10):
        headers = dict(headers or {})
        if auth and self.cookie and "Cookie" not in headers:
            headers["Cookie"] = "usr_session=" + self.cookie
        request = urllib.request.Request(self.base + path, data=data, headers=headers)
        try:
            response = self.opener.open(request, timeout=timeout)
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
        self.closed = False
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

    def collect(self, seconds=0.5, allow_close=False):
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
                        self.closed = True
                        if allow_close:
                            break
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

    def open_terminal(self):
        text, messages = self.collect(0.3)
        self.send(b'{"t":"open"}', opcode=1)
        more, later = self.collect(0.3)
        return text + more, messages + later

    def enter(self):
        """Open the terminal explicitly, then enter the shared configuration menu."""
        text, messages = self.open_terminal()
        self.send(b"AT*CONFIG\r")
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
    browser = Web(request.config.getoption("--web-host"))
    yield browser
    browser.logout()  # eight sessions at most: never leave one behind


def test_assets_require_session_and_have_correct_gzip_mime(web):
    for path in ("/app.js", "/upload.js", "/upload_flow.js", "/link.js", "/geometry.js", "/idle.js", "/toast.js", "/ui-data.js", "/ui-commands.js", "/menubar.js", "/menu-view.js", "/command-channel.js", "/xterm.mjs", "/api/session", "/api/auth", "/api/boot"):
        code, _, _ = web.request(path, auth=False)
        require(code == 401, "Unauthenticated resource exposed")
    code, headers, _ = web.request("/", auth=False)
    require(code == 303 and headers.get("Location") == "/login", "Page did not send the browser to log in")
    code, headers, body = web.request("/app.js")
    require(code == 200 and headers.get("Content-Encoding") == "gzip", "Missing gzip asset")
    require(b"connectModem" in gzip.decompress(body), "Wrong page asset")
    require("script-src 'self'" in headers.get("Content-Security-Policy", ""), "Missing CSP")
    code, headers, body = web.request("/usr-logo.png", auth=False)
    require(code == 200 and headers.get_content_type() == 'image/png', 'Missing logo asset')
    require(gzip.decompress(body) == (Path(__file__).resolve().parents[2] / 'web/src/usr-logo.png').read_bytes(),
            'Served logo differs from supplied artwork')
    for path in ("/call-history.bin", "/image-id.txt", "/../call-history.bin", "/%2e%2e/call-history.bin"):
        require(web.request(path)[0] == 404, "Non-web file exposed")


def test_login_page_and_its_presentation_files_are_public(web):
    code, headers, body = web.request("/login", auth=False)
    require(code == 200 and headers.get_content_type() == "text/html", "Login page unavailable")
    page = gzip.decompress(body)
    require(b'action="/api/login"' in page and b'autocomplete="current-password"' in page, "Wrong login page")
    # Under no-referrer, browsers send "Origin: null" with the form, which the login refuses.
    require(headers.get("Referrer-Policy") == "same-origin", "Login page referrer policy")
    require(headers.get("Cache-Control") == "no-store", "Login page may be cached")
    for path in ("/login.js", "/style.css", "/menubar.css", "/ibm-plex-mono-400.woff2", "/ibm-plex-mono-700.woff2"):
        require(web.request(path, auth=False)[0] == 200, "Login page asset not public")
    code, headers, _ = web.request("/login")
    require(code == 303 and headers.get("Location") == "/", "A logged-in browser was shown the login page")


def test_login_sets_a_strict_session_cookie_and_reports_its_deadline(web):
    browser = Web(web.host, login=False)
    body = urllib.parse.urlencode({"username": "admin", "password": os.environ["USR_MODEM_PASSWORD"]}).encode()
    code, headers, _ = browser.request("/api/login", data=body, headers={
        "Origin": web.base, "Content-Type": "application/x-www-form-urlencoded"})
    require(code == 303 and headers.get("Location") == "/", "Login refused")
    cookie = headers.get("Set-Cookie", "")
    for part in ("Path=/", "HttpOnly", "SameSite=Strict", "Max-Age=43200"):
        require(part in cookie, "Session cookie attribute missing")
    require("Secure" not in cookie and "Domain" not in cookie, "Unexpected cookie scope")
    browser.cookie = session_cookie(headers)
    require(re.fullmatch(r"[0-9a-f]{32}", browser.cookie or "") is not None, "Session id is not 128 bits")
    try:
        auth = browser.auth()
        require(auth is not None and auth["user"] == "admin", "Session status unavailable")
        require(43100 < auth["expires"] <= 43200, "Session deadline is not 12 hours from login")
        require(re.fullmatch(r"[0-9a-f]{32}", auth["csrf"]) and auth["csrf"] != browser.cookie, "Bad CSRF token")
        require(browser.auth()["expires"] <= auth["expires"], "Using the session extended it")
    finally:
        browser.logout()


def test_login_and_logout_need_this_modem_as_host_and_origin(web):
    browser = Web(web.host, login=False)
    for origin in ("http://attacker.invalid", "null", "http://" + web.host + ":81"):
        require(browser.login(origin=origin)[0] == 403, "Foreign Origin could log in")
    require(browser.login(headers={"Host": "attacker.invalid"})[0] == 403, "Foreign Host could log in")
    require(browser.cookie is None, "A refused login set a cookie")
    time.sleep(1.1)
    with Web(web.host) as other:
        csrf = other.auth()["csrf"]
        code, _, _ = other.request("/api/logout", data=b"", headers={"Origin": "http://attacker.invalid", "X-CSRF-Token": csrf})
        require(code == 403, "Foreign Origin could log out")
        code, _, _ = other.request("/api/logout", data=b"", headers={"Origin": other.base})
        require(code == 403, "Logout without the CSRF token")
        code, _, _ = other.request("/api/logout", data=b"", headers={"Origin": other.base, "X-CSRF-Token": "0" * 32})
        require(code == 403, "Logout with a wrong CSRF token")
        require(other.auth() is not None, "A refused logout ended the session")


def test_login_form_and_cookies_are_parsed_strictly(web):
    browser = Web(web.host, login=False)
    form = {"Origin": web.base, "Content-Type": "application/x-www-form-urlencoded"}
    password = urllib.parse.quote(os.environ["USR_MODEM_PASSWORD"])

    def refused(body, headers):
        # The modem answers 400 and closes without reading the body: the client may see either.
        try:
            return browser.request("/api/login", data=body, headers=headers)[0] == 400
        except ConnectionError:
            return True

    require(refused(b"username=admin", {**form, "Content-Type": "text/plain"}),
            "Login accepted another content type")
    require(refused(b"username=admin&password=" + b"x" * 300, form), "Oversized login body accepted")
    require(browser.cookie is None, "A refused login set a cookie")
    for body in (f"username=admin&password={password}&password={password}", f"username=admin&username=admin&password={password}",
                 f"username=admin&password={password}&extra=1"):
        code, headers, _ = browser.request("/api/login", data=body.encode(), headers=form)
        require(code == 303 and headers.get("Location") == "/login?error=invalid", "Ambiguous form accepted")
        require(session_cookie(headers) is None, "Ambiguous form set a cookie")
        time.sleep(1.1)
    for cookie in ("usr_session=" + "0" * 31, "usr_session=" + "A" * 32, "usr_session=" + web.cookie + "; usr_session=" + web.cookie):
        require(web.request("/api/auth", headers={"Cookie": cookie})[0] == 400, "Ambiguous session cookie accepted")
    require(web.request("/api/auth", headers={"Cookie": "usr_session=" + "0" * 32})[0] == 401, "Unknown session accepted")
    # Two Cookie headers never reach a handler: the connection is closed.
    with socket.create_connection((web.host, 80), timeout=4) as sock:
        sock.sendall((f"GET /api/auth HTTP/1.1\r\nHost: {web.host}\r\nCookie: usr_session={web.cookie}\r\n"
                      f"Cookie: usr_session={'0' * 32}\r\n\r\n").encode())
        reply = b""
        with contextlib.suppress(OSError):
            while chunk := sock.recv(4096):
                reply += chunk
    require(b" 200 " not in reply.split(b"\r\n")[0], "Duplicate Cookie headers accepted")
    require(web.auth() is not None, "Malformed requests ended the session")


def test_recent_console_history_requires_auth_and_replays_banner(web, console):
    # A ring is not a permanent boot transcript: make the banner recent for this assertion.
    console.restart()
    log_in_again(web)  # a restart ends every session
    code, _, _ = web.request('/api/boot', headers={'Host': 'attacker.invalid'}, auth=False)
    require(code == 403, 'Console history accepted a foreign Host')
    code, _, _ = web.request('/api/boot', auth=False)
    require(code == 401, 'Console history exposed without authentication')
    code, headers, body = web.request('/api/boot')
    require(code == 200, 'Console history unavailable')
    require(headers.get_content_type() == 'text/plain', 'Wrong console history type')
    require(headers.get('Cache-Control') == 'no-store', 'Console history may be cached')
    require(0 < len(body) <= 16384, 'Console history exceeded ring capacity')
    require(b'USR Modem Emulator v' in body, 'Recent console history missing boot banner')


def test_parallel_page_load_has_no_connection_resets(web):
    """A browser loads a dozen files at once, each on its own connection."""
    paths = ["/", "/app.js", "/upload.js", "/upload_flow.js", "/link.js", "/geometry.js", "/idle.js", "/toast.js",
             "/ui-data.js", "/ui-commands.js", "/menubar.js", "/menu-view.js", "/command-channel.js", "/menubar.css", "/api/session"] * 2

    def fetch(path):
        try:
            return web.request(path)[0]
        except OSError as error:
            return type(error).__name__

    failures = []
    for _ in range(5):
        with concurrent.futures.ThreadPoolExecutor(6) as pool:
            failures += [code for code in pool.map(fetch, paths) if code != 200]
        time.sleep(0.5)
    require(not failures, f"{len(failures)} of {5 * len(paths)} parallel requests failed: "
            + ", ".join(sorted(set(map(str, failures)))))


def test_wrong_password_locks_out_for_a_second_without_stalling(web):
    browser = Web(web.host, login=False)
    browser.request("/login")  # the PC's first name lookup can take seconds
    start = time.monotonic()
    code, location = browser.login(password="not-the-password")
    require(code == 303 and location == "/login?error=invalid", "Wrong password accepted")
    # No sleep in the server task: the refusal comes back at once.
    require(time.monotonic() - start < 0.8, "Wrong password stalled the web server")
    # Inside the lock-out even the right password is refused, without being checked.
    code, location = browser.login()
    require(code == 303 and location == "/login?error=wait", "Lock-out did not refuse the next attempt")
    require(browser.cookie is None, "A refused login set a cookie")
    time.sleep(1.1)
    code, location = browser.login()
    require(code == 303 and location == "/", "Lock-out lasted longer than a second")
    browser.logout()


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
            require(b"A. Status" in console.raw(b"AT*CONFIG\r"), "Stopping web retained reservation")
            console.raw(b"\x03", wait=0.2)
    finally:
        console.raw(b"\x03", wait=0.1)
        console.command("AT*WEB=ON", timeout=15)
    require(web.request("/api/session")[0] == 200, "Web did not restart")


def test_new_terminal_starts_with_real_at_ok_without_session_banner(web, console):
    profile = ' '.join(console.command('ATI4'))
    old = dict(re.findall(r'(?<!\S)([EQV])(\d+)', profile))
    console.command('ATE1Q0V1')
    try:
        with web.ws() as peer:
            initial, messages = peer.collect(0.3)
            require(not initial, 'Observer received a session banner')
            peer.send(b'{"t":"open"}', opcode=1)
            text, messages = peer.collect(0.4)
            require(any(m.get('t') == 'access' and m.get('edit') for m in messages), 'Terminal unavailable')
            require(text == b'AT\r\r\nOK\r\n', 'Fresh terminal did not contain only AT and its OK reply')
            ready = next((m for m in reversed(messages) if m.get('t') == 'ready'), {})
            require(ready.get('idle') and ready.get('rx') == 0, 'Initial probe broke command readiness')
    finally:
        console.command('AT' + ''.join(k + v for k, v in old.items()))


def test_ticket_origin_single_use_and_initial_snapshots(web):
    ticket = web.ticket()
    with web.ws(ticket, "http://attacker.invalid") as peer:
        require(not peer.accepted, "Foreign browser Origin accepted")
    with web.ws(ticket) as peer:
        require(peer.accepted, "Valid upgrade rejected or foreign Origin consumed ticket")
        text, messages = peer.collect(1)
        require({"hello", "led", "wifi", "clock", "call"}.issubset({m["t"] for m in messages}),
                "Initial status snapshot incomplete")
        require(not text,
                "Browser did not start read-only")
        peer.send(b'{"t":"open"}', opcode=1)
        _, messages = peer.collect(0.3)
        peer.send(b"AT*CONFIG\r")
        text, later = peer.collect(0.4)
        messages += later
        require(b"A. Status" in text, "Shared browser menu missing")
        require(any(m.get("t") == "access" and m.get("edit") for m in messages),
                "Explicit open did not grant terminal access")
    with web.ws(ticket) as peer:
        require(not peer.accepted, "Reused ticket accepted")


def test_open_browser_claims_nothing(web, console):
    with web.ws() as peer:
        require(peer.accepted, "Browser refused")
        peer.collect(0.4)
        require(b"A. Status" in console.raw(b"AT*CONFIG\r"), "Idle browser holds the modem")
        console.raw(b"\x03", wait=0.2)


def test_one_browser_menu_and_second_browser_read_only(web, console):
    with web.ws() as first:
        require(first.accepted, "Browser refused")
        first.enter()
        with web.ws() as second:
            text, messages = second.collect(0.5)
            require(not text and any(m.get("t") == "access" and not m.get("edit") for m in messages), "Second browser took menu ownership")
            require(console.command("AT*CONFIG")[-1] == "ERROR", "Serial menu stole browser slot")
            second.send(b'{"t":"open"}', opcode=1)
            _, messages = second.collect(0.3)
            require(not any(m.get("t") == "access" and m.get("edit") for m in messages),
                    "Second browser took terminal ownership")
            second.send(b"c\r")
            text, _ = second.collect(0.3)
            require(b"Settings" not in text, "Read-only input invoked menu")
        first.send(b"q\r")
        first.collect(0.4)
        require(b"A. Status" in console.raw(b"AT*CONFIG\r"), "Quit did not release slot")
        console.raw(b"\x03")


def test_browser_status_during_console_menu_cannot_run_commands(web, console):
    console.raw(b"AT*CONFIG\r", wait=0.2)
    try:
        with web.ws() as peer:
            text, _ = peer.collect(0.5)
            require(not text, "Browser stole serial menu")
            peer.send(b'{"t":"open"}', opcode=1)
            peer.collect(0.2)
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
        for keys in (b"ede\r", b"l\r"):
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
        for keys in (b"ede\r", b"n\r"):
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
            require(not text, "Incoming call did not reserve browser access")
            peer.send(b'{"t":"open"}', opcode=1)
            _, messages = peer.collect(0.3)
            require(not any(m.get('edit') for m in messages), 'Terminal open stole incoming RS232 call')
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
        peer.send(b"dc\r")
        peer.collect(0.3)
        peer.send(b"e\r")
        text, _ = peer.collect(0.3)
        require(b"Forget network and disconnect browser?" in text, "Missing browser network warning")
        peer.send(b"n\r")
        peer.collect(0.3)
        require(web.request("/api/session")[0] == 200, "Cancelling forget lost the saved network")


def test_browser_guided_stop_returns_control_without_starting_leds(web, console):
    with web.ws() as peer:
        peer.enter()
        for keys in (b"ede\r", b"a\r"):
            peer.send(keys)
            text, _ = peer.collect(0.3)
        require(b"Enter = start" in text, "Guided test did not reach its first prompt")
        peer.send(b"\x18")
        text, _ = peer.collect(0.4)
        require(b"Summary" in text and b"skipped" in text, "Stop did not cancel guided test")
    require(b"A. Status" in console.raw(b"AT*CONFIG\r"), "Cancelled test retained reservation")
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


# --- Limits and abuse. Opt-in: --flood-tests, --upload-tests ---------------------------


def flood_only(request):
    if not request.config.getoption("--flood-tests"):
        pytest.skip("--flood-tests not enabled")


def open_idle_sockets(host, port, count):
    """Connections that say nothing. Refusals are fine; the point is to hold slots."""
    sockets = []
    for _ in range(count):
        try:
            sockets.append(socket.create_connection((host, port), timeout=2))
        except OSError:
            pass
    return sockets


def test_fourth_browser_is_refused_and_the_slot_returns(web, request):
    flood_only(request)
    with contextlib.ExitStack() as stack:
        peers = [stack.enter_context(web.ws()) for _ in range(3)]
        require(all(peer.accepted for peer in peers), "Three browsers must be accepted")
        try:
            fourth = stack.enter_context(web.ws())
            refused = not fourth.accepted
        except (OSError, EOFError):
            refused = True
        require(refused, "A fourth browser was accepted")
        peers[0].close()
        time.sleep(1.5)
        with web.ws() as again:
            require(again.accepted, "The slot of a closed browser was not freed")


def test_idle_connection_flood_on_port_80_leaves_the_web_server_and_console_working(
        web, console, request):
    flood_only(request)
    held = open_idle_sockets(web.host, 80, 40)
    try:
        time.sleep(3)
        require(console.command("AT")[-1] == "OK", "The console stopped answering during a flood")
    finally:
        for sock in held:
            sock.close()
    # The server drops idle connections itself; allow it a few seconds, then it must serve.
    deadline = time.monotonic() + 15
    code = None
    while time.monotonic() < deadline and code != 200:
        try:
            code = web.request("/api/session", timeout=4)[0]
        except OSError:
            time.sleep(1)
    require(code == 200, "The web server did not recover after a connection flood")


def test_idle_connection_flood_on_port_23_leaves_the_console_working_and_no_call_behind(
        web, console, request):
    flood_only(request)
    held = open_idle_sockets(web.host, 23, 20)
    try:
        time.sleep(3)
        require(console.command("AT")[-1] == "OK", "The console stopped answering during a flood")
    finally:
        for sock in held:
            sock.close()
    time.sleep(2)
    console.command("ATH")
    require(console.command("AT")[-1] == "OK", "The console stopped answering after a flood")
    # A caller can still get through afterwards.
    with socket.create_connection((web.host, 23), timeout=4):
        time.sleep(1)
    time.sleep(1)
    require(console.command("AT")[-1] == "OK", "A caller after the flood left the modem stuck")


def test_interrupted_upload_frees_the_modem(web, console, request):
    if not request.config.getoption("--upload-tests"):
        pytest.skip("--upload-tests not enabled")
    version = (Path(__file__).resolve().parents[2] / "VERSION").read_text(encoding="utf-8").strip()
    with web.ws() as owner:
        require(owner.accepted, "Browser refused")
        text, messages = owner.collect(0.5)
        owner.send(b'{"t":"open"}', opcode=1)
        more, later = owner.collect(0.5)
        hello = next((m for m in messages + later if m.get("t") == "hello"), None)
        require(hello is not None and hello.get("csrf"), "No CSRF token in the page snapshot")
        length = 600000
        sock = socket.create_connection((web.host, 80), timeout=5)
        # The request is sent in full but its body stops after 64 KiB, then the link drops.
        # It holds no firmware identity, so nothing could be installed even if it finished.
        head = (f"POST /api/update?target=firmware&version={version} HTTP/1.1\r\n"
                f"Host: {web.host}\r\nOrigin: http://{web.host}\r\n"
                f"Content-Type: application/octet-stream\r\nContent-Length: {length}\r\n"
                f"Cookie: usr_session={web.cookie}\r\n"
                f"X-CSRF-Token: {hello['csrf']}\r\nX-Image-SHA256: {'0' * 64}\r\n\r\n")
        sock.sendall(head.encode())
        try:
            sock.sendall(bytes(65536))
        except OSError:
            pass
        sock.close()
    # A dropped upload must give the modem back without a reboot.
    deadline = time.monotonic() + 20
    ok = False
    while time.monotonic() < deadline and not ok:
        time.sleep(1)
        try:
            ok = (web.request("/api/session", timeout=4)[0] == 200
                  and console.command("AT")[-1] == "OK")
        except OSError:
            pass
    require(ok, "The modem stayed busy after an interrupted upload")
    require(b"A. Status" in console.raw(b"AT*CONFIG\r"), "Menu unavailable after upload")
    console.raw(b"\x03", wait=0.2)


def test_browser_letter_lists_keep_geometry_separate_and_tests_serial_only(web):
    with web.ws() as peer:
        peer.enter()
        peer.send(b"\x0c")
        text, _ = peer.collect(0.4)
        require(b"A. Status" in text and b"E. Help" in text, "Incomplete browser list")
        require(b"\x1b" not in re.sub(rb"\x1b\[[0-9;]*m", b"", text), "Browser menu emitted cursor controls")
        for width in (80, 132, 255, 512):
            peer.send(f'{{"t":"size","cols":{width},"rows":25}}'.encode(), opcode=1)
            text, _ = peer.collect(0.2)
            require(not text, "Transport size repainted the browser list")
        peer.send(b"edfa")
        text, _ = peer.collect(0.4)
        require(b"Client Tests run on serial only" in text, "Browser ran a serial diagnostic")


def test_menu_readiness_tracks_partial_commands_and_terminal_prompts(web):
    with web.ws() as peer:
        _, messages = peer.open_terminal()
        ready = next((m for m in reversed(messages) if m.get("t") == "ready"), None)
        require(ready and ready.get("idle") is True and ready.get("rx") == 0,
                "No idle menu readiness on open")
        received = 0

        def send(data, idle):
            nonlocal received
            peer.send(data)
            received += len(data)
            text, messages = peer.collect(0.4)
            ready = next((m for m in reversed(messages) if m.get("t") == "ready"), None)
            require(ready is not None and ready.get("rx") == received and ready.get("idle") is idle,
                    f"Menu readiness: expected rx={received}, idle={idle}; got {ready}")
            return text

        send(b"ATQ0V1\r", True)
        send(b"AT", False)
        send(b"\x08\x08", False)  # Hayes AT prefix cannot be erased with Backspace.
        send(b"\r", True)
        send(b"AT*RESTART\r", False)
        send(b"n\r", True)
        text = send(b"AT*PHONEBOOK?\r", True)
        require(b"OK" in text, "Phonebook reply did not finish before the idle acknowledgement")
        send(b"ATQ1\r", True)
        peer.send(b"AT*NOTACOMMAND\r")
        received += len(b"AT*NOTACOMMAND\r")
        text, messages = peer.collect(0.4)
        failed = next((m for m in reversed(messages) if m.get("t") == "ready"), {})
        require(failed.get("idle") is True and failed.get("rx") == received and
                failed.get("errors", 0) > ready.get("errors", 0) and b"ERROR" not in text,
                "Quiet command failure was not acknowledged")
        send(b"ATQ0\r", True)
        send(b"AT*CONFIG\r", False)
        send(b"q\r", True)
        peer.send(b'{"t":"hangup"}', opcode=1)
        _, messages = peer.collect(0.4)
        ready = next((m for m in reversed(messages) if m.get("t") == "ready"), {})
        require(ready.get("idle") is True and ready.get("rx") == 0,
                "An idle hang-up did not acknowledge the reset input count")


def test_selected_model_name_updates_status_observers(web, console):
    original = console.command('AT*MODEL?')[0].split()[0]
    models = {'14400': 'US Robotics Sportster 14.4k', '28800': 'US Robotics Sportster 28.8k',
              '33600': 'US Robotics Sportster 33.6k', '56000': 'US Robotics Sportster 56k'}
    require(original in models, 'Unknown current model')
    try:
        with web.ws() as first, web.ws() as second:
            _, initial = first.collect(0.3)
            require(any(m.get('model') == models[original] for m in initial), 'Initial model name missing')
            second.collect(0.2)
            previous = original
            for speed, name in models.items():
                require(console.command('AT*MODEL=' + speed)[-1] == 'OK', 'Model change failed')
                # Re-selecting the current model produces no changed snapshot.
                if speed == previous:
                    continue
                for peer in (first, second):
                    _, messages = peer.collect(0.4)
                    require(any(m.get('model') == name for m in messages), 'Live model name not published')
                previous = speed
    finally:
        require(console.command('AT*MODEL=' + original)[-1] == 'OK', 'Original model restore failed')


def test_terminal_at_commands_config_exit_and_serial_only_services(web):
    with web.ws() as peer:
        _, messages = peer.open_terminal()
        require(any(m.get("state") == "terminal" for m in messages), "Terminal access missing")
        peer.send(b"AT\rATI\r")
        text, _ = peer.collect(0.5)
        require(b"OK" in text, "Browser AT parser did not answer")
        peer.send(b"AT*CONFIG\r")
        text, _ = peer.collect(0.4)
        require(b"A. Status" in text, "AT*CONFIG did not open")
        peer.send(b"q\r")
        peer.collect(0.3)
        peer.send(b"AT\r")
        text, _ = peer.collect(0.3)
        require(b"OK" in text, "Leaving configuration lost terminal ownership")
        for command in (b"ATD666\r", b"ATD667\r", b"ATD5517546\r", b"ATD5517545\r",
                        b"AT*TEST SUMMARY\r", b"AT*SX=TEST\r", b"AT*RX\r"):
            peer.send(command)
            text, _ = peer.collect(0.3)
            require(b"ERROR" in text and b"CONNECT" not in text, "Browser ran a serial service")
        peer.send(b"AT*STYLE=UTF8\rAT*STYLE?\r")
        text, _ = peer.collect(0.3)
        require(b"UTF8" in text and b"ERROR" not in text, "UTF8 style unavailable")
        peer.send(b"AT*STYLE=CP437\rAT*STYLE?\r")
        text, _ = peer.collect(0.3)
        require(b"CP437" in text and b"ERROR" not in text, "CP437 style unavailable")
        peer.send(b"AT*SIZE=160,60\rAT*SIZE?\r")
        text, _ = peer.collect(0.3)
        require(b"160,60" in text and b"ERROR" not in text, "Browser transport size unavailable")


def test_terminal_large_command_paste_and_fragmented_input(web):
    with web.ws() as peer:
        peer.open_terminal()
        peer.send(b"AT\r" * 3000)
        text, _ = peer.collect(8)
        require(text.count(b"OK\r\n") == 3000, "Large paste lost command replies")
        peer.send(b"AT", final=False)
        peer.send(b'{"t":"ping"}', opcode=1)
        peer.send(b"\r", opcode=0)
        text, messages = peer.collect(0.4)
        require(b"OK" in text and any(m.get("t") == "pong" for m in messages),
                "Fragmented input or interleaved heartbeat failed")


def test_terminal_disconnect_forgets_partial_command_and_repeat(web):
    with web.ws() as peer:
        peer.open_terminal()
        peer.send(b"ATQ1")
        peer.collect(0.2)
    with web.ws() as peer:
        peer.open_terminal()
        peer.send(b"\rAT\r")
        text, _ = peer.collect(0.3)
        require(b"OK" in text, "New terminal inherited partial command")


def test_terminal_hardware_bridge_consumes_prefetched_tail(web, rs232):
    with web.ws() as peer:
        peer.open_terminal()
        rs232.ser.reset_input_buffer()
        peer.send(b"AT*HWBRIDGE\rprefetched-browser-input")
        text, _ = peer.collect(0.4)
        require(b"Bridge:" in text, "Direct browser hardware test did not start")
        received = bytearray()
        end = time.monotonic() + 1
        while time.monotonic() < end:
            received.extend(rs232.ser.read(rs232.ser.in_waiting or 1))
        require(b"prefetched-browser-input" in received, "Hardware test lost prefetched input")
        peer.send(b"\x18")
        peer.collect(0.3)
        peer.send(b"AT\r")
        text, _ = peer.collect(0.3)
        require(b"OK" in text, "Direct hardware test did not return to command mode")


def test_browser_generated_password_reaches_requesting_terminal(web, console, request):
    if not request.config.getoption("--device-tests"):
        pytest.skip("--device-tests not enabled")
    original = os.environ["USR_MODEM_PASSWORD"]
    try:
        with web.ws() as peer:
            peer.open_terminal()
            peer.send(b"AT*PASS\r")
            text, _ = peer.collect(1, allow_close=True)
            require(re.search(rb"Device password: [^\r\n]{12}\r\n", text) is not None,
                    "Generated password did not reach authenticated terminal")
            require(b"OK" in text, "Browser password generation failed")
            require(peer.closed, "Password change did not close authenticated terminal")
    finally:
        require(console.command("AT*PASS=" + original)[-1] == "OK", "Password restore failed")
        time.sleep(0.3)
    with Web(web.host) as again:
        require(again.request("/api/session")[0] == 200, "Restored browser login failed")
    require(web.request("/api/auth")[0] == 401, "A password change left the old session logged in")


@contextlib.contextmanager
def terminal_bbs(port):
    """Negotiate NAWS, record unescaped input and keep the local carrier up."""
    listener = socket.socket()
    listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    listener.bind(("0.0.0.0", port))
    listener.listen(1)
    listener.settimeout(0.2)
    received, sizes, connections, errors = bytearray(), [], [], []
    ready, closed, stop = threading.Event(), threading.Event(), threading.Event()

    def run():
        try:
            while not stop.is_set():
                try:
                    connection, _ = listener.accept()
                    break
                except socket.timeout:
                    continue
            else:
                return
            with connection:
                connections.append(connection)
                connection.settimeout(0.2)
                connection.sendall(bytes([255, 253, 31]) + b"local-bbs-ready\r\n")
                ready.set()
                state, sub = 0, bytearray()
                while not stop.is_set():
                    try:
                        data = connection.recv(4096)
                    except socket.timeout:
                        continue
                    if not data:
                        closed.set()
                        return
                    for b in data:
                        if state == 0:
                            if b == 255:
                                state = 1
                            else:
                                received.append(b)
                        elif state == 1:
                            if b == 255:
                                received.append(b)
                                state = 0
                            elif b in (251, 252, 253, 254):
                                state = 2
                            elif b == 250:
                                sub.clear()
                                state = 3
                            else:
                                state = 0
                        elif state == 2:
                            state = 0
                        elif state == 3:
                            if b == 255:
                                state = 4
                            else:
                                sub.append(b)
                        elif state == 4:
                            if b == 240:
                                if len(sub) == 5 and sub[0] == 31:
                                    sizes.append(struct.unpack("!HH", sub[1:]))
                                state = 0
                            else:
                                if b == 255:
                                    sub.append(b)
                                state = 3
        except OSError:
            if not stop.is_set():
                errors.append("local Telnet socket failed")

    worker = threading.Thread(target=run, daemon=True)
    worker.start()
    try:
        yield listener.getsockname()[1], received, sizes, ready, closed, connections
    finally:
        stop.set()
        worker.join(timeout=2)
        listener.close()
        require(not errors, "Local Telnet peer failed")


@pytest.mark.parametrize('command_mode', [False, True])
def test_hangup_control_ends_only_owner_call_and_keeps_terminal(web, console, request, command_mode):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
        route.connect((web.host, 80))
        address = route.getsockname()[0]
    profile = ' '.join(console.command('ATI4'))
    old_m = re.search(r'(?<!\S)M(\d+)', profile)
    require(console.command('ATM0')[-1] == 'OK', 'Cannot mute test dial')
    try:
        with terminal_bbs(request.config.getoption('--call-server-port')) as (port, _, _, ready, closed, _):
            with web.ws() as owner:
                owner.open_terminal()
                owner.send(f'ATD{address}:{port}\r'.encode())
                text, messages = owner.collect(3)
                require(ready.is_set() and b'CONNECT' in text, 'Local browser dial failed')
                require(any(m.get('state') == 'connected' and m.get('browser') for m in messages),
                        'Browser call ownership missing')
                with web.ws() as observer:
                    observer.collect(0.2)
                    observer.send(b'{"t":"hangup"}', opcode=1)
                    observer.collect(0.2)
                    require(not closed.is_set(), 'Observer hung up owner call')
                if command_mode:
                    time.sleep(1.1)
                    owner.send(b'+++')
                    owner.collect(1.4)
                owner.send(b'{"t":"hangup"}', opcode=1)
                _, messages = owner.collect(0.5)
                require(closed.wait(1), 'Hang-up control did not close call')
                require(any(m.get('t') == 'ready' and m.get('idle') and m.get('rx') == 0 for m in messages),
                        'Hang-up did not restore idle terminal readiness')
                owner.send(b'AT\r')
                text, _ = owner.collect(0.3)
                require(b'OK' in text, 'Hang-up lost terminal ownership')
    finally:
        if old_m:
            console.command('ATM' + old_m.group(1))


def test_terminal_dials_telnet_paste_resize_escape_and_disconnect(web, console, request):
    address = request.config.getoption("--call-server-ip")
    if not address:
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as route:
            route.connect((web.host, 80))
            address = route.getsockname()[0]
    throttle = console.command("AT*THROTTLE?")[0]
    profile = " ".join(console.command("ATI4"))
    old_m = re.search(r"(?<!\S)M(\d+)", profile)
    require(console.command("ATM0")[-1] == "OK", "Cannot mute test dial")
    require(console.command("AT*THROTTLE=OFF")[-1] == "OK", "Cannot set test speed")
    try:
        with terminal_bbs(request.config.getoption("--call-server-port")) as (port, received, sizes, ready, closed, connections):
            with web.ws() as peer:
                peer.open_terminal()
                peer.send(b'{"t":"size","cols":160,"rows":60}', opcode=1)
                peer.send(f"ATD{address}:{port}\r".encode())
                text, messages = peer.collect(4)
                require(ready.is_set() and b"CONNECT" in text and b"local-bbs-ready" in text,
                        "Browser Telnet dial failed (check local listener reachability)")
                require((160, 60) in sizes, "Initial NAWS ignored terminal size")
                require(any(m.get("state") == "connected" for m in messages), "Call state missing")
                with web.ws() as observer:
                    observer.collect(0.2)
                    observer.send(b'{"t":"open"}', opcode=1)
                    _, messages = observer.collect(0.2)
                    require(not any(m.get("edit") for m in messages), "Observer stole terminal")
                    observer.send(b"observer-must-not-reach-bbs")
                    paste = b"large-paste-" * 4000
                    peer.send(paste)
                    peer.collect(3)
                    require(bytes(received) == paste, "BBS paste lost bytes or accepted observer input")
                peer.send(b'{"t":"size","cols":255,"rows":511}', opcode=1)
                peer.collect(0.5)
                require((255, 511) in sizes, "Live NAWS resize failed")
                connections[0].sendall(b"\x01\xda\xc4\xbf\r\n" + b"fast-output-" * 6000)
                text, _ = peer.collect(4)
                require(text.startswith("\u263a\u250c\u2500\u2510\r\n".encode()) and
                        text.endswith(b"fast-output-" * 6000), "Fast BBS output or CP437 glyphs lost bytes")
                time.sleep(1.1)
                peer.send(b"+++")
                text, _ = peer.collect(1.4)
                require(b"OK" in text and not closed.is_set(), "+++ did not keep carrier in command mode")
                peer.send(b"AT*STYLE=UTF8\rATO\r")
                text, _ = peer.collect(0.4)
                require(b"CONNECT" in text, "ATO did not resume BBS")
                connections[0].sendall("UTF8 caf\u00e9 \u2713\r\n".encode())
                text, _ = peer.collect(0.4)
                require("UTF8 caf\u00e9 \u2713".encode() in text, "UTF8 BBS output was translated")
            require(closed.wait(2), "Closing browser did not hang up BBS")
        with terminal_bbs(request.config.getoption("--call-server-port")) as (port, _, _, ready, closed, _):
            with web.ws() as peer:
                peer.open_terminal()
                peer.send(f"AT*DIAL={address}:{port}\r".encode())
                text, _ = peer.collect(4)
                require(ready.is_set() and b"CONNECT" in text, "AT*DIAL did not dial on browser session")
                time.sleep(1.1)
                peer.send(b"+++")
                peer.collect(1.4)
                peer.send(b"ATH\r")
                text, _ = peer.collect(0.3)
                require(b"OK" in text and closed.wait(1), "Browser ATH did not hang up")
    finally:
        console.command("AT*THROTTLE=" + throttle)
        if old_m:
            console.command("ATM" + old_m.group(1))


# --- Login sessions -------------------------------------------------------------------------

def log_in_again(web, seconds=30):
    """Logs the fixture's browser in again, waiting for the web server after a restart."""
    deadline = time.monotonic() + seconds
    while True:
        with contextlib.suppress(OSError):
            if web.login() == (303, "/"):
                return
        require(time.monotonic() < deadline, "Could not log in again")
        time.sleep(1.1)

def console_output(console, seconds):
    """What the maintenance console prints in the next `seconds`, unfiltered."""
    data = b""
    end = time.monotonic() + seconds
    while time.monotonic() < end:
        data += console.ser.read(console.ser.in_waiting or 1)
    return data


def test_each_web_login_is_logged_once_on_the_console(web, console):
    console_output(console, 0.5)
    with Web(web.host) as browser:
        text = console_output(console, 2.0)
        require(text.count(b"Login: web admin from ") == 1, "Web login not logged exactly once")
        require(re.search(rb"Login: web admin from \d+\.\d+\.\d+\.\d+\r\n", text), "Login line lacks the peer")
        for secret in (browser.cookie, browser.auth()["csrf"], os.environ["USR_MODEM_PASSWORD"]):
            require(secret.encode() not in text, "Login line holds a secret")
        # Routine requests, a reconnecting socket and a failed login are not logins.
        browser.request("/api/auth")
        with browser.ws():
            pass
        with browser.ws():
            pass
        time.sleep(1.1)
        require(Web(web.host, login=False).login(password="not-the-password")[1] == "/login?error=invalid", "Wrong password")
        require(b"Login: web" not in console_output(console, 1.5), "A non-login was logged as a login")


def test_logout_ends_this_browsers_tabs_and_sockets_only(web):
    with Web(web.host) as other:
        with web.ws() as owner, web.ws() as observer, other.ws() as elsewhere:
            owner.open_terminal()
            observer.collect(0.3)
            elsewhere.collect(0.3)
            spare = web.ticket()  # a ticket issued before the logout dies with the session
            csrf = web.auth()["csrf"]
            code, headers, _ = web.request("/api/logout", data=b"", headers={"Origin": web.base, "X-CSRF-Token": csrf})
            require(code == 204 and session_cookie(headers) == "", "Logout did not expire the cookie")
            for peer in (owner, observer):
                peer.collect(1.0, allow_close=True)
                require(peer.closed, "Logout left a socket of the session open")
            elsewhere.collect(0.5)
            require(not elsewhere.closed, "Logout closed another browser's socket")
            require(other.request("/api/session")[0] == 200, "Logout ended another browser's session")
            require(web.request("/api/session")[0] == 401, "The logged-out session still works")
            late = WS(web.host, spare)
            try:
                require(not late.accepted, "A ticket outlived its session")
            finally:
                late.close()
            code, _, _ = web.request("/api/logout", data=b"", headers={"Origin": web.base, "X-CSRF-Token": csrf})
            require(code == 204, "A repeated logout failed")
    code, location = web.login()
    require(code == 303 and location == "/", "Login after logout failed")


def test_listener_restart_keeps_sessions(web, console):
    try:
        require(console.command("AT*WEB=OFF")[-1] == "OK", "Web server did not stop")
        time.sleep(1)
    finally:
        require(console.command("AT*WEB=ON")[-1] == "OK", "Web server did not restart")
    deadline = time.monotonic() + 10
    code = None
    while time.monotonic() < deadline and code != 200:
        with contextlib.suppress(OSError):
            code = web.request("/api/auth", timeout=4)[0]
        time.sleep(0.5)
    require(code == 200, "A listener restart ended the session")


def test_restart_ends_every_session(web, console):
    require(web.request("/api/auth")[0] == 200, "No session to end")
    console.restart()
    deadline = time.monotonic() + 30
    code = None
    while time.monotonic() < deadline and code is None:
        with contextlib.suppress(OSError):
            code = web.request("/api/auth", timeout=4)[0]
        time.sleep(0.5)
    require(code == 401, "A session survived a restart")


def test_a_ninth_browser_waits_without_ending_anyone(web):
    browsers = []
    try:
        busy = None
        for _ in range(9):
            browser = Web(web.host, login=False)
            code, location = browser.login()
            if location == "/login?error=busy":
                busy = browser
                break
            require(code == 303 and location == "/", "Login refused before the sessions were full")
            browsers.append(browser)
        require(busy is not None, "More than eight sessions were accepted")
        require(busy.cookie is None, "A refused login set a cookie")
        require(web.request("/api/auth")[0] == 200 and all(b.auth() for b in browsers),
                "A full table ended someone else's session")
        browsers.pop().logout()
        code, location = busy.login()
        require(code == 303 and location == "/", "A freed slot was not reused")
        browsers.append(busy)
    finally:
        for browser in browsers:
            browser.logout()


def test_upload_after_logout_finishes_and_releases_the_worker(web, console, request):
    """An admitted upload outlives its session; nothing else does. The current build is sent
    with a wrong hash, so it streams to the end and is then rejected: nothing is installed."""
    if not request.config.getoption("--upload-tests"):
        pytest.skip("--upload-tests not enabled")
    root = Path(__file__).resolve().parents[2]
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    image = (root / ".pio/build/s3/firmware.bin").read_bytes()
    require(f"USRMODEM/1/FIRMWARE/{version}\n".encode() in image, "Build normal firmware first")
    length = len(image)
    with web.ws() as owner:
        _, messages = owner.enter()
        hello = next((m for m in messages if m.get("t") == "hello"), None)
        require(hello is not None, "No CSRF token in the page snapshot")
        sock = socket.create_connection((web.host, 80), timeout=10)
        head = (f"POST /api/update?target=firmware&version={version} HTTP/1.1\r\n"
                f"Host: {web.host}\r\nOrigin: http://{web.host}\r\n"
                f"Content-Type: application/octet-stream\r\nContent-Length: {length}\r\n"
                f"Cookie: usr_session={web.cookie}\r\n"
                f"X-CSRF-Token: {hello['csrf']}\r\nX-Image-SHA256: {'0' * 64}\r\n\r\n")
        sock.sendall(head.encode() + image[:65536])
        time.sleep(1)
        # While the upload holds the worker: a second upload is refused, logout still works.
        code, _, _ = web.request(f"/api/update?target=firmware&version={version}", data=bytes(24), headers={
            "Origin": web.base, "Content-Type": "application/octet-stream",
            "X-CSRF-Token": hello["csrf"], "X-Image-SHA256": "0" * 64})
        require(code in (403, 409), "A second upload was admitted")
        start = time.monotonic()
        csrf = web.auth()["csrf"]
        code, _, _ = web.request("/api/logout", data=b"", headers={"Origin": web.base, "X-CSRF-Token": csrf})
        require(code == 204 and time.monotonic() - start < 3, "Logout waited for the upload")
        owner.collect(1.0, allow_close=True)
        require(owner.closed, "Logout left the uploader's socket open")
        sock.sendall(image[65536:])
        sock.settimeout(30)
        reply = b""
        with contextlib.suppress(OSError):
            while b"\r\n\r\n" not in reply and (chunk := sock.recv(4096)):
                reply += chunk
        sock.close()
    require(b" 400 " in reply.split(b"\r\n")[0], "The admitted upload did not finish with its own reply")
    deadline = time.monotonic() + 20
    ok = False
    while time.monotonic() < deadline and not ok:
        time.sleep(1)
        with contextlib.suppress(OSError):
            ok = console.command("AT")[-1] == "OK"
    require(ok, "The modem stayed busy after the upload")
    web.login()
    with web.ws() as again:
        require(again.accepted, "The web server did not serve the terminal after the upload")
