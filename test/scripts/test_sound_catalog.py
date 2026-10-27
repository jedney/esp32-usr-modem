"""Ensure every playback/transfer recording is actually packaged."""
from pathlib import Path
import re
import wave

ROOT = Path(__file__).resolve().parents[2]


def test_sound_catalog_matches_packaged_recordings():
    text = (ROOT / 'lib/sounds/src/catalog.h').read_text()
    assets = re.findall(r'\{(\d+), "(HS\d+\.WAV)", "(/sounds/handshake-\d+\.wav)"\}', text)
    assert len(assets) == 7
    assert [int(speed) for speed, _, _ in assets] == [1200, 2400, 9600, 14400, 28800, 33600, 56000]
    assert {Path(path).name for _, _, path in assets} == {p.name for p in (ROOT / 'data/sounds').glob('*.wav')}
    for speed, name, path in assets:
        assert name == 'HS' + speed + '.WAV'
        packaged = ROOT / 'data' / path.lstrip('/')
        assert packaged.read_bytes() == (ROOT / 'sound' / f'{speed}bps.wav').read_bytes()
        with wave.open(str(packaged), 'rb') as source:
            assert (source.getnchannels(), source.getsampwidth(), source.getframerate()) == (1, 2, 8000)
            assert source.getnframes() > 0
