"""The web file list is kept in several places; they must name the same files.

- scripts/build_web.py        what goes into the LittleFS image
- lib/hw/src/web_service.cpp  what the modem serves
- the page itself             what index.html, the style sheets and the modules load
- scripts/make_preview.py     the modules inlined into web/preview.html
- test/e2e/test_web.py        the assets the end-to-end test expects behind the login
"""
import importlib.util
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "web" / "src"
VENDOR = ROOT / "web" / "vendor"


def built_files():
    path = ROOT / "scripts" / "build_web.py"
    spec = importlib.util.spec_from_file_location("build_web_under_test", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return set(module.web_files(ROOT))


def served_files():
    text = (ROOT / "lib" / "hw" / "src" / "web_service.cpp").read_text(encoding="utf-8")
    block = text[text.index("static const Asset assets[]"):]
    block = block[: block.index("};")]
    return {m.group(1).lstrip("/") for m in re.finditer(r'\{"(/[^"]+)",\s*"[^"]+"\}', block)}


def source_for(name):
    for folder in (SRC, VENDOR):
        if (folder / name).exists():
            return folder / name
    raise AssertionError(f"{name} is not in web/src or web/vendor")


def loaded_files():
    """Everything the page can load, found by following index.html, style sheets and imports."""
    found = set()
    todo = []
    html = (SRC / "index.html").read_text(encoding="utf-8")
    for match in re.finditer(r'(?:src|href)="([^":/][^":]*\.(?:js|mjs|css))"', html):
        todo.append(match.group(1))
    while todo:
        name = todo.pop()
        if name in found:
            continue
        found.add(name)
        if not name.endswith((".css", ".js", ".mjs")):
            continue  # fonts and other binary files load nothing
        text = source_for(name).read_text(encoding="utf-8")
        if name.endswith(".css"):
            todo += re.findall(r"url\('([^'/:]+)'\)", text)
        elif not name.startswith("xterm"):  # vendored bundle: its own imports are not ours
            todo += re.findall(r"""from\s+'\./([^']+)'""", text)
            todo += re.findall(r"""import\('\./([^']+)'\)""", text)
    return found


def test_build_list_and_served_list_name_the_same_files():
    assert built_files() == served_files()


def test_every_file_the_page_loads_is_built_and_served():
    loaded = loaded_files()
    assert "app.js" in loaded and "upload_flow.js" in loaded and "demo.js" in loaded
    assert loaded - built_files() == set(), "loaded by the page but not built"
    assert loaded - served_files() == set(), "loaded by the page but not served"


def test_nothing_is_built_that_the_page_never_loads():
    # index.html (the entry) is loaded by the browser itself.
    assert built_files() - loaded_files() == {"index.html"}


def test_preview_inlines_every_module_the_page_imports():
    text = (ROOT / "scripts" / "make_preview.py").read_text(encoding="utf-8")
    modules = {name for name in loaded_files() if name.endswith((".js", ".mjs"))}
    for name in modules:
        assert f'"{name}"' in text or f"'./{name}'" in text or f"(SRC / \"{name}\")" in text or \
            f"VENDOR / \"{name}\"" in text, f"make_preview.py does not inline {name}"


def test_end_to_end_asset_check_names_only_files_that_are_served():
    text = (ROOT / "test" / "e2e" / "test_web.py").read_text(encoding="utf-8")
    block = text[text.index("def test_assets_require_digest"):]
    block = block[: block.index("for path in", block.index("for path in") + 1)]
    named = {m.group(1).lstrip("/") for m in re.finditer(r'"(/[a-z_\-]+\.js)"', block)}
    assert named, "no assets found in the end-to-end test"
    assert named <= served_files()
    assert "upload_flow.js" in named
