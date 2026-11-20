"""PlatformIO pre-build script: take the GPIO pin map from .env.

Reads USR_MODEM_PIN_* from .env.example (the defaults, always present in the repo), then
.env, then real environment variables, and defines each one as a compile-time macro for
lib/hw/src/pins.h. Also defines USR_MODEM_PINS_DEFAULT=1 when the map equals
.env.example, so the unit test can lock in the map for that case. A changed map
fails the pin test unless USR_MODEM_PINS_OVERRIDDEN=1 is set (USR_MODEM_PINS_OVERRIDE_OK).
Pin numbers are checked by scripts/pin_rules.py.
"""

import os
import pathlib
import sys

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

root = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
sys.path.insert(0, str(root / "scripts"))
from ota_upload import read_env_file  # noqa: E402
from pin_rules import check_pin  # noqa: E402

PREFIX = "USR_MODEM_PIN_"
OVERRIDE_FLAG = "USR_MODEM_PINS_OVERRIDDEN"  # set to 1 when a changed map is on purpose


def pin_values(source):
    return {k: v for k, v in source.items() if k.startswith(PREFIX)}


defaults = pin_values(read_env_file(root / ".env.example"))
if not defaults:
    raise SystemExit(f"No {PREFIX}* pins found in .env.example")

pins = dict(defaults)
pins.update(pin_values(read_env_file(root / ".env")))
pins.update(pin_values(os.environ))

for name in defaults:
    try:
        number = int(pins[name], 10)
    except ValueError:
        raise SystemExit(f"{name} must be a GPIO number, found {pins[name]!r}")
    try:
        check_pin(name, number)
    except ValueError as error:
        raise SystemExit(str(error))
    pins[name] = number

unknown = sorted(set(pins) - set(defaults))
if unknown:
    raise SystemExit("Unknown pin setting(s): " + ", ".join(unknown))

used = {}
for name, number in pins.items():
    if number in used.values():
        other = next(k for k, v in used.items() if v == number)
        raise SystemExit(f"GPIO{number} is assigned to both {other} and {name}")
    used[name] = number

env.Append(CPPDEFINES=[(name, number) for name, number in pins.items()])  # noqa: F821
overridden = pins != {k: int(v) for k, v in defaults.items()}
flag = os.environ.get(OVERRIDE_FLAG) or read_env_file(root / ".env").get(OVERRIDE_FLAG, "")
env.Append(CPPDEFINES=[("USR_MODEM_PINS_OVERRIDE_OK", int(flag == "1"))])  # noqa: F821
if overridden and flag != "1":
    print(f"WARNING: the pin map differs from .env.example. Unit tests will fail until you "
          f"set {OVERRIDE_FLAG}=1 (only if the change is on purpose).")
env.Append(CPPDEFINES=[("USR_MODEM_PINS_DEFAULT", int(all(  # noqa: F821
    pins[k] == int(defaults[k]) for k in defaults)))])
print("GPIO pin map from " + ("defaults (.env.example)" if all(
    pins[k] == int(defaults[k]) for k in defaults) else ".env / environment"))
