"""Package the supplied PCM handshake recordings without changing their audio.

Inputs are already mono 8000 Hz PCM16 WAVs. See credits/LICENSES.md for provenance.
"""
from pathlib import Path
import shutil
import wave

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "sound"
DEST = ROOT / "data" / "sounds"
SPEEDS = (1200, 2400, 9600, 14400, 28800, 33600, 56000)


def build():
    # Check every input before replacing any packaged recording.
    for speed in SPEEDS:
        source = SOURCES / f"{speed}bps.wav"
        with wave.open(str(source), "rb") as recording:
            if (recording.getnchannels(), recording.getsampwidth(),
                    recording.getframerate(), recording.getcomptype()) != (1, 2, 8000, "NONE"):
                raise ValueError(f"{source}: expected mono 8000 Hz PCM16 WAV")
            if not recording.getnframes():
                raise ValueError(f"{source}: recording is empty")
    DEST.mkdir(parents=True, exist_ok=True)
    for speed in SPEEDS:
        shutil.copyfile(SOURCES / f"{speed}bps.wav", DEST / f"handshake-{speed}.wav")


if __name__ == "__main__":
    build()
