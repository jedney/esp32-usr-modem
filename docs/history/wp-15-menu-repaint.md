# WP-15 (v2.15): Configuration screen repaint

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high |
| Size | L |
| Branch | `firmware/v2.15-menu-repaint` |
| Depends on | WP-07 (touches `menu.cpp` Esc handling) |
| Firmware change | yes (`lib/config`) |

Follow the session workflow and token rules in [README.md](README.md).
`lib/config/src/menu.cpp` is about 1,450 lines: read only the functions named.

## Findings

### PERF-01 The screen is repainted in full on every key

`lib/config/src/menu.cpp:255-258` (every key sets the dirty flag), `:794-827` (the whole
frame is sent), `lib/config/src/cell_grid.cpp:120-160`. No comparison with the previous
frame. Estimate 2.3-3.5 KB for an 80 × 25 colour frame: about 3 s per key at 9600,
10-15 s at 2400, over a minute at 300. The README offers 300-115200.

1. **Measure first.** In `test/native/test_config`: open at 80 × 25 (ANSI colour, CP437,
   plain), press one arrow key, print the drained output size. Record bytes and the time
   at 300/2400/9600 in the decisions log.
2. **Then decide** (write the decision in the log):
   - If a key costs more than about 1 s at 2400: keep the previous `CellGrid` and send
     only changed rows (cursor-position + row redraw; per-cell runs only if rows are
     still big). Full repaint stays for open, resize, `Ctrl-L`/refresh, style or charset
     change, and after anything else wrote to the port (abort paths, live Status view,
     size probes). Plain (no ANSI) mode can't position the cursor: keep full frames there.
   - Otherwise state a minimum useful speed in the README and close.
3. **Tests:** after one arrow key, output is under a stated byte bound; a resize, a
   refresh and a style change still send a full frame; the screen content after a diff
   matches a full repaint (render both into a fake terminal grid and compare, if a
   parser exists in the tests; otherwise compare the model).

### 5.5-14 (confirm each) Menu behaviour

`lib/config/src/menu.cpp:259-261`: paging step does not match tall screens; `:1194-1198`:
Alt+letter cancels a form then acts as a hotkey; `:1294-1306`: the "input too long" flag
survives Backspace. Confirm each with a `test_config` test; fix the real ones.

### MNT-05j (part) cppcheck note `menu.cpp:449`

Duplicate condition: tidy it.

### STA-09d

Done in WP-07 (v2.7); nothing to do here.

## Steps

1. Branch, bump (`minor "Configuration screen sends only what changed"`).
2. The measurement test, then the decision.
3. The diff renderer with tests (or the README note).
4. 5.5-14, MNT-05j.

**Cut line:** 5.5-14 can move to WP-16 (same file).

## End checks

`pio test -e native -f test_config`, full suite, `pio run -e s3`, `pio check`,
clang-format. Optional bench check for Jon: PuTTY at 2400 baud through RS232, move
around the menu.

## Done when

A key press at 80 × 25 sends a small, measured number of bytes (or the README states a
minimum speed), with tests guarding full repaints where they're needed.
