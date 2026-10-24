"""PlatformIO post-script: authenticated espota without secrets in arguments/logs."""

import pathlib
import sys

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

root = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
sys.path.insert(0, str(root / "scripts"))
from ota_upload import password_from, upload  # noqa: E402


def authenticated_upload(source, target, env):
    framework = pathlib.Path(env.PioPlatform().get_package_dir("framework-arduinoespressif32"))
    password = password_from(root / ".env")
    filesystem = any(name in COMMAND_LINE_TARGETS for name in ("uploadfs", "uploadfsota"))
    return upload(framework / "tools" / "espota.py", env.subst("$UPLOAD_PORT"),
                  str(source[0]), password, filesystem)


# The original action uses --debug and logs the parsed options, including --auth.
# A callable action also works in verbose mode without displaying a password.
env.Replace(UPLOADCMD=env.VerboseAction(authenticated_upload, "Uploading with device authentication"))  # noqa: F821
