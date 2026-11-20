"""Compile the design inventory and service bindings; no runtime JSON parser needed."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

TYPES = {"enum", "int", "string", "bool", "ip", "action", "secret"}


def walk_menu(nodes):
    for node in nodes:
        yield node
        yield from walk_menu(node.get("items", []))


def validate_metadata(node, path):
    def fail(message):
        raise ValueError(f"Invalid metadata {path}: {message}")

    for key in ("friendly", "help"):
        if not isinstance(node.get(key), str) or not node[key].strip():
            fail(f"missing {key}")
    if node.get("type") not in TYPES:
        fail("unknown type")
    for key in ("min", "max", "minLength", "maxLength"):
        if key in node and (type(node[key]) is not int or node[key] < 0):
            fail(f"invalid {key}")
    for lo, hi in (("min", "max"), ("minLength", "maxLength")):
        if lo in node and hi in node and node[lo] > node[hi]:
            fail(f"{lo} exceeds {hi}")
    if node["type"] == "int" and (not all(k in node for k in ("min", "max", "unit")) or not node["unit"]):
        fail("integers need a range and unit")
    for key in ("optional", "allowCustom", "readOnly"):
        if key in node and type(node[key]) is not bool:
            fail(f"invalid {key}")
    if "displayScale" in node and (type(node["displayScale"]) not in (int, float) or node["displayScale"] <= 0):
        fail("invalid displayScale")
    if "pattern" in node:
        try:
            re.compile(node["pattern"])
        except (re.error, TypeError):
            fail("invalid pattern")
    values = node.get("values", [])
    if node["type"] in ("enum", "bool") and not values:
        fail("enumerations need values")
    seen = set()
    for value in values:
        if not all(isinstance(value.get(k), str) and value[k].strip()
                   for k in ("value", "label", "description")):
            fail("values need value, label and description")
        if value["value"] in seen:
            fail("duplicate value")
        seen.add(value["value"])
    for group in ("fields", "variants"):
        names = [child.get("id") for child in node.get(group, [])]
        if any(not name for name in names) or len(set(names)) != len(names):
            fail(f"invalid {group} IDs")
        for child in node.get(group, []):
            validate_metadata(child, f"{path}.{child['id']}")
    template = node.get("template", "")
    if template:
        if not re.match(r"^\*[A-Z?]+(?:[=? ].*)?$", template):
            fail("template must start with a raw AT* command (without AT)")
        placeholders = re.findall(r"\{([^{}]+)\}", template)
        allowed = {field["id"] for field in node.get("fields", [])} or {"value"}
        if any(name not in allowed for name in placeholders):
            fail("unknown template placeholder")
        if "{" in re.sub(r"\{[^{}]+\}", "", template) or "}" in re.sub(r"\{[^{}]+\}", "", template):
            fail("invalid template placeholder")


def catalogue(root):
    menu = json.loads((root / "ui/menu-definition.json").read_text(encoding="utf-8"))
    bindings = json.loads((root / "lib/config/ui-bindings.json").read_text(encoding="utf-8"))
    items = list(walk_menu(menu["main_menu"]))
    ids = [item["id"] for item in items]
    if len(ids) != len(set(ids)) or set(ids) != set(bindings):
        raise ValueError("Every menu ID must have exactly one production binding")

    def validate_parent(nodes, screen):
        for node in nodes:
            binding = bindings[node["id"]]
            if binding["screen"] != screen:
                raise ValueError(f"Invalid metadata {node['id']}: screen differs from menu parent")
            if node.get("items"):
                if binding["command"]:
                    raise ValueError(f"Invalid metadata {node['id']}: a menu group cannot run a command")
                validate_parent(node["items"], binding["target"])

    validate_parent(menu["main_menu"], "Main")
    for item in items:
        b = bindings[item["id"]]
        validate_metadata(b, item["id"])
        if "template" not in b or (b["command"] and not b["template"]):
            raise ValueError(f"Invalid metadata {item['id']}: missing template")
        if b["command"] and not b["template"].startswith(b["command"]):
            raise ValueError(f"Invalid metadata {item['id']}: template differs from binding")
        if b.get("secret", False) != (b["type"] == "secret"):
            raise ValueError(f"Invalid metadata {item['id']}: secret flag/type mismatch")
    return menu, items, bindings


def cpp_metadata(items, bindings):
    quote = lambda value: json.dumps(value, ensure_ascii=True)
    out = []

    def emit(nodes, symbol):
        rows = []
        for i, node in enumerate(nodes):
            base = f"{symbol}_{i}"
            choices = node.get("values", [])
            if choices:
                out.append(f"constexpr UiValue {base}_values[] = {{\n")
                for v in choices:
                    out.append("    {" + ", ".join(quote(v[k]) for k in ("value", "label", "description")) + "},\n")
                out.append("};\n")
            fields, variants = node.get("fields", []), node.get("variants", [])
            if fields:
                emit(fields, base + "_fields")
            if variants:
                emit(variants, base + "_variants")
            row = [quote(node.get("id", "")), quote(node["friendly"]), quote(node["help"]),
                   "UiType::" + node["type"].capitalize(), quote(node.get("unit", ""))]
            row += [str(node.get(k, -1)) for k in ("min", "max", "minLength", "maxLength")]
            row += [quote(node.get("pattern", "")), base + "_values" if choices else "nullptr", str(len(choices)),
                    quote(node.get("template", "")), base + "_fields" if fields else "nullptr", str(len(fields)),
                    base + "_variants" if variants else "nullptr", str(len(variants))]
            row += [str(node.get(k, False)).lower() for k in ("optional", "allowCustom", "readOnly")]
            row += [quote(node.get("displayUnit", node.get("unit", ""))), str(node.get("displayScale", 1))]
            rows.append("    {" + ", ".join(row) + "},\n")
        out.append(f"constexpr UiMetadata {symbol}[] = {{\n" + "".join(rows) + "};\n")

    emit([dict(bindings[item["id"]], id=item["id"]) for item in items], "kCommandMetadata")
    return "".join(out)


def command_data(root=ROOT):
    _, items, bindings = catalogue(root)
    data = [dict(bindings[item["id"]], id=item["id"], key=item["key"],
                 available=item.get("status") != "unavailable") for item in items]
    return "// Generated by scripts/generate_ui.py. Do not edit.\nexport default " + json.dumps(data, ensure_ascii=True, indent=2) + ";\n"


def generate(root=ROOT):
    menu, items, bindings = catalogue(root)
    theme = json.loads((root / "ui/theme-tokens.json").read_text(encoding="utf-8"))
    for group in [menu["main_menu"], *[p.get("items", []) for p in items]]:
        keys = [item["key"] for item in group]
        if keys != list("ABCDEFGHIJKLMNO")[:len(keys)]:
            raise ValueError("Invalid hotkey order: use consecutive letters A-O")
    quote = lambda value: json.dumps(value, ensure_ascii=True)
    out = "// Generated by scripts/generate_ui.py. Do not edit.\n#pragma once\n// clang-format off\n"
    # Native tests check the source fingerprint too, so stale generated data cannot hide drift.
    fingerprint = 2166136261
    source = (root / "lib/config/ui-bindings.json").read_text(encoding="utf-8")
    for byte in source.encode("utf-8"):
        fingerprint = ((fingerprint ^ byte) * 16777619) & 0xffffffff
    out += f"namespace config {{\nconstexpr uint32_t kBindingsFingerprint = {fingerprint}u;\n"
    out += cpp_metadata(items, bindings) + "constexpr Action kActions[] = {\n"
    for index, item in enumerate(items):
        b = bindings[item["id"]]
        available = item.get("status") != "unavailable"
        if available and not b["command"] and b["target"] == b["screen"]:
            raise ValueError(f"Missing action/route: {item['id']}")
        values = ["Screen::" + b["screen"], quote(item["label"]), quote(b["command"]),
                  quote(b["help"]), "Screen::" + b["target"]]
        values += [str(b.get(flag, False)).lower() for flag in ("input", "secret", "rs232Unavailable", "leave")]
        values += [quote(item["id"]), "'" + item["key"] + "'", str(available).lower(), quote(item.get("summary", b["help"]))]
        values += [f"&kCommandMetadata[{index}]"]
        out += "    {" + ", ".join(values) + "},\n"
    out += "};\nstruct ModelTheme { unsigned speed; const char *label; const char *shortLabel; unsigned rgb; };\n"
    out += "constexpr ModelTheme kModelThemes[] = {\n"
    for m in theme["models"]:
        out += f"    {{{m['nominal_line_rate_bps']}, {quote(m['label'])}, {quote(m['short_label'])}, 0x{m['accent'][1:]}}},\n"
    out += "};\nconstexpr unsigned kUiColors[] = {\n"
    roles = ["primary_text", "frame_white", "logo_red", "bright_cyan", "hotkey_green", "shortcut_blue", "disabled_gray", "muted_text", "warning_yellow", "error_red"]
    out += "    " + ", ".join("0x" + theme["colors"][role][1:] for role in roles) + ",\n"
    out += "};\n}  // namespace config\n// clang-format on\n"
    return out


def demo_data(root=ROOT):
    menu = json.loads((root / "ui/menu-definition.json").read_text(encoding="utf-8"))
    theme = json.loads((root / "ui/theme-tokens.json").read_text(encoding="utf-8"))
    data = {"menu": menu["main_menu"], "models": theme["models"], "colors": theme["colors"]}
    return "// Generated by scripts/generate_ui.py. DEMO catalogue only.\nexport default " + json.dumps(data, ensure_ascii=True, indent=2) + ";\n"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args(argv)
    for path, content in [(ROOT / "lib/config/src/ui_generated.h", generate()),
                          (ROOT / "web/src/ui-data.js", demo_data()),
                          (ROOT / "web/src/ui-commands.js", command_data())]:
        previous = path.read_text(encoding="utf-8") if path.exists() else ""
        if args.check:
            if previous != content:
                raise SystemExit("UI catalogue is stale; run python scripts/generate_ui.py")
        elif previous != content:
            path.write_text(content, encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
