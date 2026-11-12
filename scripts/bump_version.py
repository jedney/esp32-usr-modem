#!/usr/bin/env python3
"""Bump VERSION and add a CHANGES.md entry.

Usage:
  python scripts/bump_version.py minor "Fixed DTR hang-up timing"
  python scripts/bump_version.py major "Added USB CDC serial (hardware change)"

Rules (project brief 6.9):
  minor  +0.1  small enhancement or bug fix  (0.9 -> 0.10, never 0.9 -> 1.0)
  major  +1.0  major feature or hardware change (resets the minor number to 0)
"""

import datetime
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
VERSION_FILE = ROOT / "VERSION"
CHANGES_FILE = ROOT / "CHANGES.md"


def bump(version: str, part: str) -> str:
    match = re.fullmatch(r"(\d+)\.(\d+)", version)
    if not match:
        raise ValueError(f"bad version {version!r}")
    major, minor = int(match.group(1)), int(match.group(2))
    if part == "minor":
        return f"{major}.{minor + 1}"
    if part == "major":
        return f"{major + 1}.0"
    raise ValueError("part must be 'minor' or 'major'")


def main(argv):
    if len(argv) != 3 or argv[1] not in ("minor", "major"):
        print(__doc__)
        return 2
    part, summary = argv[1], argv[2].strip()
    old = VERSION_FILE.read_text(encoding="utf-8").strip()
    new = bump(old, part)
    section = "Changed" if part == "minor" else "Added"
    today = datetime.date.today().isoformat()

    changes = CHANGES_FILE.read_text(encoding="utf-8")
    marker = "## [Unreleased]\n"
    if marker not in changes:
        raise SystemExit("CHANGES.md has no '## [Unreleased]' heading")
    entry = f"{marker}\n## [{new}] - {today}\n\n### {section}\n\n- {summary}\n"
    CHANGES_FILE.write_text(changes.replace(marker, entry, 1), encoding="utf-8", newline="\n")
    VERSION_FILE.write_text(new + "\n", encoding="utf-8", newline="\n")
    print(f"{old} -> {new}. Commit with a message starting 'v{new}: '.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
