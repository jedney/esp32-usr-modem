"""PlatformIO script: run the PC unit tests before every upload (USB or OTA).

The upload stops if any test fails. For an emergency upload only, set the
environment variable USR_MODEM_SKIP_TESTS=1.
"""

import os
import subprocess

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)


def run_native_tests(source, target, env):  # noqa: ARG001
    if os.environ.get("USR_MODEM_SKIP_TESTS") == "1":
        print("WARNING: USR_MODEM_SKIP_TESTS=1 - uploading without running the unit tests")
        return
    print("Running unit tests (pio test -e native) before upload...")
    result = subprocess.run(
        [env.subst("$PYTHONEXE"), "-m", "platformio", "test", "-e", "native",
         "-d", env.subst("$PROJECT_DIR")],
        check=False,
    )
    if result.returncode != 0:
        print("Unit tests failed - upload cancelled.")
        env.Exit(1)


env.AddPreAction("upload", run_native_tests)  # noqa: F821
env.AddPreAction("uploadfs", run_native_tests)  # noqa: F821
