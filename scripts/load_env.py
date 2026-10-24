"""PlatformIO pre-script for uploads over Wi-Fi (env s3_ota): the OTA password.

Reads USR_MODEM_OTA_PASSWORD from the environment or, if it isn't set there, from the
git-ignored `.env` file at the project root (see `.env.example`), and passes it to
espota as --auth. The password is never printed (except in PlatformIO's verbose mode,
which shows the whole upload command).
"""

import os
import pathlib

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

KEY = "USR_MODEM_OTA_PASSWORD"


def read_env_file(path):
    values = {}
    try:
        text = path.read_text(encoding="utf-8")
    except OSError:
        return values
    for raw in text.splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if key.startswith("export "):
            key = key[len("export "):].strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        values[key] = value
    return values


root = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
password = os.environ.get(KEY) or read_env_file(root / ".env").get(KEY, "")
if password:
    env.Replace(UPLOAD_FLAGS=[f"--auth={password}"])  # noqa: F821
else:
    print(f"Note: {KEY} is not set (.env or environment); the OTA upload will be refused.")
