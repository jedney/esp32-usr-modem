#!/usr/bin/env python3
"""Make a single-file preview of the web interface, running in demo mode.

    python scripts/make_preview.py            # writes web/preview.html

Open the file in any browser: no modem or web server needed. Everything (scripts, CSS,
fonts) is inlined; the page's JavaScript modules are loaded through an import map of
data: URLs, so the code is exactly the code the modem serves.
"""

import base64
import json
import pathlib
import re
import runpy
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "web" / "src"
VENDOR = ROOT / "web" / "vendor"


def data_url(mime: str, data: bytes) -> str:
    return f"data:{mime};base64,{base64.b64encode(data).decode()}"


def strip_source_map(text: str) -> str:
    # Only "\n" counts as a line break: xterm.js has raw control characters (FS, GS, VT...)
    # inside string literals, which str.splitlines() would wrongly treat as line ends.
    return re.sub(r"^//# sourceMappingURL=[^\n]*\n?", "", text, flags=re.M)


def main(out: pathlib.Path) -> None:
    runpy.run_path(str(ROOT / "scripts/generate_ui.py"))["main"]([])
    html = (SRC / "index.html").read_text(encoding="utf-8")

    style = (SRC / "style.css").read_text(encoding="utf-8")
    for weight in (400, 700):
        font = VENDOR / f"ibm-plex-mono-{weight}.woff2"
        style = style.replace(f"url('{font.name}')", f"url('{data_url('font/woff2', font.read_bytes())}')")

    modules = {
        "usr-xterm": strip_source_map((VENDOR / "xterm.mjs").read_text(encoding="utf-8")),
        "usr-status": (SRC / "status.js").read_text(encoding="utf-8"),
        "usr-ui-data": (SRC / "ui-data.js").read_text(encoding="utf-8"),
        "usr-ui-commands": (SRC / "ui-commands.js").read_text(encoding="utf-8"),
        "usr-demo": (SRC / "demo.js").read_text(encoding="utf-8").replace("'./ui-data.js'", "'usr-ui-data'").replace("'./ui-commands.js'", "'usr-ui-commands'"),
        "usr-upload": (SRC / "upload.js").read_text(encoding="utf-8"),
        "usr-upload-flow": (SRC / "upload_flow.js").read_text(encoding="utf-8").replace("'./upload.js'", "'usr-upload'"),
        "usr-geometry": (SRC / "geometry.js").read_text(encoding="utf-8"),
        "usr-link": (SRC / "link.js").read_text(encoding="utf-8").replace("'./status.js'", "'usr-status'"),
    }
    app = (SRC / "app.js").read_text(encoding="utf-8")
    app = app.replace("'./xterm.mjs'", "'usr-xterm'").replace("'./status.js'", "'usr-status'")
    app = app.replace("'./demo.js'", "'usr-demo'")
    app = app.replace("'./upload_flow.js'", "'usr-upload-flow'")
    app = app.replace("'./link.js'", "'usr-link'")
    app = app.replace("'./geometry.js'", "'usr-geometry'")
    import_map = {"imports": {k: data_url("text/javascript", v.encode()) for k, v in modules.items()}}

    # The modem's Content-Security-Policy forbids inline scripts; the preview needs them.
    html = re.sub(r'<meta http-equiv="Content-Security-Policy"[^>]*>\s*', "", html, flags=re.S)
    html = html.replace('<link rel="stylesheet" href="xterm.css">',
                        f"<style>{(VENDOR / 'xterm.css').read_text(encoding='utf-8')}</style>")
    html = html.replace('<link rel="stylesheet" href="style.css">', f"<style>{style}</style>")
    html = html.replace(
        '<script type="module" src="ui-commands.js"></script>',
        '<script type="module">import "usr-ui-commands";</script>',
    )
    html = html.replace(
        '<script type="module" src="app.js"></script>',
        f'<script type="importmap">{json.dumps(import_map)}</script>\n'
        f"<script>window.USR_MODEM_DEMO = true;</script>\n"
        f'<script type="module">{app}</script>',
    )
    html = html.replace("<title>USR Modem</title>", "<title>USR Modem (preview)</title>")
    out.write_text(html, encoding="utf-8")
    print(f"Preview: {out} ({out.stat().st_size / 1024:.0f} KB). Letter menu preview; no device services connected.")


if __name__ == "__main__":
    main(pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "web" / "preview.html")
