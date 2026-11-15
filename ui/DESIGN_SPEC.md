# Configuration list design

`AT*CONFIG` and the browser configuration screen print the same wrapping list. There is no cell grid, cursor positioning, box drawing, arrow navigation or size-dependent layout. The terminal owns wrapping and scrollback. Plain text and ANSI colour have identical content and keyboard behaviour.

## Inventory and navigation

There are 13 main entries and 78 nested items: 91 production bindings. Main letters A-M are Status, Phonebook, Settings, Sounds, Diagnostics, Call History, Wi-Fi and Clock, Updates and Reset, Export Configuration, About, Hardware Tests, Client Tests and Command Reference. Submenus use A-O in inventory order. Settings has 13 items; Wi-Fi and Clock, Hardware Tests and Client Tests each have 15. Profile editing has 25 modem fields and 64 S-registers, with S1 read only.

Press an item's letter to open it. Lists show up to 15 options per page, with P for the previous page and R for the next. X or Esc goes back; Q also goes back and exits at the root. Ctrl-C exits immediately; Ctrl-L prints the current list again. Letter selection is case insensitive. Arrow and function key escape sequences and Alt+letter are ignored. A lone Esc is recognised after 60 ms so an escape sequence cannot accidentally run a menu action.

## Friendly names, values and help

`ui/menu-definition.json` supplies inventory and letter keys; `lib/config/ui-bindings.json` supplies each friendly name, help, type, units, limits, choices and command template. `scripts/generate_ui.py` emits the same metadata to C++ in `lib/config/src/ui_generated.h` and JavaScript in `web/src/ui-commands.js`, plus demo inventory in `web/src/ui-data.js`. Never hand-edit these outputs.

Types are `enum`, `int`, `string`, `bool`, `ip`, `action` and `secret`. Numeric metadata has `unit`, `min` and `max`; string constraints may include UTF-8 byte lengths and a full-match `pattern`. `displayScale` and `displayUnit` convert wire values for presentation: ring and connection delays use seconds in the menu and milliseconds in commands. `values` contains wire values, friendly labels and descriptions. `fields` and `variants` describe multi-field commands and alternative operations. Optional fields, custom values and read-only fields are explicit. Native metadata tests compare firmware tables and a source fingerprint; generator and web tests validate the generated schema.

Each menu item prints its friendly name, available current value, units and help. Enumerations show their friendly label before the technical value; for example `US Robotics Sportster 14.4k (14400)`. Secret values show `hidden`. Actions without a current value show their help. Public network values come from an explicit whitelist; credentials and stored hashes never enter it. Current values are read again whenever the list is printed, without running commands or changing settings.

## Editing and commands

Enumerations print lettered friendly choices and descriptions, including additional pages for time zones and sounds. Select a letter and press Enter to confirm. Custom-capable choices offer S to enter text. Text forms accept Enter, Backspace and Ctrl-U; they echo only new characters and mask secrets with stars. Esc cancels; X is ordinary text inside a text field. Multi-field commands prompt for each field in order; variants first offer a lettered operation list. Templates assemble the raw command, including punctuation, after all fields are entered. Commands remain responsible for definitive validation and storage errors.

Changes that need review retain Y/N confirmation. Temporary secret input is wiped after submission, cancellation, timeout, disconnect and exit. Confirmations expire after 30 seconds and secret forms after two minutes. Wi-Fi join uses the existing masked command prompt and connection checks. Wi-Fi off/forget in the browser retains the network service's warning and rollback handling. Restart and factory reset retain the command handler's confirmation.

Phonebook A lists saved entries using paged command output. Add/edit prompts for number, host, port, protocol and speed; delete/dial asks for a saved number and confirms it. Delete/dial compares the reviewed entry again before dispatch so a changed entry must be reviewed again. Missing and damaged storage is reported by the existing handlers.

Service output is sanitised, buffered up to 32 KiB and paged in 15 logical lines, capped at 4096 bytes per page for unusually long lines. The terminal wraps those lines at its own width. Enter/R advances, P goes back and X/Esc returns. Export preserves complete replayable command lines. Overflow is explicitly reported. Asynchronous output and masked prompts stream while the command is running; Esc cancels a prompt or waiting command. Output remains pull-based and supports one-byte transport chunks. Interactive diagnostics own the stream until they finish, including binary tests.

Client Tests run on serial only; the browser lists them for reference and refuses to dispatch them. RS232 takeover tests remain visible and unavailable from that same port. A menu reserves the line and refuses incoming calls, without raising carrier for direct `AT*CONFIG`. Five minutes without a key closes the screen and frees the line; active tests and commands have their own timeouts.

## Style and terminal size

The configuration list uses ASCII with optional ANSI SGR colour, including the active model accent when RGB colour is enabled. It never emits a cursor address, erase-screen command, size query, wrap-mode change or cursor visibility control. Idle lists emit nothing. CP437 and Latin-1 diagnostics and the BBS byte-to-UTF-8 conversion remain separate from menu rendering.

`AT*SIZE`, browser size frames and received serial size reports still store transport dimensions. They never clip, reflow or repaint the menu. Browser sizing and Telnet negotiation may use them independently. Client Tests retain their own display and size measurements. There is no menu size selector.

## Verification

`native/test_config` checks plain/ANSI lists at widths from 1 to 132, letter navigation, all profile pages, friendly enums, current values, units, forms, confirmations, paging, sanitisation, timeouts and binary diagnostics. The `test_config_services` canary tour visits all ten screens and every submenu action at 20, 40, 80 and 132 columns in both styles, while checking Wi-Fi/device passwords and stored hashes never appear. On-board `embedded/test_config` and serial `e2e/test_config.py` check the real stream. The browser demo and `ui/tools/preview.py` are illustrative lists, with no invented device values. `ui/tests` checks the inventory, keys and generated preview contract.
