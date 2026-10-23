"""Build the web interface into data/www/ (gzipped) for the LittleFS image.

Runs automatically as a PlatformIO pre-script when the filesystem image is built
(`pio run -e s3 -t buildfs` / `-t uploadfs`), or by hand:

    python scripts/build_web.py
"""

import gzip
import pathlib
import re

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "web" / "src"
VENDOR = ROOT / "web" / "vendor"

# Published name -> source file. Everything is served flat from /.
FILES = {
    "index.html": SRC / "index.html",
    "style.css": SRC / "style.css",
    "app.js": SRC / "app.js",
    "status.js": SRC / "status.js",
    "demo.js": SRC / "demo.js",
    "xterm.mjs": VENDOR / "xterm.mjs",
    "xterm.css": VENDOR / "xterm.css",
    "ibm-plex-mono-400.woff2": VENDOR / "ibm-plex-mono-400.woff2",
    "ibm-plex-mono-700.woff2": VENDOR / "ibm-plex-mono-700.woff2",
}


def read_asset(path: pathlib.Path) -> bytes:
    data = path.read_bytes()
    if path.suffix in (".mjs", ".js"):
        # The source map isn't shipped, so drop the reference to it. Match on "\n" only:
        # xterm.js has raw control characters inside strings that splitlines() would split on.
        data = re.sub(rb"(?m)^//# sourceMappingURL=[^\n]*\n?", b"", data)
    return data


def build(out_dir: pathlib.Path) -> int:
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.gz"):
        old.unlink()
    total = 0
    for name, src in FILES.items():
        # mtime=0 keeps the output identical from build to build
        packed = gzip.compress(read_asset(src), compresslevel=9, mtime=0)
        (out_dir / f"{name}.gz").write_bytes(packed)
        total += len(packed)
    print(f"Web interface: {len(FILES)} files, {total / 1024:.0f} KB gzipped -> {out_dir}")
    return total


try:
    Import("env")  # noqa: F821  (provided by PlatformIO/SCons)
except NameError:
    if __name__ == "__main__":
        build(ROOT / "data" / "www")
else:
    targets = {str(t) for t in BUILD_TARGETS}  # noqa: F821
    if targets & {"buildfs", "uploadfs", "uploadfsota"}:
        build(pathlib.Path(env.subst("$PROJECT_DATA_DIR")) / "www")  # noqa: F821
