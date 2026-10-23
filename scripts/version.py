"""PlatformIO pre-build script: embed the project version in the firmware.

Reads VERSION (e.g. "0.1") and the git commit, and defines:
  FW_VERSION  "0.1"
  FW_COMMIT   "a1b2c3d" (or "a1b2c3d-dirty" when there are uncommitted changes)
"""

import pathlib
import re
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

VERSION_RE = re.compile(r"^\d+\.\d+$")


def _git(root, *args):
    try:
        return subprocess.check_output(
            ["git", *args], cwd=root, stderr=subprocess.DEVNULL
        ).decode().strip()
    except (OSError, subprocess.CalledProcessError):
        return ""


root = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
version = (root / "VERSION").read_text(encoding="utf-8").strip()
if not VERSION_RE.match(version):
    raise SystemExit(f"VERSION must look like 0.1 or 1.0, found {version!r}")

commit = _git(root, "rev-parse", "--short", "HEAD") or "nogit"
if _git(root, "status", "--porcelain", "--untracked-files=no"):
    commit += "-dirty"

env.Append(  # noqa: F821
    CPPDEFINES=[
        ("FW_VERSION", env.StringifyMacro(version)),  # noqa: F821
        ("FW_COMMIT", env.StringifyMacro(commit)),  # noqa: F821
    ]
)
print(f"USR Modem firmware v{version} ({commit})")
