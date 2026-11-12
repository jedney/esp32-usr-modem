"""Use the installed espota implementation without passing a secret on its CLI."""

import importlib.util
import os

KEY = "USR_MODEM_PASSWORD"
CALLBACK_PORT = 32320


def read_env_file(path):
    values = {}
    try:
        raw_bytes = path.read_bytes()
    except OSError:
        return values
    if raw_bytes[:2] in (b"\xff\xfe", b"\xfe\xff"):
        raise ValueError(f"{path.name} is saved as UTF-16. Save it as UTF-8 (no BOM needed).")
    try:
        text = raw_bytes.decode("utf-8-sig")
    except UnicodeDecodeError:
        raise ValueError(f"{path.name} is not valid UTF-8. Save it as UTF-8.") from None
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key, value = key.strip(), value.strip()
        if key.startswith("export "):
            key = key[len("export "):].strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values


def password_from(path):
    return os.environ.get(KEY, read_env_file(path).get(KEY, ""))


def upload(espota_path, host, image, password, filesystem=False):
    if not password:
        print(f"OTA refused: set {KEY} in .env or the environment.")
        return 1
    try:
        spec = importlib.util.spec_from_file_location("modem_espota", espota_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        module.TIMEOUT = 10
        module.PROGRESS = True
        return module.serve(host, "0.0.0.0", 3232, CALLBACK_PORT, password,
                            image, module.SPIFFS if filesystem else module.FLASH)
    except Exception:
        # Some espota versions raise exceptions containing their arguments. Never
        # stringify one or print a traceback from this authentication boundary.
        print("OTA upload failed. Check Wi-Fi, OTA mode and the device password.")
        return 1
