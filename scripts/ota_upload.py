"""OTA upload to the modem with ArduinoOTA's PBKDF2-HMAC-SHA256 sign-in, and nothing older.

The device password comes from the environment or .env and stays in this process: it is
never put on a command line, printed or logged. Only its SHA-256 is used, as the modem
stores it (ArduinoOTA's setPasswordHash()).

Protocol (ArduinoOTA in Arduino core 3.3):
1. UDP to the modem's port 3232: "<command> <callback port> <size> <image MD5>\\n", where
   command is 0 for the app and 100 for the file system. The modem answers
   "AUTH <nonce>" with a 64-character nonce.
2. UDP: "200 <cnonce> <response>\\n". key = PBKDF2-HMAC-SHA256(password = SHA-256 hex of the
   device password, salt = "nonce:cnonce", 10000 rounds), response = SHA-256 hex of
   "key hex:nonce:cnonce". The modem answers "OK" or "Authentication Failed".
3. The modem connects to the callback port over TCP. The image goes in chunks; the modem
   answers each read with the number of bytes written, and "OK" once the image checks out.
"""

import hashlib
import os
import secrets
import socket
import sys

KEY = "USR_MODEM_PASSWORD"
OTA_PORT = 3232
CALLBACK_PORT = 32320
FLASH, FILESYSTEM, AUTH = 0, 100, 200
ITERATIONS = 10000
CHUNK = 1024
INVITE_TRIES = 10
TIMEOUT = 10.0  # seconds for each answer
RESULT_TIMEOUT = 60.0  # seconds for the final "OK" (the modem checks the whole image)


def read_env_file(path):
    values = {}
    try:
        raw_bytes = path.read_bytes()
    except OSError:
        return values
    if raw_bytes[:2] in (b"\xff\xfe", b"\xfe\xff"):
        raise ValueError(f"{path.name} is saved as UTF-16. Save it as UTF-8 (no BOM needed).")
    try:
        text = raw_bytes.decode("utf-8-sig")
    except UnicodeDecodeError:
        raise ValueError(f"{path.name} is not valid UTF-8. Save it as UTF-8.") from None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if key.startswith("export "):
            key = key[len("export "):].strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values


def password_from(path):
    return os.environ.get(KEY, read_env_file(path).get(KEY, ""))


def auth_response(password, nonce, cnonce):
    """The answer to the modem's challenge (step 2 above)."""
    password_hash = hashlib.sha256(password.encode()).hexdigest()
    salt = f"{nonce}:{cnonce}"
    key = hashlib.pbkdf2_hmac("sha256", password_hash.encode(), salt.encode(), ITERATIONS).hex()
    return hashlib.sha256(f"{key}:{nonce}:{cnonce}".encode()).hexdigest()


def _is_hex64(text):
    return len(text) == 64 and all(c in "0123456789abcdefABCDEF" for c in text)


def _printable(data):
    text = data.decode("ascii", "replace")
    return "".join(c if c.isprintable() else "?" for c in text).strip()[:80]


class _Refused(Exception):
    """An upload step failed; the message is fixed text or the modem's own words."""


def _invite(udp, address, message):
    for _ in range(INVITE_TRIES):
        udp.sendto(message, address)
        try:
            data, _ = udp.recvfrom(128)
            return data
        except socket.timeout:
            continue
    raise _Refused("No answer from the modem. Check Wi-Fi and that OTA is open (AT*OTA).")


def _sign_in(udp, address, password, answer):
    words = answer.decode("ascii", "replace").split()
    if answer.strip() == b"OK":
        # The modem always has a device password; an open OTA port is not this firmware.
        raise _Refused("The modem accepted an upload without a password. Upload refused.")
    if len(words) != 2 or words[0] != "AUTH":
        raise _Refused(f"Unexpected answer from the modem: {_printable(answer)}")
    nonce = words[1]
    if not _is_hex64(nonce):
        raise _Refused("The modem asked for an older OTA sign-in. Only SHA-256 is supported.")
    cnonce = secrets.token_hex(32)
    response = auth_response(password, nonce, cnonce)
    udp.sendto(f"{AUTH} {cnonce} {response}\n".encode(), address)
    try:
        data, _ = udp.recvfrom(128)
    except socket.timeout:
        raise _Refused("No answer to the sign-in.") from None
    if data.strip() != b"OK":
        raise _Refused("Wrong device password (the modem said: " + _printable(data) + ").")


def _send_image(connection, image):
    total = len(image)
    shown = -1
    replies = b""
    connection.settimeout(TIMEOUT)
    for offset in range(0, total, CHUNK):
        connection.sendall(image[offset:offset + CHUNK])
        reply = connection.recv(32)
        if not reply:
            raise _Refused("The modem closed the connection during the upload.")
        replies = (replies + reply)[-64:]
        percent = (offset + CHUNK) * 100 // total if total else 100
        if percent // 10 != shown:
            shown = percent // 10
            sys.stderr.write(f"Uploading: {min(percent, 100)}%\n")
    connection.settimeout(RESULT_TIMEOUT)
    while b"OK" not in replies:
        reply = connection.recv(64)
        if not reply:
            break
        replies = (replies + reply)[-256:]
    if b"OK" not in replies:
        text = _printable(replies.lstrip(b"0123456789"))
        raise _Refused("The modem did not accept the image" + (f": {text}" if text else "."))


def upload(host, image_path, password, filesystem=False, port=OTA_PORT,
           callback_port=CALLBACK_PORT):
    """Upload one image. Returns 0 on success and 1 on any failure."""
    if not password:
        print(f"OTA refused: set {KEY} in .env or the environment.")
        return 1
    listener = udp = connection = None
    try:
        with open(image_path, "rb") as f:
            image = f.read()
        try:
            address = socket.getaddrinfo(host, port, socket.AF_INET, socket.SOCK_DGRAM)[0][4]
        except OSError:
            raise _Refused(f"Cannot find {host} on the network.") from None
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        listener.bind(("0.0.0.0", callback_port))
        listener.listen(1)
        callback = listener.getsockname()[1]
        udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        udp.settimeout(TIMEOUT)
        command = FILESYSTEM if filesystem else FLASH
        md5 = hashlib.md5(image).hexdigest()  # ArduinoOTA's image check, not the sign-in
        answer = _invite(udp, address, f"{command} {callback} {len(image)} {md5}\n".encode())
        _sign_in(udp, address, password, answer)
        listener.settimeout(TIMEOUT)
        try:
            connection, _ = listener.accept()
        except socket.timeout:
            raise _Refused("The modem did not connect back. Check the PC firewall "
                           f"(TCP port {callback}).") from None
        _send_image(connection, image)
        sys.stderr.write("OTA upload done. The modem restarts.\n")
        return 0
    except _Refused as refused:
        print(f"OTA upload failed. {refused}")
        return 1
    except Exception as error:
        # Never print an exception's text or a traceback here: it could hold an argument.
        print(f"OTA upload failed ({type(error).__name__}). Check Wi-Fi, OTA mode and the "
              "device password.")
        return 1
    finally:
        for item in (connection, udp, listener):
            if item is not None:
                item.close()
