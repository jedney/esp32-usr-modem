"""The design inventory and the firmware service bindings cannot silently drift."""
import importlib.util
import json
import re
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
    assert ui.command_data() == (ROOT / "web/src/ui-commands.js").read_text(encoding="utf-8")
    data = json.loads(ui.command_data().split("export default ", 1)[1].removesuffix(";\n"))
    bindings = json.loads((ROOT / "lib/config/ui-bindings.json").read_text(encoding="utf-8"))
    assert {row["id"] for row in data} == set(bindings)
    for row in data:
        for key, value in bindings[row["id"]].items():
            assert row[key] == value
    menu = json.loads((ROOT / "ui/menu-definition.json").read_text(encoding="utf-8"))
    assert len(menu["main_menu"]) == 13
    assert sum(len(p["items"]) for p in menu["main_menu"]) == 78


@pytest.mark.parametrize("change", [
    {"friendly": ""}, {"help": ""}, {"type": "number"}, {"type": "enum", "values": []},
    {"min": 5, "max": 2}, {"maxLength": -1}, {"pattern": "["},
    {"template": "*MODEL={missing}"}, {"template": "AT*MODEL={value}"},
    {"values": [{"value": "14400", "label": "", "description": "Missing label"}]},
    {"values": [{"value": "14400", "label": "A", "description": "A"}] * 2},
    {"displayScale": 0}, {"optional": "yes"}, {"template": "*MODEL={value"},
])
def test_invalid_metadata_is_rejected(tmp_path, change):
    for relative in ("ui/menu-definition.json", "ui/theme-tokens.json", "lib/config/ui-bindings.json"):
        destination = tmp_path / relative
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes((ROOT / relative).read_bytes())
    path = tmp_path / "lib/config/ui-bindings.json"
    bindings = json.loads(path.read_text(encoding="utf-8"))
    bindings["settings.model"].update(change)
    path.write_text(json.dumps(bindings), encoding="utf-8")
    with pytest.raises(ValueError, match="metadata"):
        ui.generate(tmp_path)


def test_all_metadata_is_typed_including_nested_fields():
    bindings = json.loads((ROOT / "lib/config/ui-bindings.json").read_text(encoding="utf-8"))

    def check(node):
        assert node["friendly"].strip() and node["help"].strip()
        assert node["type"] in {"enum", "int", "string", "bool", "ip", "action", "secret"}
        if node["type"] == "int":
            assert node["unit"] and node["min"] <= node["max"]
        for field in node.get("fields", []) + node.get("variants", []):
            check(field)

    for binding in bindings.values():
        check(binding)


def test_patterns_match_network_password_and_phonebook_rules():
    bindings = json.loads((ROOT / "lib/config/ui-bindings.json").read_text(encoding="utf-8"))
    book = {f["id"]: f for f in bindings["phonebook.add-edit-entry"]["fields"]}

    def accepts(node, value):
        size = len(value.encode("utf-8"))
        return (node.get("minLength", 0) <= size <= node.get("maxLength", 128)
                and re.fullmatch(node["pattern"], value) is not None)

    hostname = bindings["wifi-clock.network-name"]
    ntp = bindings["wifi-clock.ntp-server"]
    password = bindings["updates-reset.change-device-password"]
    for value in ("a", "A-1", "123", "a" * 32):
        assert accepts(hostname, value)
    for value in ("", "-a", "a-", "a_b", "a.b", "a" * 33, "a\n", "a\x7f"):
        assert not accepts(hostname, value)
    for value in ("pool.ntp.org", "192.0.2.1", "a.-b", "a-", "A" * 63):
        assert accepts(ntp, value)
    for value in ("", ".a", "-a", "a.", "a..b", "a_b", "a" * 64, "a\n"):
        assert not accepts(ntp, value)
    for value in ("123", "666", "667", "5517545", "5517546", "1" * 12, "1-2", ""):
        assert not accepts(book["num"], value)
    for value in ("1", "0001", "1" * 11):
        assert accepts(book["num"], value)
    for value in ("12345678", "a" * 32, " " * 8):
        assert accepts(password, value)
    for value in ("short", "a" * 33, "canary\npass", "canary\x7fpass", "pāssword"):
        assert not accepts(password, value)


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


@pytest.mark.parametrize("key", ["AB", "", "0", "1", "P", "Q", "R", "X", "O"])
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
