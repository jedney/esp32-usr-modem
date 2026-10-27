# Sportster configuration screen design

This folder is the design contract for the configuration screen: four model themes, the
canonical 80x25 layout and the wider layout for larger terminals. The firmware renders
it in `lib/config/src/`, and `scripts/generate_ui.py` builds the firmware catalogue and
the browser demo data from `menu-definition.json`. Main menu keys A-D are About,
Hardware Tests, Client Tests and Command Reference; Client Tests run on serial only and
the browser lists them for reference. The preview images are references, never
application screens. The original implementation notes are in
[`docs/history/sportster-ui-implementation.md`](../docs/history/sportster-ui-implementation.md).

## Files

| Path | Description |
| --- | --- |
| `CODEX_PROMPT.md` | Paste-ready task updated from the user's revised instructions. |
| `DESIGN_SPEC.md` | Layout, colour, model selection, interaction, security and acceptance rules. |
| `theme-tokens.json` | Four exact model accent colours + shared ANSI palette, resolution targets. |
| `menu-definition.json` | Complete 13-entry main menu, 72 nested items, UI keys, serial-only Client Tests. |
| `assets/reference-main-menu.png` | 28.8k wide visual concept. |
| `assets/reference-phonebook.png` | 28.8k wide Phonebook visual concept. |
| `assets/us-robotics-logo-original.svg` | Supplied red brand mark, reference for ANSI approximation. |
| `previews/*.txt` | Strict 80×25 (80 display cells per line, 25 rows) human-readable examples. |
| `previews/*.ans` | ANSI 24-bit-colour terminal versions of these exact cell grids. |
| `previews/*.png` | Image previews rendered from actual 80×25 cell buffers, not AI recreations. |
| `tools/preview_80x25.py` | Python reference generator, standard library only; reads JSON theme + menu. |
| `tools/render_previews.py` | Optional Pillow-powered PNG renderer for preview updates. |
| `tests/test_layout_contract.py` | Built-in unittest checks of static handoff grid and data. |

## Static layout checks (Python 3)

```sh
python ui/tools/preview_80x25.py
python -m unittest discover -s ui/tests -v
# optional for PNGs: pip install pillow
python ui/tools/render_previews.py
```

The previews deliberately use `Link: UNKNOWN`, `Profile: --`, and an unbound Phonebook. No real modem has been queried; no fake contacts or serial numbers have been inserted. For an unknown runtime model, the actual implementation should show an explicit unknown-model state instead of treating the 28.8k preview as a default.
