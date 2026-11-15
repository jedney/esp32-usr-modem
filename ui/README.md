# Configuration list

The configuration screen is a wrapping list in plain text or ANSI colour. `menu-definition.json` defines 13 main entries and 78 nested items, with letter keys A-M and A-O. `lib/config/ui-bindings.json` supplies typed metadata, friendly names, help, units, ranges and command templates for all 91 bindings. See `DESIGN_SPEC.md` for editing, pagination, confirmations and secret handling.

`python scripts/generate_ui.py` generates the firmware catalogue, browser command metadata and demo inventory. `python ui/tools/preview.py` generates the illustrative text lists in `previews/`; they contain no live modem data. `python -m unittest discover -s ui/tests` checks their contract. The firmware stream is tested in `test/native/test_config`, `test/native/test_config_services`, `test/embedded/test_config` and `test/e2e/test_config.py`.
