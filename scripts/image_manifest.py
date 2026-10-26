"""Validate project image identity and write a release SHA-256 manifest.

Usage: python scripts/image_manifest.py .pio/build/s3
The raw .bin files remain directly usable by USB and browser upload.
"""
import hashlib
import json
import pathlib
import sys


def inspect(data: bytes, target: str, version: str) -> dict:
    marker = f"USRMODEM/1/{'FIRMWARE' if target == 'firmware' else 'LITTLEFS'}/{version}\n".encode()
    if marker not in data:
        raise ValueError("Missing project/version identity")
    if target == "firmware":
        if len(data) < 24 or len(data) > 0x400000 or data[0] != 0xE9 or data[12:14] != b"\x09\x00":
            raise ValueError("Invalid ESP32-S3 application")
    elif target != "littlefs" or len(data) != 0x7E0000:
        raise ValueError("Invalid LittleFS partition image")
    return {"project": "usr-modem", "target": target, "version": version,
            "length": len(data), "sha256": hashlib.sha256(data).hexdigest()}


def build(directory: pathlib.Path, version: str):
    images = [inspect((directory / f"{target}.bin").read_bytes(), target, version)
              for target in ("firmware", "littlefs")]
    (directory / "image-manifest.json").write_text(
        json.dumps({"format": 1, "images": images}, indent=2) + "\n", encoding="utf-8", newline="\n"
    )


if __name__ == "__main__":
    root = pathlib.Path(__file__).resolve().parents[1]
    build(pathlib.Path(sys.argv[1]), (root / "VERSION").read_text().strip())
