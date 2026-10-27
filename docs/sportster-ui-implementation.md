# Sportster UI implementation

Release integration, 27 October 2026: bundled with v0.12 Client Tests and the
device-password variable rename. The historical placeholder descriptions below
describe the earlier working tree; v0.12 enables D on serial and provides a
read-only Client Tests listing in the browser. See `docs/v0.12-plan.md`.

Working-tree implementation, 26 October 2026. These changes are intended for the
next bundled release. No commit, branch change, version bump or upload was made.
The pre-existing work in the checkout is retained.

## Implementation boundaries

- `lib/config/src/menu.cpp` extends the shared terminal used by USB, RS232 and the
  browser. `cell_grid.*` provides bounded character cells, independent colour roles,
  single-cell CP437/ASCII rendering and streamed rows. Trusted rendering escapes
  never pass through the untrusted command-output sink.
- `ui/menu-definition.json` and `ui/theme-tokens.json` remain authoritative for
  labels, IDs, hotkeys, order, availability and RGB colours. Production command
  bindings live in `lib/config/ui-bindings.json`. `scripts/generate_ui.py` validates
  the complete mapping and produces `ui_generated.h` and demo-only `ui-data.js`.
  Builds and preview generation regenerate these files; CI checks for staleness
  before building. No JSON parser or new HTTP endpoint is added to the modem.
- `src/main.cpp` supplies `UiServices` from the real Phonebook, journal and RS232
  state. The footer identifies the actual active profile rather than claiming a
  known stored profile. Missing link/baud/rate values are Unknown/--. Screens do
  not parse printed AT responses to discover device state.
- The Phonebook's existing number/host/port/protocol/speed format is unchanged.
  NAME displays the saved host. Number is the stable selection key, so compaction
  and paging do not switch the selected record. A damaged/unavailable store is
  distinguished from an empty list, and the product's existing factory fallback
  remains visible with a warning. No design-image contacts are installed.

## Layout and input

80x25 Main uses the measured five-row red wordmark, model accent and all 13 entries
in two text columns. Settings (13), Wi-Fi (12) and Hardware (15) fit one screen.
Phonebook uses nine visible entries, selected summary and contextual actions.
120x45 and larger use Main cards/descriptions, modem cell art and Phonebook action
and details panes. Intermediate dimensions stay compact. Smaller terminals page
their lists; dimensions below 20 columns or eight rows use a minimal bounded view.
The supported viewport is 1-132 columns by 1-50 rows, with larger reported terminal
sizes capped to that viewport.

ANSI/CP437 serial defaults to 80x25 and accepts standard `CSI 8;rows;cols t` reports.
The renderer requests those reports with `CSI 18 t` at entry and periodically when
idle; unsupported clients retain the default. `AT*SIZE=columns,rows` sets a local
override, and `AT*SIZE?` reads it. Plain mode emits no terminal escapes or idle
redraw spam. Its output remains a transcript rather than a positioned screen.
The browser uses CP437-to-Unicode conversion and exact RGB; serial ANSI uses safe
ASCII and approximate 16 colours. A 256-colour renderer mode is also supported.

Frames are streamed a row at a time, even for one-byte reads. Rendering coalesces
state changes, honours browser backpressure, disables wrap during positioned output,
and emits no lower-right newline. Resize cancels a partial old-size CSI before
building a new frame. Exit, DTR loss and guarded escape restore cursor/attributes.
Export has a separate paged transcript to preserve complete replayable lines.

| Context | Controls |
|---|---|
| Main | Arrows select/switch columns, Enter opens, 1-9/A-C opens by hotkey, D reports unavailable, Q/F10 exits |
| Child menus | Up/Down, Enter or displayed key; Esc/Q Back; F2 Main |
| Main shortcuts | F1 Help; F2 Status; F3 Phonebook; F4 Settings |
| Phonebook | Up/Down, PgUp/PgDn, I/Enter details; 1 List, 2 Add/edit, 3 Delete, 4 Dial |
| Forms | Printable input, cursor arrows, Home/End, Backspace/Delete, Tab across comma-separated fields, Ctrl-U clear; Enter reviews; Esc cancels |
| UI confirmation | Y confirms; N/Esc cancels; expires after 30 seconds |
| Results/help | PgUp/PgDn; Enter continues/pages or returns; Esc/Q Back |
| Everywhere | Ctrl-C exits; running services keep their existing cancellation prompts |

Bare Esc uses a timeout separate from fragmented CSI input. Geometry reports are
decoded before forwarding service-prompt input, preventing a late report from
entering a password or cancelling a running operation. Menu hotkeys do not capture
form typing. A trailing Enter after a hotkey cannot submit an empty form, approve a
change or dismiss the opened result. Selection/scroll state survives Back, model
changes and resize. Secret input is masked and temporary/pending buffers are wiped
on cancellation, timeout, exit and disconnect, including time spent in Help.

## Service behaviour and limitations

All 57 nested actions retain their existing production command/service bindings.
Model reads the active `session.port().modem().model()` state. The existing
`AT*MODEL=14400|28800|33600|56000` setter persists immediately with save rollback;
the UI confirms it first. Model changes affect identity and the next call's
default reporting speed; explicit overrides and current-call reporting are retained.
The UI explicitly displays an unknown model for an unrecognized value.

Delete/dial require a real selected entry, confirmation and a recheck that the
entry has not changed. Other consequential profile/network/password/OTA-policy/log
changes get a UI review where their service lacks a confirmation. Browser Wi-Fi
off/forget and factory/restart retain their existing service-owned confirmations.
Browser dial still transfers the operation to the idle RS232 host; serial dial
uses the existing departure flow. Results come from the service, including failures
and timeouts, and busy operations cannot be activated twice.

Client Tests (v0.12) is always unavailable and is skipped by navigation. RS232
takeover tests remain listed but cannot execute from RS232; use USB or the browser.
An unattached Phonebook reports the missing `UiServices::phonebook` hook. Separate
saved names, notes, login credentials, init strings and compression details do not
exist in the current Phonebook and are not fabricated. The synthetic browser DEMO
does not implement production writes; it identifies unavailable actions and uses
`demo.invalid` for its explicitly simulated call. D cannot dial in the demo either.

## Browser protocol

The existing authenticated WebSocket advertises `hello.size: true`. Its owner
sends `{"t":"size","cols":120,"rows":45}` as a canonical text envelope, distinct
from binary terminal input. The server task validates/coalesces dimensions per
client; only the modem loop changes a terminal, and an observer cannot resize an
owner's UI. The page fits a minimum 80x25 grid in small windows and adds cells at a
readable base font size for larger windows. Old firmware without the capability
keeps the fixed 80x25 behaviour. See `web/PROTOCOL.md` for bounds and framing.

## Verification

Run these with the repository's normal Python/PlatformIO/Node tools:

```powershell
python scripts/generate_ui.py --check
python -m unittest discover -s ui/tests -v
python -m pytest test/scripts -q
pio test -e native
npm --prefix web test
pio check -e native --fail-on-defect=medium
pio run -e s3
pio run -e s3 -t buildfs
python scripts/image_manifest.py .pio/build/s3
python scripts/check_version.py
python scripts/make_preview.py
```

The native screen emulator consumes actual output bytes, including single-byte
chunks, cursor/SGR/wrap behaviour and physical screen bounds. Tests cover the six
required geometries plus tiny screens, full inventories, four/unknown model themes,
failed saves, disabled navigation, forms/Back, confirmations, password safety,
async prompts, delayed geometry replies, export line integrity and Phonebook
0/1/9/10/64-entry boundaries. Browser tests cover geometry, the disabled demo entry
and confirmed simulated dialing. Static handoff tests remain separate contracts.

For optional visual captures, create `.pio/ui-snapshots`, set
`USR_UI_SNAPSHOTS=1`, and run `pio test -e native -f native/test_config`, then
`python ui/tools/render_runtime.py` with Pillow installed. It renders the actual
emulator cells to PNGs for review. The layout review caught and corrected a wide
Client Tests card/footer overlap; a regression assertion covers that boundary.

Hardware uploads, live E2E and physical PC/Amiga observation have not been run for
this implementation. Native Chrome access timed out during manual preview checking;
runtime captures and automated Node checks were used. The generated standalone
preview is at `web/preview.html`. Device acceptance remains required before release;
earlier v0.11 bench results do not certify these UI changes.

Completed host checks: **339 native tests, 22 Node tests, eight script tests and
five handoff contract tests passed**. The final cursor/style/resize adjustment
also passed all 22 terminal tests. Firmware and LittleFS builds, image identity/
manifest validation, version/changelog validation, generated-file checks and
clang-format checks passed. Cppcheck passed the repository's medium-severity gate
with no high or medium findings; low-severity style suggestions remain. VERSION
is still 0.11. Runtime Main/Phonebook/submenu captures were inspected, including
the corrected 120x45 card/footer boundary.
