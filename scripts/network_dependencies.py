"""Prepare immutable LittleFS sources for the Arduino-as-IDF-component build.

These are the same revisions recorded in Arduino 2.0.17's SDK versions.txt.
Only the project's ignored .pio directory is written; installed packages stay intact.
"""

import hashlib
import io
from pathlib import Path, PurePosixPath
import tarfile
from urllib.request import urlopen

SOURCES = (
    ("esp_littlefs", "joltwallet/esp_littlefs",
     "41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317",
     "8240ad063e1fb5a5108a6fb1bb7e0b29c49745affa408a57a96ddc6bcd597570"),
    ("littlefs", "littlefs-project/littlefs",
     "f53a0cc961a8acac85f868b431d2f3e58e447ba3",
     "8172652dc546a408190f259c590d9c4badab3744ef3fa584335cadb822bb34e1"),
)


def unpack(data, destination):
    """Extract regular files only, stripping the archive's repository prefix."""
    destination = destination.resolve()
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            parts = PurePosixPath(member.name).parts
            if len(parts) < 2:
                continue
            relative = Path(*parts[1:])
            target = (destination / relative).resolve()
            if not target.is_relative_to(destination):
                raise ValueError("Dependency archive escapes destination")
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.extractfile(member) as source:
                    target.write_bytes(source.read())
            else:
                raise ValueError("Dependency archive contains a non-regular entry")


def prepare(root):
    cache = root / ".pio/network-archives"
    cache.mkdir(parents=True, exist_ok=True)
    base = root / ".pio/network-deps/esp_littlefs"
    for name, repository, revision, checksum in SOURCES:
        destination = base if name == "esp_littlefs" else base / "src/littlefs"
        marker = destination / ".usr-source-sha256"
        if marker.exists() and marker.read_text().strip() == checksum:
            continue
        archive = cache / (name + ".tar.gz")
        if not archive.exists():
            with urlopen(f"https://codeload.github.com/{repository}/tar.gz/{revision}",
                         timeout=60) as response:
                archive.write_bytes(response.read())
        data = archive.read_bytes()
        if hashlib.sha256(data).hexdigest() != checksum:
            raise ValueError(f"SHA-256 mismatch for {name}")
        unpack(data, destination)
        marker.write_text(checksum + "\n")


if "Import" in globals():
    Import("env")  # noqa: F821
    project = Path(env.subst("$PROJECT_DIR"))  # noqa: F821
    prepare(project)
    # sdkconfig is generated. Reconfigure when the checked-in defaults change;
    # otherwise IDF silently retains old CPU/PSRAM/network settings.
    digest = hashlib.sha256((project / "sdkconfig.defaults").read_bytes()).hexdigest()
    stamp = project / ".pio" / ("network-config-" + env.subst("$PIOENV"))  # noqa: F821
    if not stamp.exists() or stamp.read_text() != digest:
        generated = project / ("sdkconfig." + env.subst("$PIOENV"))  # noqa: F821
        generated.unlink(missing_ok=True)
        stamp.write_text(digest)
elif __name__ == "__main__":
    prepare(Path(__file__).resolve().parents[1])
