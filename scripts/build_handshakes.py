"""Rebuild the licensed v0.8 PCM assets; requires FFmpeg on PATH.

See credits/LICENSES.md for provenance. Five filenames intentionally share a
generic fallback: they are not verified recordings of those negotiated speeds.
"""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SOURCES = ROOT / "sound" / "sources"
DEST = ROOT / "data" / "sounds"


def convert(source, target, *trim):
    subprocess.run([
        "ffmpeg", "-v", "error", "-y", "-i", str(SOURCES / source), *trim,
        "-map_metadata", "-1", "-ac", "1", "-ar", "8000", "-c:a", "pcm_s16le",
        "-fflags", "+bitexact", "-flags:a", "+bitexact", str(DEST / target),
    ], check=True)


if __name__ == "__main__":
    DEST.mkdir(parents=True, exist_ok=True)
    convert("modem1200-78657.mp3", "handshake-1200.wav")
    # The answer/handshake segment follows recorded dialing and ringing. Remove
    # those and trailing silence: firmware generates its own tone and digits.
    fallback = "handshake-2400.wav"
    convert("dial-up-modem-noises.ogg", fallback, "-ss", "9.9", "-t", "16.9")
    for speed in (14400, 28800, 33600, 56000):
        shutil.copyfile(DEST / fallback, DEST / f"handshake-{speed}.wav")
