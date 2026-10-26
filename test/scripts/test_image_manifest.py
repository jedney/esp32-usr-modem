import importlib.util
from pathlib import Path
import pytest

script = Path(__file__).resolve().parents[2] / "scripts/image_manifest.py"
spec = importlib.util.spec_from_file_location("image_manifest", script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


def test_identity_is_embedded_and_target_and_version_are_checked():
    image = bytearray(24)
    image[0] = 0xE9
    image[12] = 9
    image.extend(b"USRMODEM/1/FIRMWARE/0.11\n")
    assert module.inspect(image, "firmware", "0.11")["length"] == len(image)
    with pytest.raises(ValueError):
        module.inspect(image, "firmware", "0.10")
    image[12] = 0
    with pytest.raises(ValueError):
        module.inspect(image, "firmware", "0.11")


def test_filesystem_marker_and_partition_size():
    image = b"USRMODEM/1/LITTLEFS/0.11\n"
    with pytest.raises(ValueError):
        module.inspect(image, "littlefs", "0.11")
    image += b"\xff" * (0x7E0000 - len(image))
    assert module.inspect(image, "littlefs", "0.11")["target"] == "littlefs"
