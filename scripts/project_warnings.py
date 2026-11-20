"""PlatformIO post-build script: compiler warnings for the project's own libraries (lib/).

The firmware build gets -Wall -Wextra for project code only, never for ESP-IDF or Arduino.
ESP-IDF's own flags (-Werror=all, -Wno-unused-parameter, -Wno-sign-compare,
-Wno-error=deprecated-declarations, ...) come after anything set in build_flags and would
override it, so the project's flags are added after them here. The sources in src/ are an IDF
component and get the same list from src/CMakeLists.txt. The two lists must match
(test/scripts/test_project_warnings.py checks this).

-Wextra is not an error in IDF's policy, so a warning in project code shows up in the build
output but does not stop the build. A deprecated API is an error.
"""

Import("env")  # noqa: F821  (provided by PlatformIO/SCons)

PROJECT_WARNINGS = (
    "-Wall",
    "-Wextra",
    "-Wunused-parameter",
    "-Wsign-compare",
    "-Werror=deprecated-declarations",
)

env.Append(CCFLAGS=list(PROJECT_WARNINGS))  # noqa: F821
