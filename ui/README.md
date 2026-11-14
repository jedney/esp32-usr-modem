# Sportster configuration screen design

This folder is the design contract for the configuration screen: four model themes, the
canonical 80x25 layout and the wider layout for larger terminals. The firmware renders
it in `lib/config/src/`, and `scripts/generate_ui.py` builds the firmware catalogue and
the browser demo data from `menu-definition.json`. Main menu keys A-D are About,
Hardware Tests, Client Tests and Command Reference; Client Tests run on serial only and
the browser lists them for reference. The text previews are layout references, never
application screens.

## Files

| Path | Description |
| --- | --- |
| `DESIGN_SPEC.md` | The design: layout, colour, model selection, interaction, security and acceptance rules. |
| `theme-tokens.json` | Four exact model accent colours + shared ANSI palette, resolution targets. |
| `menu-definition.json` | Complete 13-entry main menu, 78 nested items, UI keys, serial-only Client Tests. |
| `previews/*.txt` | Strict 80×25 (80 display cells per line, 25 rows) layout examples; the tests and `scripts/generate_ui.py` read them. |
| `tools/preview_80x25.py` | Regenerates the text previews, standard library only; reads the JSON theme and menu. |
| `tools/render_runtime.py` | Optional Pillow renderer for the cell snapshots the native `test_config` writes when `USR_UI_SNAPSHOTS=1`. |
| `tests/test_layout_contract.py` | Built-in unittest checks of static handoff grid and data. |

## Static layout checks (Python 3)

```sh
python ui/tools/preview_80x25.py
python -m unittest discover -s ui/tests -v
```

The previews deliberately use `Link: UNKNOWN`, `Profile: --`, and an unbound Phonebook. No real modem has been queried; no fake contacts or serial numbers have been inserted. For an unknown runtime model, the actual implementation should show an explicit unknown-model state instead of treating the 28.8k preview as a default.
