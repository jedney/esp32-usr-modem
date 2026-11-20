# Configuration screen and menus

`AT*CONFIG` prints the same wrapping list on serial ports and in the browser terminal. The browser menu bar uses the same inventory and command metadata, with dialogs for input. There is no cell grid, cursor positioning, box drawing, arrow navigation or size-dependent layout in the text menu. The terminal owns wrapping and scrollback. Plain text and ANSI colour have identical content and keyboard behaviour.

## Files in this folder

- `menu-definition.json`: the menus, their nested groups and letter keys (A-E at the top, A-O below).
- `theme-tokens.json`: the Sportster model colours for the configuration list and the web interface.
- `tools/preview.py` writes the illustrative lists in `previews/` (`python ui/tools/preview.py`); they hold no live modem data.
- `tests/` checks the inventory, keys and preview contract (`python -m unittest discover -s ui/tests`).

`python scripts/generate_ui.py` combines these with `lib/config/ui-bindings.json` (see below).

## Inventory and navigation

The shared top-level order is Status, Phone Book, Call History, Settings and Help (A-E in AT*CONFIG). Settings contains General Settings, Sounds, Wi-Fi, Clock, Updates, Reset and Export Configuration. Help contains About, GitHub, Command Reference and Diagnostics. Diagnostics retains its log and reset-information tools plus Hardware Tests and serial-only Client Tests; the browser omits Client Tests. Nested groups have Back controls in the browser and X/Esc in the terminal. There are 96 production bindings. Submenus use A-O. General Settings has 13 items, Wi-Fi has 12, Clock has three, Updates has six and Reset has two. Profile editing retains 25 modem fields and 64 S-registers, with S1 read only.

Upload image is under Settings > Updates. DCD/RI/CTS toggles and Stop test are under Help > Diagnostics > Hardware Tests; only active line tests show the toggles and only active hardware tests show Stop test. Help > Log out, Upload image and Recent console history are browser-only.

Press an item's letter to open it. Lists show up to 15 options per page, with P for the previous page and R for the next. X or Esc goes back; Q also goes back and exits at the root. Ctrl-C exits immediately; Ctrl-L prints the current list again. Letter selection is case insensitive. Arrow and function key escape sequences and Alt+letter are ignored. A lone Esc is recognised after 60 ms so an escape sequence cannot accidentally run a menu action.

## Friendly names, values and help

`ui/menu-definition.json` supplies inventory and letter keys; `lib/config/ui-bindings.json` supplies each friendly name, help, type, units, limits, choices and command template. `scripts/generate_ui.py` emits the same metadata to C++ in `lib/config/src/ui_generated.h` and JavaScript in `web/src/ui-commands.js`, plus demo inventory in `web/src/ui-data.js`. Never hand-edit these outputs.

Types are `enum`, `int`, `string`, `bool`, `ip`, `action` and `secret`. Numeric metadata has `unit`, `min` and `max`; string constraints may include UTF-8 byte lengths and a full-match `pattern`. `displayScale` and `displayUnit` convert wire values for presentation: ring and connection delays use seconds in the menu and milliseconds in commands. `values` contains wire values, friendly labels and descriptions. `fields` and `variants` describe multi-field commands and alternative operations. Optional fields, custom values and read-only fields are explicit. Native metadata tests compare firmware tables and a source fingerprint; generator and web tests validate the generated schema.

Each terminal menu item prints its friendly name, available current value and units on one logical line, wrapping only when the terminal is too narrow. Explanatory help and separate range/unit/length lines are omitted from lists. Help remains in the shared metadata for browser hover tooltips. Enumerations show their friendly label before the technical value; for example `US Robotics Sportster 14.4k (14400)`. Secret values show `hidden`. Public network values come from an explicit whitelist; credentials and stored hashes never enter it. Current values are read again whenever the list is printed, without running commands or changing settings.

## Editing and commands

The browser presents the same inventory as a compact single-row menu with icons and a More disclosure for overflow. Dropdown entries have a single name line, with help available as a hover tooltip. Client Tests are omitted. Light/dark tokens preserve the Sportster colours and IBM Plex Mono terminal. Command actions require an open, idle terminal acknowledged by the firmware, including its consumed-byte count; partial input, prompts, tests and calls disable them with an explanation. Hardware Stop and active line toggles remain available. The rest of the page (masthead, terminal header, call buttons, status bar) is in [web/README.md](../web/README.md).

Browser dialogs use labelled fields, adjacent help, number units, friendly enumerations, grouped time zones, filtered long lists, schema validation and a live advanced command preview. Variants select an operation; profile editing selects one register or setting at a time. Values are new choices to apply, not snapshots of live settings. Device-password previews are masked and temporary inputs are wiped on completion, cancellation, access loss and timeout. Wi-Fi password entry and all command confirmations stay in the terminal. Phonebook data is read through `AT*PHONEBOOK?`, displayed safely as a table and edited through the same commands; its results still appear in the terminal.

Enumerations print lettered friendly choices without descriptions, including additional pages for time zones and sounds. Select a letter and press Enter to confirm. Custom-capable choices offer S to enter text. Text forms accept Enter, Backspace and Ctrl-U; they echo only new characters and mask secrets with stars. Esc cancels; X is ordinary text inside a text field. Multi-field commands prompt for each field in order; variants first offer a lettered operation list. Input prompts retain units, ranges and length limits but omit explanatory help. Templates assemble the raw command, including punctuation, after all fields are entered. Commands remain responsible for definitive validation and storage errors.

Changes that need review retain Y/N confirmation. Temporary secret input is wiped after submission, cancellation, timeout, disconnect and exit. Confirmations expire after 30 seconds and secret forms after two minutes. Wi-Fi join uses the existing masked command prompt and connection checks. Wi-Fi off/forget in the browser retains the network service's warning and rollback handling. Restart and factory reset retain the command handler's confirmation.

Phonebook A lists saved entries using paged command output. Add/edit prompts for number, host, port, protocol (Telnet, Raw TCP or SSH (basic), whose description says it is for hobby use only) and speed; delete/dial asks for a saved number and confirms it. Delete/dial compares the reviewed entry again before dispatch so a changed entry must be reviewed again. Missing and damaged storage is reported by the existing handlers.

Service output is sanitised, buffered up to 32 KiB and paged in 15 logical lines, capped at 4096 bytes per page for unusually long lines. The terminal wraps those lines at its own width. Enter/R advances, P goes back and X/Esc returns. Export preserves complete replayable command lines. Overflow is explicitly reported. Asynchronous output and masked prompts stream while the command is running; Esc cancels a prompt or waiting command. Output remains pull-based and supports one-byte transport chunks. Interactive diagnostics own the stream until they finish, including binary tests.

Client Tests run on serial only (console, RS232 and USB modem port). The browser menu bar omits them; the browser terminal's `AT*CONFIG` list can show the section but refuses to run its tests with a serial-only explanation. RS232 takeover tests remain visible and unavailable from that same port. A menu reserves the line and refuses incoming calls, without raising carrier for direct `AT*CONFIG`. Five minutes without a key closes the screen and frees the line; active tests and commands have their own timeouts.

## Style and terminal size

The configuration list uses ASCII with optional ANSI SGR colour, including the active model accent when RGB colour is enabled. It never emits a cursor address, erase-screen command, size query, wrap-mode change or cursor visibility control. Idle lists emit nothing. CP437 and Latin-1 diagnostics and the BBS byte-to-UTF-8 conversion remain separate from menu rendering.

`AT*SIZE`, browser size frames and received serial size reports still store transport dimensions. They never clip, reflow or repaint the menu. Browser geometry is 80-65535 columns by 25-65535 rows and feeds the browser session and live Telnet NAWS independently of the menu. `AT*STYLE=CP437|UTF8` selects BBS encoding independently of PLAIN/ANSI menu colour. Client Tests retain their own display and size measurements. There is no menu size selector.

## Verification

`native/test_config` checks plain/ANSI lists at widths from 1 to 132, letter navigation, all profile pages, friendly enums, current values, units, forms, confirmations, paging, sanitisation, timeouts and binary diagnostics. The `test_config_services` canary tour visits all fourteen screens and every submenu action at 20, 40, 80 and 132 columns in both styles, while checking Wi-Fi/device passwords and stored hashes never appear. On-board `embedded/test_config` and serial `e2e/test_config.py` check the real stream. The browser demo, like the previews, shows illustrative lists with no invented device values.

## Login page and logout

The login page (`web/src/login.html`) reuses the terminal's masthead, logo, IBM Plex Mono labels, theme tokens, `.field` inputs and the primary button, and follows the browser's light or dark appearance. The masthead sits at the top left, as on the terminal; one card, centred horizontally and vertically in the space below it, holds the heading "Log in", the line "Log in as admin with the device password", an alert line for the last error and a real form: labelled Username (`autocomplete="username"`, no capitals or spell check, initially empty) and Password (`autocomplete="current-password"`) fields and a full-width Log in button. Enter submits. It is 420 px wide at most and fills a narrow window with a 16 px margin. Errors: "Invalid username or password.", "Too many attempts. Wait a second, then try again.", "Too many browsers are logged in…" and "The modem could not record the login. Try again."

Help > Log out is the last Help item. It needs no terminal, asks for no confirmation and stays enabled in every state. While an accepted upload is still sending, the page stays loaded and shows a "Logged out" card with "Finishing update…", then the result and a Log in button.
