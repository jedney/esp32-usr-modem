"""Prepare immutable LittleFS and TinyUSB sources for the Arduino-as-IDF-component build.

These are the same revisions recorded in Arduino 2.0.17's SDK versions.txt. The
arduino_tinyusb component comes from Espressif's esp32-arduino-lib-builder (the commit
current when Arduino 2.0.17 was built); its TinyUSB headers match the ones Arduino 2.0.17
ships. Its CMakeLists.txt is replaced by scripts/arduino_tinyusb/CMakeLists.txt.
Only the project's ignored .pio directory is written; installed packages stay intact.
"""

import hashlib
import io
from pathlib import Path, PurePosixPath
import shutil
import tarfile
from urllib.request import urlopen

LITTLEFS = Path("esp_littlefs")
TINYUSB = Path("arduino_tinyusb")

# name, GitHub repository, revision, SHA-256 of the archive, folder inside the archive
# ("" = all of it), destination under .pio/network-deps
SOURCES = (
    ("esp_littlefs", "joltwallet/esp_littlefs",
     "41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317",
     "8240ad063e1fb5a5108a6fb1bb7e0b29c49745affa408a57a96ddc6bcd597570",
     "", LITTLEFS),
    ("littlefs", "littlefs-project/littlefs",
     "f53a0cc961a8acac85f868b431d2f3e58e447ba3",
     "8172652dc546a408190f259c590d9c4badab3744ef3fa584335cadb822bb34e1",
     "", LITTLEFS / "src/littlefs"),
    ("arduino_tinyusb", "espressif/esp32-arduino-lib-builder",
     "3eb9cb06af",
     "d38038f08b380764d4df69ab43ed8f7a7058d9e80a2b29d8ca8a4dec19808bb1",
     "components/arduino_tinyusb", TINYUSB),
    ("tinyusb", "hathach/tinyusb",
     "a0e5626bc50d484a23f33000c48082179f0cc2dd",
     "82c214d2eef32bebc9a720c0e88f43f6f79c5398c29a61b033dcf5f76382fc72",
     "src", TINYUSB / "tinyusb/src"),
)


def unpack(data, destination, folder=""):
    """Extract regular files only, stripping the archive's repository prefix.

    With `folder`, only that folder of the repository is extracted, into `destination`.
    """
    destination = destination.resolve()
    prefix = PurePosixPath(folder).parts if folder else ()
    with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as archive:
        for member in archive.getmembers():
            parts = PurePosixPath(member.name).parts[1:]
            if parts[:len(prefix)] != prefix:
                continue
            parts = parts[len(prefix):]
            if not parts:
                continue
            relative = Path(*parts)
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
    deps = root / ".pio/network-deps"
    for name, repository, revision, checksum, folder, place in SOURCES:
        destination = deps / place
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
        unpack(data, destination, folder)
        marker.write_text(checksum + "\n")
    # The project's own build file for the TinyUSB component (see its comments).
    ours = root / "scripts/arduino_tinyusb/CMakeLists.txt"
    theirs = deps / TINYUSB / "CMakeLists.txt"
    if not theirs.exists() or theirs.read_bytes() != ours.read_bytes():
        shutil.copyfile(ours, theirs)


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
