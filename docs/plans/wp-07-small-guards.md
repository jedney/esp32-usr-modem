# WP-07 (v2.7): Small settings and parser guards

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / medium |
| Size | M-L |
| Branch | `firmware/v2.7-small-guards` |
| Depends on | WP-01 |
| Firmware change | yes (`lib/atcmd`, `lib/modemcfg`, `lib/calls`, `lib/config`, `lib/sounds`, `lib/hw`) |

Follow the session workflow and token rules in [README.md](README.md). Each item is
small and independent: test first, fix, run its suite, next.

## Findings

| ID | Where | What is wrong | Fix | Suite |
| --- | --- | --- | --- | --- |
| STA-09a | `lib/atcmd/src/atcmd.cpp:799-805`, `:419` | S3, S4, S5 accept 0-255. `ATS3=49` + `AT&W` makes every `1` end the line on all ports after restart | Limit S3/S4/S5 to 0-127 and refuse a printable S3 (0x20-0x7E) with `ERROR`. Check what a stored out-of-range value does on load (fall back to the default) | `test_atcmd`, `test_modemcfg` |
| STA-09e | `lib/modemcfg/src/modemcfg.cpp:402-406` | A failed NVS read is passed to `deserialize()` as a huge length; the modem starts on factory values and the next `&W` overwrites the stored settings | Treat a load error as a bad size (same path as a damaged blob) and keep the stored blob untouched | `test_modemcfg` |
| STA-09f | `lib/calls/src/phonebook.h:26`, `put()` | Comment says a damaged phonebook is not saved over, but the next `AT*PHONEBOOK=` edit replaces it with the factory entry plus the new one | Refuse edits while damaged (`ERROR`, with a hint to `AT*PHONEBOOK` reset if that exists); smaller and safer than loading partial records | `test_calls` |
| STA-09g | `lib/hw/src/client_platform.h:66`, `lib/sounds/src/catalog.h:21` | `sizes_[8]` fits today's 7 sounds + 1 exactly; an eighth sound writes past it | Size it from `kAssetCount + 1` (or add a `static_assert`) | build |
| STA-09d | `lib/config/src/menu.cpp:1244-1248`, `lib/atcmd/src/atcmd.cpp:510-519` | Esc during a scan, ping, DNS or play started from the screen calls `cancelPrompt()`, which only cancels masked prompts; the command carries on uncaptured. On-screen effect needs confirmation | Feed the key to the port so its wait is cancelled, as a typed key on the command line does. Confirm with a `test_config` test first | `test_config`, `test_atcmd` |
| SEC-07a | `lib/config/src/commands.cpp:63-78` | `AT*SETTING=` checks only the first character: `AT*SETTING=E0DT555` dials; `E0A`/`E1H` answer or hang up. Local only | Accept exactly one setting command (letter, optional `&`/`S`n=, digits) and reject trailing letters | `test_config_services` or `test_config` |
| MNT-05g | `lib/modemcfg/src/modemcfg.cpp:19`, `lib/config/src/settings_help.cpp:31` | Tables sized by hand against `kSettingCount`; a new setting without its row is a null-pointer crash | `static_assert` on the row count | build |
| MNT-05i | `lib/hostport/src/connection.h:1-3`, `lib/atcmd/src/atcmd.cpp:764` | Comments describe v0.5-v0.8 behaviour | Update the comments | - |

### STA-02b Idle time-out for the configuration screen

**Decision (Jon, 12 Nov 2026): a configuration screen with no key for 5 minutes closes
itself.** This is the open point from STA-02: a screen opened on purpose (`AT*CONFIG` on a
serial port, or `E` in the browser) holds the call slot, so a forgotten one turns
callers away and makes `ATD` give `BUSY`.

- Where: `lib/config/src/menu.cpp:1061-1107` (`poll()` already has time-outs for a
  confirmation, 30 s, and a masked field, 120 s). Add one more there: 5 minutes (a named
  constant) since the last key, on every page, including the live Status view.
- On time-out, close the screen the same way as leaving it normally: unsaved form edits
  are discarded, the call slot is released. A serial port gets a short line ("Screen
  closed after 5 minutes idle.") and returns to command mode with `OK`; the browser
  returns to the read-only view (WP-06; if WP-06 hasn't landed, just closing the screen
  is enough). Check that the existing exit path does this; reuse it rather than
  writing a second one.
- Keys reset the timer; output from the modem (Status refresh, scan results) doesn't.
  A hardware test or command running from the screen has its own limits (WP-03) and
  shouldn't be cut off mid-run: start the 5 minutes again when it finishes.
- Fixed value: no `AT*` command, so no parity work.
- Tests in `test_config`: no key for 5 min closes and releases the slot; a key at
  4:59 resets it; the 30 s and 120 s time-outs still work; the clock wrap case.
- Docs: decisions log (implemented), `docs/COMMANDS.md` under `AT*CONFIG`, README where
  it says calls are turned away while the screen is open, `web/PROTOCOL.md` (the browser
  returns to read-only).

## Steps

1. Branch, bump (`minor "Range checks, settings guards, configuration screen time-out"`).
2. STA-02b (decided behaviour; do it first).
3. The table top to bottom.
4. `docs/COMMANDS.md` for S3/S4/S5 ranges, `AT*SETTING=` and the phonebook refusal;
   `AT*HELP` if it lists ranges; decisions log for the S3 rule; `test/README.md`.

**Cut line:** STA-09d is the least certain (needs confirming); move it to WP-15 (same
file) if short of time, then MNT-05g/MNT-05i to WP-16.

## End checks

Suites as listed while working; full `pio test -e native`, `pio run -e s3`, `pio check`,
clang-format.

## Done when

Every row has a test (or a `static_assert`) that fails without the fix; finding map
updated.
