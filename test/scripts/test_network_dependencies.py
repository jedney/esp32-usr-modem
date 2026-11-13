"""Pinned source preparation must reject corrupted downloads and unsafe archives."""
import importlib.util
import io
from pathlib import Path
import tarfile

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('network_dependencies', ROOT / 'scripts/network_dependencies.py')
dependencies = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dependencies)


def test_corrupt_cached_source_fails_before_extraction(tmp_path):
    cache = tmp_path / '.pio/network-archives'
    cache.mkdir(parents=True)
    (cache / 'arduino_tinyusb.tar.gz').write_bytes(b'corrupted')
    with pytest.raises(ValueError, match='SHA-256'):
        dependencies.prepare(tmp_path)
    assert not (tmp_path / '.pio/network-deps/arduino_tinyusb/CMakeLists.txt').exists()
    assert not (cache / 'arduino_tinyusb.tar.gz').exists()  # bad cached copy is removed


class _Response(io.BytesIO):
    def __enter__(self):
        return self

    def __exit__(self, *args):
        return False


def test_bad_download_is_not_cached(tmp_path, monkeypatch):
    monkeypatch.setattr(dependencies, 'urlopen', lambda *a, **k: _Response(b'wrong body'))
    archive = tmp_path / 'dep.tar.gz'
    with pytest.raises(ValueError, match='SHA-256'):
        dependencies.fetch(archive, 'owner/repo', 'rev', '0' * 64, 'dep')
    assert not archive.exists()


def test_good_download_is_cached_after_check(tmp_path, monkeypatch):
    body = b'good body'
    monkeypatch.setattr(dependencies, 'urlopen', lambda *a, **k: _Response(body))
    archive = tmp_path / 'dep.tar.gz'
    digest = dependencies.hashlib.sha256(body).hexdigest()
    assert dependencies.fetch(archive, 'owner/repo', 'rev', digest, 'dep') == body
    assert archive.read_bytes() == body


def test_folder_extracts_only_that_folder(tmp_path):
    archive = io.BytesIO()
    with tarfile.open(fileobj=archive, mode='w:gz') as output:
        for name in ('repo-abc/src/tusb.c', 'repo-abc/src/class/cdc.c', 'repo-abc/examples/x.c',
                     'repo-abc/srcx/no.c'):
            item = tarfile.TarInfo(name)
            item.size = 1
            output.addfile(item, io.BytesIO(b'x'))
    dependencies.unpack(archive.getvalue(), tmp_path / 'dep', 'src')
    found = sorted(p.relative_to(tmp_path / 'dep').as_posix()
                   for p in (tmp_path / 'dep').rglob('*') if p.is_file())
    assert found == ['class/cdc.c', 'tusb.c']


def test_tinyusb_sources_are_pinned():
    names = {source[0]: source for source in dependencies.SOURCES}
    assert names['tinyusb'][2] == 'a0e5626bc50d484a23f33000c48082179f0cc2dd'
    assert names['arduino_tinyusb'][4] == 'components/arduino_tinyusb'
    assert (ROOT / 'scripts/arduino_tinyusb/CMakeLists.txt').exists()


@pytest.mark.parametrize('name', ['repo/../../escaped', 'repo/../escaped'])
def test_dependency_archive_cannot_escape_its_directory(tmp_path, name):
    archive = io.BytesIO()
    with tarfile.open(fileobj=archive, mode='w:gz') as output:
        item = tarfile.TarInfo(name)
        item.size = 1
        output.addfile(item, io.BytesIO(b'x'))
    with pytest.raises(ValueError, match='escapes'):
        dependencies.unpack(archive.getvalue(), tmp_path / 'dependency')
    assert not (tmp_path / 'escaped').exists()
