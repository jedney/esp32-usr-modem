#!/usr/bin/env python3
"""CI check: VERSION is well formed and CHANGES.md has an entry for it.

Exit code 0 on success, 1 on failure. Prints the version on success.
"""

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def main():
    version = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"\d+\.\d+", version):
        print(f"VERSION must look like 0.1 or 1.0, found {version!r}")
        return 1
    changes = (ROOT / "CHANGES.md").read_text(encoding="utf-8")
    if not re.search(rf"^## \[{re.escape(version)}\] - \d{{4}}-\d{{2}}-\d{{2}}$", changes, re.M):
        print(f"CHANGES.md has no '## [{version}] - YYYY-MM-DD' entry")
        return 1
    print(version)
    return 0


if __name__ == "__main__":
    sys.exit(main())
