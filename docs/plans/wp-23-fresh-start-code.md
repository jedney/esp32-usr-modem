# WP-23 (v3.0): Fresh start, code

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | L |
| Depends on | WP-22 (its network console helps debugging; not required) |

Read [wp-19-common.md](wp-19-common.md) first (decisions, versions, board testing, rules).
WP-24 does the same for the documentation.

## Modernisation review row

| Area | Now | Look at |
| --- | --- | --- |
| Settings | `Preferences` in `hw/nvs_store.cpp` and `src/main.cpp` | all access through `nvs_store` (fresh start: layout 1) |

## Rule

For every file outside `case/` and `electronics/`: keep what someone needs to
understand, build, test and change the **current** design with a new ESP32-S3; remove the
rest. Keep the reason for a non-obvious choice; drop who decided it and when, the version
it arrived in, what it used to do, and review-finding IDs. Inventory on 13 Nov 2026: about
74 version tags, 106 finding IDs (`SEC-`, `STA-`, `PERF-`, `MNT-`, `TG-`, `5.5-`) and 82
brief or `Q` references in `lib/`, `src/`, `test/`, `scripts/`, `web/src/`.

## Steps

1. **Stored data, one layout each, starting at 1.** Remove migration paths and their
   tests: `modemcfg` (`kMigrated`, layouts 1-4, the "converted from an older firmware's
   format" text, `kLayoutVersion = 5`), `clienttest/client_settings` (layout 1), and check
   `devicecfg`, `netcfg`, `internet`, the phonebook, call history, `webinterface`
   (`web_policy.h`'s separate namespace "to avoid migrating"), `hw::FileStore` and the
   `nvs.net80211` handling. Keep the version byte and the CRC: an unknown layout or a bad
   CRC still loads factory settings (that is protection, not migration). NVS namespace
   and key names may be tidied (fresh flash).
2. **Comments and messages:** strip version tags, finding IDs, `Q` numbers, names and
   dates; brief section references (such as "brief 6.6") stay, and WP-24 fixes them
   after the trim. Remove "before vX this did Y" notes. Same for `sdkconfig.defaults`,
   `platformio.ini`, `CMakeLists.txt`, `.github/workflows`, `web/src` and the test files.
   Remove the `v3.0 SPIKE` marks once each is resolved.
3. **Pins:** `lib/hw/src/pins.h`, `.env.example`, `test_pins` and `scripts/pin_*.py`
   describe the current board only (no "rev A/rev B", `Q76`/`Q89`, old assignments);
   test names without `rev_b`.
4. **Tests:** names describe behaviour, not the finding or version that added them;
   delete tests that only exercise removed migration code. The remaining suites with
   their own copies of the shared fakes (listed in `test/README.md`) move to
   `test/native/support/fakes.h` if it shortens them.
5. **Dead code and files:** cppcheck `unusedFunction` and a grep for unused scripts
   (`scripts/allow_call_tests.ps1`, `allow_ota.ps1`, `test_refusal.ps1`,
   `make_preview.py`, `build_handshakes.py`, `generate_credits.py`: check what uses each).
6. **Obsolete features: remove them** (decision 8). Obsolete means a feature that is
   superseded by another one, kept only for compatibility with an older version (old
   command spellings, aliases, settings formats, protocol variants), or a development or
   diagnostic aid that served an earlier version's work and that no current test or
   bench step uses. A feature in the firmware spec (brief section 6) or in a current e2e
   or bench test is not obsolete. When it isn't clear, ask Jon rather than guess. Remove
   each one completely: code, `AT*` command and `AT*HELP` line, configuration screen and
   web page entries, settings fields, tests, `docs/COMMANDS.md`, README and
   `web/PROTOCOL.md`. **One commit per feature**, so a regression can be traced to it;
   one decisions-log row listing them all, and a CHANGES.md "Removed" line each.
7. **Jon's `.env`** (never print it): compare key names with `.env.example` and remove
   keys that are no longer read. If its pin values equal `.env.example`'s, remove those
   lines and `USR_MODEM_PINS_OVERRIDDEN`. Compare values in a script that prints only
   key names and "same/different".
8. Full native tests, lint, clang-format, firmware build.

Cut line: 1, 2 and 7; 3-6 can move to WP-24.
