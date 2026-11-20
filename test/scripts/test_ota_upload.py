"""OTA uploader: .env reading, the SHA-256 sign-in against a fake modem, no password disclosure."""

import hashlib
import importlib.util
import socket
import threading
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ota_upload", ROOT / "scripts" / "ota_upload.py")
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)

PASSWORD = "Pa ss$'\"&w0rd"


def expected_response(stored_hash, nonce, cnonce):
    """ArduinoOTAClass::_onRx() in Arduino core 3.3.12, written out step by step."""
    salt = nonce + ":" + cnonce
    derived = hashlib.pbkdf2_hmac("sha256", stored_hash.encode(), salt.encode(), 10000).hex()
    return hashlib.sha256((derived + ":" + nonce + ":" + cnonce).encode()).hexdigest()


class FakeModem(threading.Thread):
    """Answers like ArduinoOTA with setPasswordHash(SHA-256 of the device password)."""

    def __init__(self, password=PASSWORD, nonce="ab" * 32, accept_image=True):
        super().__init__(daemon=True)
        self.stored = hashlib.sha256(password.encode()).hexdigest()
        self.nonce = nonce
        self.accept_image = accept_image
        self.udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.udp.bind(("127.0.0.1", 0))
        self.udp.settimeout(5)
        self.port = self.udp.getsockname()[1]
        self.invitations = []
        self.auths = []
        self.image = b""

    def run(self):
        try:
            data, peer = self.udp.recvfrom(256)
            self.invitations.append(data)
            command, callback, size, md5 = data.decode().split()
            self.udp.sendto(f"AUTH {self.nonce}".encode(), peer)
            if len(self.nonce) != 64:
                return
            data, peer = self.udp.recvfrom(256)
            self.auths.append(data)
            code, cnonce, response = data.decode().split()
            if code != "200" or response != expected_response(self.stored, self.nonce, cnonce):
                self.udp.sendto(b"Authentication Failed", peer)
                return
            self.udp.sendto(b"OK", peer)
            with socket.create_connection(("127.0.0.1", int(callback)), timeout=5) as tcp:
                while len(self.image) < int(size):
                    chunk = tcp.recv(1460)
                    if not chunk:
                        return
                    self.image += chunk
                    tcp.sendall(str(len(chunk)).encode())
                good = self.accept_image and hashlib.md5(self.image).hexdigest() == md5
                tcp.sendall(b"OK" if good else b"MD5 Check Failed")
        except (OSError, ValueError):
            pass
        finally:
            self.udp.close()


@pytest.fixture
def image(tmp_path):
    path = tmp_path / "firmware.bin"
    path.write_bytes(bytes(range(256)) * 20 + b"end")  # several chunks and a short last one
    return path


@pytest.fixture(autouse=True)
def quick(monkeypatch):
    monkeypatch.setattr(ota, "TIMEOUT", 2.0)
    monkeypatch.setattr(ota, "RESULT_TIMEOUT", 2.0)
    monkeypatch.setattr(ota, "INVITE_TRIES", 2)


def run(modem, image, password=PASSWORD, filesystem=False):
    modem.start()
    result = ota.upload("127.0.0.1", image, password, filesystem, port=modem.port,
                        callback_port=0)
    modem.join(5)
    return result


def test_auth_response_matches_the_firmware_check():
    nonce, cnonce = "0f" * 32, "a5" * 32
    stored = hashlib.sha256(PASSWORD.encode()).hexdigest()
    assert ota.auth_response(PASSWORD, nonce, cnonce) == expected_response(stored, nonce, cnonce)
    assert len(ota.auth_response(PASSWORD, nonce, cnonce)) == 64


def test_upload_signs_in_once_and_sends_the_whole_image(image, capsys):
    modem = FakeModem()
    assert run(modem, image) == 0
    assert modem.image == image.read_bytes()
    assert len(modem.invitations) == 1 and len(modem.auths) == 1
    command, _, size, md5 = modem.invitations[0].decode().split()
    assert command == "0" and int(size) == image.stat().st_size
    assert md5 == hashlib.md5(image.read_bytes()).hexdigest()
    out = capsys.readouterr()
    assert PASSWORD not in out.out + out.err


def test_filesystem_image_uses_command_100(image):
    modem = FakeModem()
    assert run(modem, image, filesystem=True) == 0
    assert modem.invitations[0].startswith(b"100 ")


def test_wrong_password_fails_without_a_second_attempt(image, capsys):
    modem = FakeModem(password="the-real-one")
    assert run(modem, image) == 1
    assert len(modem.invitations) == 1 and len(modem.auths) == 1  # no MD5 retry
    out = capsys.readouterr().out
    assert "Wrong device password" in out and PASSWORD not in out


def test_older_md5_challenge_is_refused_before_any_answer(image, capsys):
    modem = FakeModem(nonce="cd" * 16)  # a 32-character nonce
    assert run(modem, image) == 1
    assert modem.auths == []
    assert "Only SHA-256" in capsys.readouterr().out


def test_rejected_image_is_reported(image, capsys):
    modem = FakeModem(accept_image=False)
    assert run(modem, image) == 1
    assert "MD5 Check Failed" in capsys.readouterr().out


def test_missing_secret_never_contacts_the_modem(image, capsys):
    assert ota.upload("127.0.0.1", image, "") == 1
    assert "set USR_MODEM_PASSWORD" in capsys.readouterr().out


def test_unexpected_errors_print_no_arguments(tmp_path, capsys, monkeypatch):
    def explode(*args, **kwargs):
        raise RuntimeError(PASSWORD)

    monkeypatch.setattr(ota, "auth_response", explode)
    modem = FakeModem()
    image = tmp_path / "fs.bin"
    image.write_bytes(b"x" * 10)
    assert run(modem, image) == 1
    out = capsys.readouterr()
    assert "RuntimeError" in out.out
    assert PASSWORD not in out.out + out.err


def test_env_file_quoted_spaces_and_symbols(tmp_path, monkeypatch):
    path = tmp_path / ".env"
    path.write_text("# ignored\nexport USR_MODEM_PASSWORD=' a b $ & ! '\n", encoding="utf-8")
    monkeypatch.delenv(ota.KEY, raising=False)
    assert ota.password_from(path) == " a b $ & ! "
    monkeypatch.setenv(ota.KEY, "environment")
    assert ota.password_from(path) == "environment"
    monkeypatch.setenv(ota.KEY, "")
    assert ota.password_from(path) == ""


def test_env_file_with_bom_is_read(tmp_path):
    path = tmp_path / ".env"
    path.write_bytes(b"\xef\xbb\xbfUSR_MODEM_PASSWORD=secret\r\nUSR_MODEM_PIN_RXD=1\r\n")
    assert ota.read_env_file(path) == {"USR_MODEM_PASSWORD": "secret", "USR_MODEM_PIN_RXD": "1"}


def test_utf16_env_file_gives_clear_error_without_content(tmp_path):
    path = tmp_path / ".env"
    path.write_bytes("USR_MODEM_PASSWORD=hunter2\n".encode("utf-16"))
    with pytest.raises(ValueError, match="UTF-16") as error:
        ota.read_env_file(path)
    assert "hunter2" not in str(error.value)
