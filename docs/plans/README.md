# Work packages: v2.0 code review fixes

The v2.0 code review (`REVIEW.md`, reviewed commit `1f51a09`, 10-11 Nov 2026; it moves to
`docs/history/v2-code-review.md` in WP-01) is split into the work packages below. Each
package fits one Claude Pro 5-hour session, is one branch and bumps the version once.
When a package merges, its plan file moves to `docs/history/` and its rows in the
finding map below are marked done.

Decisions behind this split (Jon, 12 Nov 2026):

- Every package bumps the minor version (v2.1, v2.2, ...). If packages are done out of
  order, take the next free number; the branch name follows the version.
- The framework move (SEC-03) is **v3.0** (major bump: platform change), not v2.1 as the
  review says. WP-01 records this in `docs/decisions-log.md`.

- A configuration screen opened on purpose (`AT*CONFIG`, or `E` in the browser) closes
  after **5 minutes** with no key (Jon, 12 Nov 2026; the review's open STA-02 point).
  Done in WP-07 as STA-02b.

## Packages

Model: Sonnet 5.5 by default (lighter on Pro limits). Opus 5.5 only where timing across
tasks or `src/` code without unit tests needs careful reasoning; if Opus is unavailable or
uses the window too fast, use Sonnet 5.5 at high effort. Effort = Claude Code's effort
setting. Size = share of a 5-hour Pro window: S about a third, M about two thirds, L the
whole window.

| WP | Version | Plan | Model / effort | Size | Depends on |
| --- | --- | --- | --- | --- | --- |
| 01 | v2.1 | [Process gates, docs and decisions](../history/wp-01-process-gates.md) | Sonnet / medium | M | - |
| 02 | v2.2 | [Serial output never blocks the loop](../history/wp-02-nonblocking-sinks.md) | Opus / high | M-L | 01 |
| 03 | v2.3 | [Hardware tests cannot freeze the modem](../history/wp-03-hwtest-bounds.md) | Sonnet / high | M | 02 |
| 04 | v2.4 | [Incoming-call limits](../history/wp-04-incoming-limits.md) | Sonnet / high | M | 01 |
| 05 | v2.5 | [Wi-Fi join, factory reset, scan names](../history/wp-05-wifi-reset-scan.md) | Sonnet / medium | S-M | 01 |
| 06 | v2.6 | [Web server: read-only start, hand-shakes, lock-out](../history/wp-06-web-server.md) | Opus / medium | M-L | 02 |
| 07 | v2.7 | [Small settings and parser guards; screen time-out](../history/wp-07-small-guards.md) | Sonnet / medium | M-L | 01 |
| 08 | v2.8 | [Bench harness safety](../history/wp-08-bench-harness.md) | Sonnet / medium | M | 01 |
| 09 | v2.9 | [Security regression tests](../history/wp-09-security-tests.md) | Sonnet / high | M | 07 |
| 10 | v2.10 | [Hostile-input tests: PPP, X/Y/ZMODEM](../history/wp-10-hostile-input.md) | Sonnet / high | M-L | 01 |
| 11 | v2.11 | [Web page tests and flows](../history/wp-11-web-page.md) | Sonnet / medium | M | 06 |
| 12 | v2.12 | [Network and storage follow-ups](../history/wp-12-network-storage.md) | Sonnet / high | M-L | 05 |
| 13 | v2.13 | [Shared native test fixtures](wp-13-test-fixtures.md) | Sonnet / medium | M | 09, 10 |
| 14 | v2.14 | [`main.cpp` tidy-up](wp-14-main-tidy.md) | Opus / medium | M-L | 02, 03, 06 |
| 15 | v2.15 | [Configuration screen repaint](wp-15-menu-repaint.md) | Opus / high | L | 07 |
| 16 | v2.16 | [Configuration screen code tidy-up](wp-16-menu-tidy.md) | Sonnet / high | M | 15 |
| 17 | v2.17 | [Watchdog and phonebook capacity](wp-17-watchdog-phonebook.md) | Opus / high | M | 02, 03, 14 |
| 18 | v3.0 | [Framework spike](wp-18-framework-spike.md) | Opus / high | L | 01-17 merged (the spike itself can run earlier) |
| 19 | v3.0 | [Framework migration](wp-19-framework-migration.md) | Opus / high | 3-5 × L | 18 |

Why this order: WP-01 is cheap and makes the gates real (CI fails, upload gate, pin lock)
before anything else lands. WP-02 is the only High finding. WP-03, WP-06, WP-14 and WP-17
all touch `src/main.cpp`; this order keeps conflicts small. Refactors (WP-13, WP-14,
WP-16) come after the tests that would catch a regression. The watchdog (WP-17) comes
after the known long waits are gone. WP-04, WP-05, WP-07, WP-08 and WP-10 depend only on
WP-01 and can be taken in any order.

## Session workflow (every package)

1. Start the session with the chosen model and effort (`/model`, `/effort`). Ask Claude
   to "do docs/plans/wp-NN-....md". It reads this section and that plan only.
2. Branch: `git fetch origin`, `git switch -c firmware/vX.Y-<name> origin/main`.
3. Version: `python scripts/bump_version.py minor "<summary>"`, then rewrite the
   generated `CHANGES.md` entry (Added / Changed / Fixed).
4. For each item in the plan, in order: write or change the native test first (it should
   fail), make the fix, run only that suite: `pio test -e native -f test_<suite>`.
5. Stop at the plan's **cut line** if the window is running short; move the rest to a
   follow-up note in the plan file and the finding map.
6. Docs: `docs/decisions-log.md` for every decision or behaviour change;
   `docs/COMMANDS.md`, `AT*HELP` and `README.md` when a command or option changes;
   `test/README.md` matrix for new tests; `web/PROTOCOL.md` for web changes;
   `credits/LICENSES.md` if third-party code is added.
7. End checks, once each (see the plan for which apply):
   - `pio test -e native` (full)
   - `pio run -e s3` (only if firmware code changed)
   - `pio check -e native --fail-on-defect=medium --fail-on-defect=high`
   - `clang-format --dry-run --Werror` on the changed `*.cpp`/`*.h`
   - `python scripts/check_version.py`; `python -m pytest test/scripts -q` if scripts
     changed; `node --test test/*.test.mjs` in `web/` if the page changed.
   - Not run unless the plan says so: on-board tests, end-to-end tests, uploads. Jon
     does these on the bench when he chooses.
8. Mark the package's rows done in the finding map below, move the plan file to
   `docs/history/`, commit `vX.Y: <summary>` (with the Co-Authored-By line), push and
   `gh pr create`.

## Token rules

- Don't read the review file. Each plan carries the summary it needs; open
  `docs/history/v2-code-review.md` only for a section the plan names.
- Line numbers in the plans are from commit `1f51a09` and drift as packages land. Grep
  for the function or text first, then read only that range. Avoid reading whole large
  files (`src/main.cpp`, `lib/config/src/menu.cpp`, the big `test_main.cpp` files).
- Run one suite while iterating (`-f test_<suite>`); the full suite once at the end.
- Pipe long output: `... 2>&1 | tail -30`. The firmware build takes about 2 minutes:
  run it once, at the end.
- In the Bash tool `pio`, Python and Node are not on PATH:
  - `PATH="$PATH:$USERPROFILE/.platformio/penv/Scripts" pio ...`
  - Python: `"$LOCALAPPDATA/Python/bin/python.exe"`
  - Node: `.pio/tools/node/node-v22.23.4-win-x64/node.exe`
- Never read or print `.env`.

## Finding map

Status: open, done (with version), not a bug, accepted (no change, as the review
recommends). Rows from the review's section 5.5 were not verified by the review lead:
the package confirms each first.

| Finding | Summary | WP | Status |
| --- | --- | --- | --- |
| SEC-01 | Caller who never hangs up holds the line (8-ring limit, keepalive) | 04 | done (v2.4; (c) browser reject not done, optional) |
| SEC-02 | Wrong Digest response or half header stalls the web task | 06 | done (v2.6) |
| SEC-03 | ESP-IDF 4.4 / Arduino 2.0.17 end-of-life | 18, 19 | open |
| SEC-04 | Wi-Fi scan names printed with control bytes | 05 | done (v2.5) |
| SEC-05 | Release workflow from any branch; write token in build steps | 01 | done (v2.1) |
| SEC-06 | `AT*PASS` prints a generated password (permitted; amend rule) | 01 | done (v2.1) |
| SEC-07a | `AT*SETTING=` checks only the first character | 07 | done (v2.7) |
| SEC-07b | `pin_config.py` accepts absent, flash/PSRAM and USB GPIOs | 01 | done (v2.1) |
| SEC-07c | Actions by tag, `pip install` unpinned, caches in release | 01 | done (v2.1) |
| SEC-07d | Download cached before its SHA-256 is checked | 01 | done (v2.1) |
| SEC-07e | Read buffers that can hold a password not wiped | 02 | done (v2.2) |
| STA-01 | Unread serial port freezes the loop 2 s per result (High) | 02 | done (v2.2) |
| STA-02 | Browser opens the configuration screen and blocks calls | 06 | done (v2.6) |
| STA-02b | Idle configuration screen holds the line (5-minute time-out) | 07 | done (v2.7) |
| STA-03 | Interactive hardware tests block for ever / during a call | 03 | done (v2.3) |
| STA-04 | Failed browser Wi-Fi join undoes later browser changes | 05 | done (v2.5) |
| STA-05 | `AT*FACTORY` without LittleFS never finishes | 05 | done (v2.5) |
| STA-06 | No watchdog on the main loop | 17 | open |
| STA-07 | Upload and unmount hand-shakes can leave stale state | 06 | done (v2.6) |
| STA-08 | A full phonebook may not fit in NVS | 17 | open |
| STA-09a | S3/S4/S5 accept 0-255 (printable S3) | 07 | done (v2.7) |
| STA-09b | `AT*BAUD` changes RS232 speed during an RS232 call | 03 | done (v2.3) |
| STA-09c | `restoreBrowserResources()` cancels other ports' holds | 03 | done (v2.3) |
| STA-09d | Esc during screen-started scan/ping/DNS/play doesn't cancel | 07 | done (v2.7) |
| STA-09e | Failed NVS read passed to `deserialize()` as a huge length | 07 | done (v2.7) |
| STA-09f | Damaged phonebook replaced by the next edit | 07 | done (v2.7) |
| STA-09g | `sizes_[8]` fits 7 sounds + 1 exactly | 07 | done (v2.7) |
| PERF-01 | Configuration screen repainted in full on every key | 15 | open |
| PERF-02 | Web status rebuilt every pass with no browser | 14 | open |
| PERF-03 | Browser output cap, PPP buffer wipe, page hashing | - | accepted (measure first; recorded in WP-01) |
| MNT-01 | CI version job can never fail | 01 | done (v2.1) |
| MNT-02 | `uploadfsota` not gated; cppcheck errors don't fail CI | 01 | done (v2.1) |
| MNT-03 | Text still describes a volume limit | 01 | done (v2.1) |
| MNT-04 | Long functions in `main.cpp`, `menu.cpp`, `commands.cpp` | 14, 16 | open |
| MNT-05a | "Dialling on the RS232 host port" printed before the check | 14 | open |
| MNT-05b | `demo.js` box characters replaced by `?` | 11 | done (v2.11) |
| MNT-05c | `build/` not in `.gitignore` | 01 | done (v2.1) |
| MNT-05d | CLAUDE.md says `espressif32@^6.9.0` | 01 | done (v2.1) |
| MNT-05e | `transfer.h` has no licence / changed mark | 10 | done (v2.10) |
| MNT-05f | `web/PROTOCOL.md` three mismatches | 06 | done (v2.6) |
| MNT-05g | Hand-sized tables without `static_assert` | 07 | done (v2.7) |
| MNT-05h | Web file list kept in three places | 11 | done (v2.11) |
| MNT-05i | Stale comments (`hwconsole.h`, `connection.h`, `atcmd.cpp:764`) | 03, 07 | done (`hwconsole.h` v2.3; `connection.h`, `atcmd.cpp` v2.7) |
| MNT-05j | cppcheck notes `hwconsole.cpp:895`, `menu.cpp:449` | 03, 15 | open (`hwconsole.cpp` done v2.3) |
| 5.5-01 | Radio stays on after `AT*WIFISCAN` with Wi-Fi off | 12 | done (v2.12) |
| 5.5-02 | `volatile` link state; late event overwrites `leave()` | 12 | done (v2.12; bench check) |
| 5.5-03 | Second scan ends the new scan early | 12 | done (v2.12; bench check) |
| 5.5-04 | `AT*WIFI=OFF`/`WIFIFORGET` in the 60 s window saves the change | 05 | done (v2.5) |
| 5.5-05 | Join, scan or IP change drops a call without warning | 12 | done (v2.12) |
| 5.5-06 | `file_store.h` power loss between renames; no backup fallback | 12 | done (v2.12; the bad-CRC fallback already worked) |
| 5.5-07 | DNS time counts against the 10 s ping limit | 12 | done (v2.12; bench check) |
| 5.5-08 | Two 1.5 KB copies on the 6 KB lwIP stack | 17 | open |
| 5.5-09 | IPCP Terminate-Request resets the 30 s time-out | 10 | done (v2.10) |
| 5.5-10 | Transfer inactivity reset by any byte; missing "OO" | 10 | done (v2.10) |
| 5.5-11 | Size probe looks only at the first `ESC[` | 10 | done (v2.10) |
| 5.5-12 | S7 runs during the handshake sound | 04 | done (v2.4; confirmed by a test) |
| 5.5-13 | `captureBegin()` failure ignored; `runs` unchecked | 02 | done (v2.2) |
| 5.5-14 | Menu paging, Alt+letter after cancel, "too long" flag | 15 | open |
| 5.5-15 | `validPassword` rejects UTF-8; `validServer` accepts `a..b` | 12 | done (v2.12) |
| 5.5-16 | Non-ASCII SSIDs shown as mojibake | 12 | done (v2.12) |
| 5.5-17 | Upload double press, reconnect loop, no reload | 11 | done (v2.11; the page cannot see an upload in another tab, so it backs off on 503 instead) |
| 5.5-18 | `.env` BOM/UTF-16, `bump_version` CRLF, hotkey substring | 01 | done (v2.1) |
| 5.5-19 | `AT*FACTORY` leaves driver-stored Wi-Fi credentials | 12 | done (v2.12; bench check) |
| TG-01 | Remote `+++` test has no guard times | 04 | done (v2.4) |
| TG-02 | No canary sweep for both passwords and the Digest hash | 09 | done (v2.9) |
| TG-03 | No hostile PPP or X/Y/ZMODEM input tests | 10 | done (v2.10) |
| TG-04 | Telnet storm, full queue, IAC from an answer-mode caller | 04 | done (v2.4) |
| TG-05 | Digest parser guards, nonce cases, `ImageCheck(true, ...)` | 09 | done (v2.9) |
| TG-06 | 2^32 ms wrap, throttle above 300 bps, S3/S5 edges | 09 | done (v2.9) |
| TG-07 | Pin lock test skipped when `.env` overrides pins | 01 | done (v2.1) |
| TG-08 | Tests behind STA-01, STA-04, STA-05, SEC-04, SEC-01 | 02, 04, 05 | open (STA-01 part done v2.2; SEC-01 part done v2.4; STA-04, STA-05, SEC-04 parts done v2.5) |
| TG-09 | e2e serial ports have no write time-out (runs hang) | 08 | done (v2.8) |
| TG-10 | Asserts can print the Wi-Fi or device password | 08 | done (v2.8) |
| TG-11 | `test_board` uses the production settings store | 08 | done (v2.8) |
| TG-12 | e2e state left behind on failure | 08 | done (v2.8) |
| TG-13 | Default run dials a real BBS; `NO DIALTONE` string | 08 | done (v2.8) |
| TG-14 | `app.js` untested; "conflicting identity" test has no conflict | 11 | done (v2.11) |
| TG-15 | No tests for browser limit, floods, interrupted upload, etc. | 11 | done (v2.11; cases written, run on the bench with --flood-tests, --upload-tests, --usb-unplug) |
| TG-16 | Fakes copied between suites and drifted | 13 | open |
| TG-17 | Two assertions cannot fail; RS232 on-board tests ignored | 08 | done (v2.8) |
| Leave alone | Digest MD5 and OTA MD5, unsalted hashes, no image signature, no secure boot, nonce table of 8, PPP idle/host limit/CR-NUL, 410 cppcheck style notes, vendored xterm.js | - | accepted (review section 8) |
