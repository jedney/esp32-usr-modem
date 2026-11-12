"""bump_version.py writes LF line endings on every platform."""
import importlib.util
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("bump_version", ROOT / "scripts/bump_version.py")
bump_version = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bump_version)


def test_bump_writes_lf_even_if_files_had_crlf(tmp_path, monkeypatch):
    version, changes = tmp_path / "VERSION", tmp_path / "CHANGES.md"
    version.write_bytes(b"2.1\r\n")
    changes.write_bytes(b"# Changelog\r\n\r\n## [Unreleased]\r\n")
    monkeypatch.setattr(bump_version, "VERSION_FILE", version)
    monkeypatch.setattr(bump_version, "CHANGES_FILE", changes)
    assert bump_version.main(["bump_version.py", "minor", "Something"]) == 0
    assert version.read_bytes() == b"2.2\n"
    assert b"\r" not in changes.read_bytes()
    assert b"## [2.2]" in changes.read_bytes()
