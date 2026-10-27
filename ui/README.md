# USRobotics Sportster BBS UI — 80×25 Codex handoff

v0.12 integration: C opens Client Tests on serial; the browser lists them without
execution. The catalogue now has 72 nested actions (57 original plus 15 client
actions). This supersedes the original disabled-placeholder instructions below.


This is **a design and implementation contract**, not a deployable program. The package supports four modem identities, a first-class **80×25 terminal**, and an enhanced wide-terminal rendition inspired by the supplied 28.8k images.

This repository now consumes the contract in `lib/config/src/`. See
[`docs/sportster-ui-implementation.md`](../docs/sportster-ui-implementation.md)
for service bindings, keyboard controls, geometry, validation and remaining manual
acceptance. `scripts/generate_ui.py` builds the firmware catalogue and labelled
browser DEMO data; the preview PNGs are never used as application screens.

## Quick start in VS Code / Codex

1. Copy the entire `ui/` folder into your application's repository root.
2. Open the repository in VS Code and start Codex within it.
3. Tell Codex: **"Implement the task in `ui/CODEX_PROMPT.md`, following `ui/DESIGN_SPEC.md`. Inspect this repository first."**
4. Inspect actual implementation and run its tests. The reference previews do not replace functional UI tests.

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

## Differences from the previous 28.8k-only handoff

- Four **AT*MODEL-selected** Sportster model themes, with exact colours.
- **80×25 is canonical**, and **all 13** main options remain visible in compact two-column text layout.
- Compact child screens have a shorter banner; up to 15 individual submenu actions fit per screen.
- Phonebook swaps wide split panes for a paged full-width table at 80×25.
- Client Tests version changed from **v0.13 to v0.12**, per the latest instruction; still unavailable.
- New measured `.txt`, `.ans` and PNG previews plus reproducible tests.
- Refined keyboard/navigation, unknown device-state handling, and security guidance.
