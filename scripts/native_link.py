"""PlatformIO script for the native (PC) unit tests: on Windows, link them statically.

During an upload, PlatformIO puts the ESP32 compiler's folder first on PATH. That folder
holds its own, older libstdc++-6.dll, so a test program linked against MinGW's DLLs can
fail to start ("Program errored with 3221225785", a missing DLL entry point). Static
linking makes the test programs independent of PATH.
"""

import sys

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

if sys.platform == "win32":
    env.Append(LINKFLAGS=["-static"])  # noqa: F821
