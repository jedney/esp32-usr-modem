"""PlatformIO post-script: OTA upload with the device password, never on a command line or log."""

import pathlib
import sys

from SCons.Script import COMMAND_LINE_TARGETS

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

root = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
sys.path.insert(0, str(root / "scripts"))
from ota_upload import password_from, upload  # noqa: E402


def authenticated_upload(source, target, env):
    password = password_from(root / ".env")
    filesystem = any(name in COMMAND_LINE_TARGETS for name in ("uploadfs", "uploadfsota"))
    return upload(env.subst("$UPLOAD_PORT"), str(source[0]), password, filesystem)


# PlatformIO's own espota action passes the password with --auth and logs it with --debug.
# A Python action also works in verbose mode without showing a password.
env.Replace(  # noqa: F821
    UPLOADCMD=env.VerboseAction(  # noqa: F821
        authenticated_upload, "Uploading with device authentication"))
