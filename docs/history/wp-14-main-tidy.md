# WP-14 (v2.14): `main.cpp` tidy-up

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / medium |
| Size | M-L |
| Branch | `firmware/v2.14-main-tidy` |
| Depends on | WP-02, WP-03, WP-06 (all change `src/main.cpp`) |
| Firmware change | yes, behaviour-preserving except PERF-02 and MNT-05a |

Follow the session workflow and token rules in [README.md](README.md). `src/` has no
unit tests: only the build and the bench cover it, so keep each step small, build after
each, and commit after each.

## Findings

### MNT-04 (main.cpp part) Too much in a few functions

Line numbers from `1f51a09`; earlier packages will have moved them. Grep the names.

- `ConfigHardware::command` (`src/main.cpp:481-704`, 220 lines, 14 commands in one
  `strcmp` chain): table-driven dispatch (name → member function).
- `pollBrowser` (`:1039-1171`, 130 lines) and `publishWebStatus` (`:947-1038`): split into
  named steps.
- Duplicates: browser claim block (`:1104-1112` and `:1121-1129`; WP-06 removes one);
  client-settings block (`:1226-1234` and `:1336-1348`); terminal dispatch (`:316-326` and
  `:372-386`); `Rs232Term` and `UsbTerm` (`:234-256`) differ by one call (WP-03 added
  `connected()`; keep it) → one class with the port as a parameter.
- `if (allow && owner)` at `:1065` is redundant.
- `internetDialer->poll()` is called twice per pass (`:1297`, `:1334`): call once, and
  check the second call isn't relied on for timing.
- File header (`:3-19`) still says "two serial ports": now RS232, USB and the console.
- Consider moving pure decision logic (who may claim the slot, which commands are allowed
  during a call) into `lib/` with native tests, if it falls out naturally. Don't force it.

### PERF-02 Web status rebuilt every pass with no browser

`src/main.cpp:947-1038` (`publishWebStatus()`) runs on every loop pass from `:1169` and
calls `tz::offsetAt()` (`lib/netcfg/src/net_tz.cpp:254`, several 64-bit divisions) every
pass at `:954-955`. **Fix:** return early when no browser is connected (make sure a new
browser still gets a full snapshot at once); cache the UTC offset and recompute once a
minute or when the time zone changes. Unit-test the cache if it goes in `lib/netcfg`.

### MNT-05a "Dialling on the RS232 host port." printed before the check

`src/main.cpp:1156-1161`: printed before checking the port can take the dial; otherwise
the dial is silently dropped. Check first; if the port can't take it, print that it's
busy.

## Steps

1. Branch, bump (`minor "Tidy main.cpp; skip web status with no browser"`).
2. Small items first: header comment, redundant `if`, double `poll()`, MNT-05a. Build.
3. PERF-02. Build.
4. Merge the two terminal classes; helpers for the duplicated blocks. Build.
5. Table-driven `ConfigHardware::command`. Build.
6. Split `pollBrowser` / `publishWebStatus`. Build.
7. Decisions log for PERF-02 (status only while a browser is connected).

**Cut line:** stop after any step; steps 5 and 6 can be a follow-up.

## End checks

`pio run -e s3` after each step; full `pio test -e native`, `pio check`, clang-format at
the end. Recommended (optional) bench smoke test for Jon before release: `ATI`, a call,
`AT*CONFIG`, the web page, `AT*HWLED`.

## Done when

No function in `src/main.cpp` is much over 80 lines, no duplicated block remains, the
build and native tests pass, and PERF-02 is in.
