"""The design inventory and the firmware service bindings cannot silently drift."""
import importlib.util
import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("generate_ui", ROOT / "scripts/generate_ui.py")
ui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(ui)


def test_generated_firmware_and_demo_are_current_and_deterministic():
    assert ui.generate() == ui.generate()
    assert ui.generate() == (ROOT / "lib/config/src/ui_generated.h").read_text(encoding="utf-8")
    assert ui.demo_data() == (ROOT / "web/src/ui-data.js").read_text(encoding="utf-8")
    menu = json.loads((ROOT / "ui/menu-definition.json").read_text(encoding="utf-8"))
    assert len(menu["main_menu"]) == 13
    assert sum(len(p["items"]) for p in menu["main_menu"]) == 78


def test_missing_binding_fails_generation(tmp_path):
    for relative in ("ui/menu-definition.json", "ui/theme-tokens.json", "lib/config/ui-bindings.json"):
        destination = tmp_path / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((ROOT / relative).read_bytes())
    path = tmp_path / "lib/config/ui-bindings.json"
    bindings = json.loads(path.read_text(encoding="utf-8"))
    del bindings["settings.model"]
    path.write_text(json.dumps(bindings), encoding="utf-8")
    with pytest.raises(ValueError, match="production binding"):
        ui.generate(tmp_path)


@pytest.mark.parametrize("key", ["AB", "", "0"])
def test_hotkey_must_be_one_valid_character(tmp_path, key):
    for relative in ("ui/menu-definition.json", "ui/theme-tokens.json", "lib/config/ui-bindings.json"):
        destination = tmp_path / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((ROOT / relative).read_bytes())
    path = tmp_path / "ui/menu-definition.json"
    menu = json.loads(path.read_text(encoding="utf-8"))
    menu["main_menu"][0]["key"] = key
    path.write_text(json.dumps(menu), encoding="utf-8")
    with pytest.raises(ValueError, match="hotkey"):
        ui.generate(tmp_path)
