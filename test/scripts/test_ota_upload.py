"""Host uploader: environment precedence and no password disclosure on failure."""

import importlib.util
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("ota_upload", ROOT / "scripts" / "ota_upload.py")
ota = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ota)


def test_env_file_quoted_spaces_and_symbols(tmp_path, monkeypatch):
    path = tmp_path / ".env"
    path.write_text("# ignored\nexport USR_MODEM_OTA_PASSWORD=' a b $ & ! '\n", encoding="utf-8")
    monkeypatch.delenv(ota.KEY, raising=False)
    assert ota.password_from(path) == " a b $ & ! "
    monkeypatch.setenv(ota.KEY, "environment")
    assert ota.password_from(path) == "environment"
    monkeypatch.setenv(ota.KEY, "")
    assert ota.password_from(path) == ""


def test_missing_secret_never_invokes_uploader(tmp_path, capsys):
    assert ota.upload(tmp_path / "missing.py", "modem.local", "firmware.bin", "") == 1
    assert "set USR_MODEM_OTA_PASSWORD" in capsys.readouterr().out


def test_password_passed_in_memory_and_exceptions_are_redacted(tmp_path, capsys):
    module = mock.Mock(FLASH=0, SPIFFS=100)
    password = " spaces $ ' \" & "
    module.serve.return_value = 0
    with mock.patch.object(ota.importlib.util, "spec_from_file_location", return_value=mock.Mock()), \
            mock.patch.object(ota.importlib.util, "module_from_spec", return_value=module):
        assert ota.upload(tmp_path / "espota.py", "modem.local", "firmware.bin", password) == 0
        args = module.serve.call_args.args
        assert args[4] == password and args[6] == 0
        assert args[3] == 32320
        module.serve.side_effect = RuntimeError(password)
        assert ota.upload(tmp_path / "espota.py", "modem.local", "fs.bin", password, True) == 1
        assert module.serve.call_args.args[6] == 100
    assert password not in capsys.readouterr().out
