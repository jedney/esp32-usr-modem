"""Build the web interface into data/www/ (gzipped) for the LittleFS image.

Runs automatically as a PlatformIO pre-script when the filesystem image is built
(`pio run -e s3 -t buildfs` / `-t uploadfs`), or by hand:

    python scripts/build_web.py

Note: PlatformIO runs extra scripts through SCons, which doesn't define __file__, so the
project root comes from the build environment there and from __file__ only when run by hand.
"""

import gzip
import pathlib
import re
import runpy

FS_TARGETS = {"buildfs", "uploadfs", "uploadfsota"}


def web_files(root: pathlib.Path) -> dict:
    """Published name -> source file. Everything is served flat from /."""
    src = root / "web" / "src"
    vendor = root / "web" / "vendor"
    return {
        "index.html": src / "index.html",
        "style.css": src / "style.css",
        "app.js": src / "app.js",
        "status.js": src / "status.js",
        "demo.js": src / "demo.js",
        "upload.js": src / "upload.js",
        "upload_flow.js": src / "upload_flow.js",
        "link.js": src / "link.js",
        "geometry.js": src / "geometry.js",
        "idle.js": src / "idle.js",
        "toast.js": src / "toast.js",
        "ui-data.js": src / "ui-data.js",
        "ui-commands.js": src / "ui-commands.js",
        "xterm.mjs": vendor / "xterm.mjs",
        "xterm.css": vendor / "xterm.css",
        "ibm-plex-mono-400.woff2": vendor / "ibm-plex-mono-400.woff2",
        "ibm-plex-mono-700.woff2": vendor / "ibm-plex-mono-700.woff2",
    }


def read_asset(path: pathlib.Path) -> bytes:
    data = path.read_bytes()
    if path.suffix in (".mjs", ".js"):
        # The source map isn't shipped, so drop the reference to it. Match on "\n" only:
        # xterm.js has raw control characters inside strings that splitlines() would split on.
        data = re.sub(rb"(?m)^//# sourceMappingURL=[^\n]*\n?", b"", data)
    return data


def build(root: pathlib.Path, out_dir: pathlib.Path) -> int:
    runpy.run_path(str(root / "scripts/generate_ui.py"))["main"]([])
    files = web_files(root)
    out_dir.mkdir(parents=True, exist_ok=True)
    for old in out_dir.glob("*.gz"):
        old.unlink()
    total = 0
    for name, src in files.items():
        # mtime=0 keeps the output identical from build to build
        packed = gzip.compress(read_asset(src), compresslevel=9, mtime=0)
        (out_dir / f"{name}.gz").write_bytes(packed)
        total += len(packed)
    print(f"Web interface: {len(files)} files, {total / 1024:.0f} KB gzipped -> {out_dir}")
    version = (root / "VERSION").read_text(encoding="utf-8").strip()
    (out_dir.parent / "image-id.txt").write_text(
        f"USRMODEM/1/LITTLEFS/{version}\n", encoding="ascii", newline="\n"
    )
    return total


try:
    Import("env")  # noqa: F821  (only defined when PlatformIO/SCons runs this script)
except NameError:
    if __name__ == "__main__":
        project = pathlib.Path(__file__).resolve().parent.parent
        build(project, project / "data" / "www")
else:
    from SCons.Script import COMMAND_LINE_TARGETS  # noqa: E402

    if FS_TARGETS & set(COMMAND_LINE_TARGETS):
        project = pathlib.Path(env.subst("$PROJECT_DIR"))  # noqa: F821
        build(project, pathlib.Path(env.subst("$PROJECT_DATA_DIR")) / "www")  # noqa: F821
