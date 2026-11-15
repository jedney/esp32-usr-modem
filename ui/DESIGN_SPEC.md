# USRobotics Sportster — ANSI/BBS configuration UI

C opens Client Tests on serial; the browser lists them without execution. The catalogue has 78 nested actions.

**80×25-first, four runtime models**

**Primary target:** 80 columns × 25 rows (including outer border and footer).

**Enhanced target:** 120–132 columns and sufficient rows for the richer two-column cards.

This document specifies the visual, keyboard, layout, safety and service-binding contract for an implementation in the **existing application**. It does **not** prescribe a programming language, a web framework, an AT command format or a device API that has not been inspected in the repository.

## 1. Source of truth and references

1. The user's latest requirements and this specification, including exact 80×25 geometry, four models and **Client Tests**.
2. `menu-definition.json`: stable IDs, exact option labels, menu tree, UI hotkeys and disabled status.
3. `theme-tokens.json`: palette and exact model accents.
4. `previews/*.txt`: **measured** 80-cell × 25-row terminal exemplars. These are **layouts only**, not operational menus.
5. `../lib/config/ui-bindings.json`: command bindings and typed, friendly metadata for every menu ID; `../scripts/generate_ui.py` emits the C++ catalogue and `../web/src/ui-commands.js`.

When a preview disagrees with real modem state, **real device values prevail**; if values are missing, show `--`, `Unknown`, or `Unavailable`. Screenshot values such as numbers, dates, serials, COM ports, modem flags, and rates are *not* production defaults.

### 1.1 Typed command metadata

Every one of the 13 top-level entries and 78 nested items has a binding keyed by the stable menu ID. `menu-definition.json` owns the tree, ordering, hotkeys and compact layout labels; `ui-bindings.json` owns the friendly names, help, command syntax and input constraints. Friendly text is authored in this binding source and emitted unchanged to both languages. C++ exposes it through `Action::metadata` (`UiMetadata`); JavaScript imports the default array from `ui-commands.js` and indexes entries by `id`. The synthetic demo's separate `ui-data.js` keeps its layout and theme catalogue.

| Field | Meaning |
|---|---|
| `friendly`, `help` | Plain name and one or two sentences explaining the choice and any saving, disconnection or confirmation behaviour. |
| `type` | `enum`, `int`, `string`, `bool`, `ip`, `action` or `secret`. An action runs without a value or opens a route; a string may also describe a multi-field form. |
| `unit`, `min`, `max` | Unit and inclusive integer limits in command units. Enumerations can also have a unit, such as bits per second. |
| `minLength`, `maxLength`, `pattern` | UTF-8 byte limits and a portable Python/JavaScript regular expression matched against the entire value. Device passwords use printable ASCII, so bytes equal characters. These limits apply to canonical field values; normalise a hostname to lower case and remove a trailing `.local` before checking its 1–32 byte label. |
| `values` | An ordered array of `{value, label, description}` choices. Values are command strings, including numbers expressed as strings; labels and descriptions explain each choice. Booleans use the handler's two wire tokens, such as `ON` and `OFF`. |
| `template` | A raw command without `AT`, with `{value}` for a scalar or named placeholders matching `fields[].id`. Prefix `AT` once when sending through terminal input. Routes and read-only fields have an empty template. |
| `fields` | Typed fields with their own `id`, friendly text and constraints. Composite commands substitute these fields; the profile route instead gives each editable field its own template. |
| `variants` | Alternative complete command forms, each with an ID, friendly text, template and any typed fields. The first variant is also the binding's default template and fields. For example, static addressing and DHCP are separate forms. |
| `optional`, `readOnly` | An optional trailing field may be omitted along with its preceding separator; it uses the handler's default. A read-only field is displayed without an edit command. |
| `allowCustom` | An enumeration also accepts a custom value validated by the firmware, such as a POSIX time-zone rule. |
| `displayUnit`, `displayScale` | Multiply the wire value by this scale for display, and divide when sending. Ring and connection delays store 0–60000 milliseconds and display 0–60 seconds with scale `0.001`. |
| `screen`, `target`, `command`, `input`, `secret`, `leave`, `rs232Unavailable` | Existing dispatch and session flags. `command` remains the text form's literal prefix. The generated JavaScript adds `id`, `key` and `available` from the menu inventory. |

For example, the phonebook template is `*PHONEBOOK={num},{host},{port},{proto},{speed}`. Its fields constrain the number to 1–11 digits excluding reserved built-in numbers, the host to 63 bytes, the port to 1–65535, the protocol to Telnet or raw TCP, and the optional speed to the supported line-speed table. Omitting speed also omits the final comma. The maximum complete command remains 128 bytes after `AT`; field limits do not override that parser limit.

The profile route describes all 25 settings and 64 S-registers. Reserved setting values are excluded, S1 is read-only, S3 permits only non-printing codes in 0–127, and S4/S5 permit 0–127. Auto-answer accepts 0–255 rings, although values above eight answer on ring eight. Time zones list all 16 built-in names and permit custom POSIX rules. Cross-field rules such as a valid static subnet or a private dial-up /30 remain firmware checks and are explained in the form help.

The generator rejects missing text, invalid types, reversed ranges, invalid patterns, duplicate choices and unknown placeholders. `python scripts/generate_ui.py --check` checks all three generated outputs. Native `test_config` checks the JSON source fingerprint, verifies every nested template against the production registry and compares choices and limits with the C++ tables and validators. Python and browser tests cover schema failures and portable validation patterns.

## 2. Brand and runtime model selection

The wordmark is always prominently **red**. The model line immediately beneath the full-size main-menu logo, or adjacent to the compact submenu wordmark, must be **selected at runtime using the existing device/application state associated with `AT*MODEL`**:

| Active model | Exact displayed subtitle | RGB accent |
| --- | --- | --- |
| 14.4k | `Sportster 14.4k Series` | `#6f327f` |
| 28.8k | `Sportster 28.8k Series` | `#448FA1` |
| 33.6k | `Sportster 33.6k Series` | `#349b67` |
| 56k | `Sportster 56k Series` | `#3F668E` |

`AT*MODEL` is the stated model selector/source. **Do not assume command parameter syntax, response values, baud implications, or that selecting it necessarily changes hardware.** Inspect the existing parser/service and bind the display to its *effective active model*. A refresh/model change should update the subtitle and model indicator without resetting navigation. If the model cannot be obtained, use an explicit `Sportster model unknown` state, not a silent 28.8k default. Nominal model speeds are identifiers; they are **not** evidence of negotiated connection rate.

Supporting palette: red `#CC2229` logo; black `#030507` screen; frame white `#D1D5DF`; titles cyan `#20DDEB`; hotkeys green `#47DB90`; F-key hints blue `#278BFF`; body `#D6DAE6`; muted `#8B92A2`; disabled grey `#696E7B`; warning yellow `#F0DA63`; error red `#F43F4F`. Exact model RGB requires a truecolor-capable renderer; nearest ANSI 256/16-colour equivalents are acceptable on restricted terminals. Never depend solely on colour to indicate selection or disabled state.

Use a **true fixed-width cell grid**. No proportional fonts, emojis (ambiguous display widths), CSS wrapping, non-cell-aligned borders, rounded modern cards or blink/scanline effects that reduce legibility. Prefer terminal-supported Unicode box characters (`╔═╗║╚╝╠╣│█`); provide an ASCII `+ - | #` fallback when Unicode or character widths are unreliable.

## 3. Display-mode rules

| Terminal size | Required behaviour |
| --- | --- |
| **80×25** | Canonical compact design. Exactly 25 rows and max 80 display cells per row; no automatic line wrapping, horizontal scrolling or footer loss. All 13 main entries fit in two **text-only** columns. |
| 81–119 cols | Choose the compact design or gradually allocate extra width; never assume a 120-column layout. |
| ≥120 cols, adequate height | Enhanced rendition: mirrored modem art, prominent header, item descriptions, separate Phonebook entries + actions/details panes, fuller status strip. |
| Fewer than 80 cols or 25 rows | Gracefully degrade to a single-column/paged minimal menu and a one-line warning, or use the application's established viewport/scrolling convention. Keep key actions reachable and never emit overlong lines. |

**Terminal safety:** width means terminal **cell width**, not Unicode code point count or UTF-8 byte count. Validate by display width (`wcwidth` or platform equivalent). Avoid placing combining / double-width glyphs across cell boundaries. When printing at physical `(80,25)`, account for terminal automatic-wrap/scroll behaviour: use positioned/cell-buffer output and do not append a newline that scrolls the last row. Hide the cursor during redraw and restore it safely. Detect resize and invalidate only necessary layout regions if available.

### 3.1 Exact 80×25 main menu layout

The canonical screen is in `previews/main-sportster-28.8k.txt` (and one per model). **Row numbers are 1-based, inclusive.**

| Rows | Content |
| --- | --- |
| 1 | Outer top border, 80 cells. |
| 2–6 | Prominent five-row red `USRobotics` ANSI/cell-art wordmark (the SVG silhouette is the reference; cell art need not trace it pixel for pixel). |
| 7 | Centred model subtitle in exact active model accent. |
| 8 | `[ MODEM CONFIGURATION - MAIN MENU ]` framed title. |
| 9–15 | **Seven compact rows, two columns**. Left options 1–6; right 7–D; one blank left cell row beside D. Text labels only—**no per-option description or icon** at 80 columns. |
| 16 | Optional breathing row. |
| 17 | Thin separating rule. |
| 18 | Navigation hint. |
| 19 | Client Tests status text. |
| 20 | Help/status separator. |
| 21 | Contextual function key hints. |
| 22 | Dynamic model/profile/link summary, with placeholders for missing values. |
| 23 | Status/feedback message (preview disclaimer is not production UI copy). |
| 24 | `Command >` / focus indicator as appropriate. |
| 25 | Bottom frame. |

The **two compact menu columns** must be 37–38 cells each, with a fixed divider. Use a `>` selection cursor or inverse video, **not colour alone**. Preserve full labels (including `Wi-Fi and Clock`, `Export Configuration`, `Client Tests`) without clipping. Disabled `C` remains grey, displays `[X]` and/or `UNAVAILABLE`, is skipped by arrow navigation, and cannot activate anything if typed. The label displays on the main menu even when the feature cannot run.

All top-level IDs and UI keys are defined in `menu-definition.json`: `1` Status, `2` Phonebook, `3` Settings, `4` Sounds, `5` Debug, `6` Call History, `7` Wi-Fi and Clock, `8` Updates and Reset, `9` Export Configuration, `A` About, `B` Hardware Tests, serial-only `C` Client Tests, and `D` Command Reference.

**Main-menu layout priority at 80×25:** brand and model identity → all options → selection + disabled state → navigation → a reliable minimal footer. The wide-preview modem illustrations, individual option borders, signal icons, explanatory sentences and decorative tagline are intentionally **omitted**; putting them all into 25 rows would compromise function.

### 3.2 Compact submenus

In compact submenus, use a **single-line red `USRobotics` brand plus colour-coded active model** rather than the five-row logo. Keep breadcrumb `MAIN > [SECTION]` near the top. Use a single-pane list, short labels, visible selection cursor, and fixed bottom controls.

The largest list is **Hardware Tests (15 items)**; Settings has 13 and Wi-Fi and Clock has 12. All three can be shown in **one row per action** between rows 6 and 20, with the compact header/footer, as demonstrated in `previews/settings-28.8k.txt` and `previews/hardware-tests-28.8k.txt`. For future additions, scroll or paginate without moving the footer; preserve selected ID and scroll offset when returning from a child screen.

Use a stable UI-local key sequence `1`–`9`, `A`–`F` for submenu actions where appropriate. Do not use `Q`, `Esc` or a conflicting form entry as an action shortcut. User-facing text and stable machine IDs remain in `menu-definition.json`, including separately listed `Load stored/factory profile` **and** `Load factory profile`.

### 3.3 Compact Phonebook

The enhanced/wide screen has the entries table at left, four actions and dynamic selected-entry details on right. **Do not attempt both full-sized panes on 80×25.** Instead the compact reference `previews/phonebook-28.8k.txt` uses:

- Rows 1–3: compact red brand, accent subtitle and breadcrumb.
- Rows 4–6: Phonebook title and fixed-cell columns `#`, `NAME`, `PHONE NUMBER`, `RATE`.
- Rows 7–15: up to **nine visible entries**, with selection, page count and Up/Down/PgUp/PgDn support. Overflow entries paginate; they do not horizontally scroll.
- Rows 16–19: selected-entry summary and action shortcuts: `1` List, `2` Add/Edit, `3` Delete, `4` Dial; `I` opens a full **Details** view when an entry exists. Actual number, speed and name come only from storage/service.
- Rows 20–25: navigation, F-key hints, feedback, command prompt and bottom border.

Empty-list state says `No saved entries` if the service genuinely returns zero rows. An unconnected service says `Phonebook unavailable` or `Not loaded`. The sample preview explicitly shows **unbound** state. Never populate real storage with the fictional BBS contacts from the concept mock-up; a test fixture must say **DEMO**. `Delete` and `Dial` require an actual selected entry and must be disabled when none exists. `Dial` and `Delete` need confirmations, using the existing device's permitted flow; neither should claim success until the service confirms it. Mask stored credentials (if any); don't echo phonebook passwords in details or log output.

## 4. Complete menu inventory

**`menu-definition.json` is authoritative for exact IDs, labels, order and nested items.** All 13 top-level options and all **78** nested entries are present. In particular:

- **Phonebook (4):** List entries; Add / edit entry; Delete entry; Dial.
- **Settings (13):** Current and stored profiles; Edit profile / registers; Model; Auto-answer rings (S0); Auto-answer destination; Host baud; Data throttle; Save active profile; Load stored/factory profile; Load factory profile; Power-on profile; Stored number; Terminal style.
- **Sounds (2):** List sounds; Play.
- **Debug (4):** View log; Verbosity; Clear debug log; Reset / crash information.
- **Wi-Fi and Clock (12):** Network status; Scan; Join; Wi-Fi on / off; Forget network; IP configuration; Network name; Time; Time zone; NTP server; DNS lookup; Ping.
- **Updates and Reset (7):** Firmware version; OTA status; OTA policy / window; Change device password; Restart; Factory reset; Web interface.
- **Hardware Tests (15):** Guided tests; Pin map; Board information; Files; LEDs; Amplifier; Test volume; Tone / sweep; DTMF; Pulse; Potentiometer; RS232 lines; Loopback; Bridge; Output signal.

Status, Call History, Export Configuration, Command Reference and About have their own screens without nested inventories in the catalogue. Avoid creating fictitious additional operations for them.

## 5. Interaction contract

| Context | Keys | Expected action |
| --- | --- | --- |
| Main menu | `1`–`9`, `A`–`C` | Open exact ID from JSON, case-insensitive. |
| Main menu | `C` | Open the Client Tests reference/menu; execute on serial only. |
| Main and lists | `↑`, `↓` | Select previous/next enabled item; skip unavailable. |
| Two-column main | `←`, `→` | Change column to nearest enabled row; predictable behavior on blank seventh left row. |
| Menus | `Enter` | Open selected enabled item. |
| Submenus | `Esc` or `Q` | Back one level, after cancelling only when safe. |
| Main | `Q` or `F10` | Respect host exit convention; ask confirmation if disruptive. |
| Everywhere | `F1` | Contextual help, if actually implemented. |
| Main | `F2/F3/F4` | Status / Phonebook / Settings, if supported. |
| Phonebook | `PgUp/PgDn`, `I` | Page list and show details when selection exists. |
| Text forms | printable keys, `Tab` | Input normally; **no global menu hotkeys while typing**. |

**F-key labels must be truthful and contextual.** If a terminal/transport does not deliver a function key, omit its advertised shortcut or show an equivalent plain key. Avoid global `Enter` interception for multiline/edit fields. An edit form must support cursor movement, backspace/delete, input validation and safe cancellation without accidentally dialing or resetting. Preserve selection and scroll position on `Back`; preserve selected entry by **stable ID**, not index.

Action states: `ready`, `selected`, `disabled`, `busy`, `confirmed`, `succeeded`, `failed`, `empty` and `unknown`. Use live confirmations/results from available services; never present optimistic success for dial, configuration write, Wi-Fi change, firmware update or hardware tests.

## 6. Device/state integration and security

**First inspect the repository.** Find existing terminal renderer, menu/router, `AT*MODEL` handling, profile store, phonebook store, serial/modem services, network interfaces, firmware/OTA API, event/log stream, password-change flow, and test harness. Reuse current conventions. `AT*MODEL` is a *model selection source*, not permission to invent endpoints or call it for every repaint.

Recommended screen/service boundary (adapt names to the repo):

```
Modem/UI state adapter -> active model, carrier/link, flags, speeds, profile
Phonebook adapter     -> list/get/create/update/delete/dial, error results
Settings adapter      -> read/change/save/load profiles and registers
Network/clock adapter -> Wi-Fi/IP/DNS/time/NTP (where supported)
Firmware adapter      -> version/OTA policy/restart/reset/password
Diagnostics adapter   -> logs, test capabilities, test results
Terminal view/router  -> render cells, selection, forms, keyboard focus
```

Render missing data as `--`, `UNKNOWN` or explicit `NOT AVAILABLE`. Never invent model firmware versions, serial numbers, COM/IRQ values, carrier flags, stored contacts, live clock, IP addresses, saved profiles, host baud or negotiated line rates. On capability mismatch, keep the screen navigable but disable the unsupported action and state which service/device hook is needed. The client-tests placeholder is always disabled in this revision.

Sensitive actions:

- Ask for explicit confirmation for **dial, delete, clear logs, change settings/profile, Wi-Fi join/forget, reboot, factory reset, firmware update, password change** as needed according to severity; clearly distinguish non-destructive reads.
- Never display a device password or saved secret in cleartext, include it in debug/log output, export it inadvertently or preserve it in long-lived UI state. Clear sensitive temporary buffers and honor existing secure transport/storage rules.
- Don't treat a visually coloured LED as live measured state unless bound to telemetry. `Unknown` must not be shown as green/connected.
- Protect async operations from duplicate activation and show failure/timeout in the status feedback area.

## 7. Acceptance and verification

1. 80×25 main menu renders **all 13 options**, with no wrapping/clipping and a readable red logo + correctly coloured active model.
2. Four model previews show the required exact subtitle colours; active model reflects existing `AT*MODEL` state, and unknown model is not mislabeled as 28.8k.
3. 80×25 Settings (13) and Hardware Tests (15) fit within the single-pane layout; Wi-Fi and Clock (12) also fits. Future longer lists paginate/scroll cleanly.
4. Phonebook displays real entries, paging, selection, details and four contextual actions. Empty/unsupported states are clear; disabled delete/dial cannot run without selection.
5. Client Tests are listed in the browser but never run there; on a serial session `C` opens them.
6. On wide displays (120–132 cols with enough height), use the two-column design; 80×25 is not simply a shrunken screenshot.
7. Keyboard + focus tests include Up/Down/Left/Right, hotkeys, Esc/Q/Enter, selection on Back, disabled item, and input fields.
8. Device footer uses real reported values; no fabricated sample data in production. No false success or password leakage.
9. Layout and smoke tests run at 80×25, 80×24, 79×25, 100×25, 120×45 and 132×50; test resize and no auto-wrap.
10. Existing project tests pass; report both supported and unsupported service-backed actions and how to run them.

`tools/preview_80x25.py` and `tests/test_layout_contract.py` check the static layout specimens; `native/test_config` and the end-to-end `test_config.py` test the real renderer.
