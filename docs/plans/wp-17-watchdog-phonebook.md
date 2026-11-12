# WP-17 (v2.17): Watchdog and phonebook capacity

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high |
| Size | M |
| Branch | `firmware/v2.17-watchdog-phonebook` |
| Depends on | WP-02, WP-03 (known long waits removed), WP-14 (`main.cpp` settled) |
| Firmware change | yes (`sdkconfig.defaults`, `src/main.cpp`, on-board tests) |

Follow the session workflow and token rules in [README.md](README.md). This is the one
package where a bench run really matters before release: a missed long operation means
false resets. Write the code and tests here; ask Jon whether he wants the on-board runs
and a soak in this session (bench: OTA to the modem, RS232 on COM18; see CLAUDE.md).

## Findings

### STA-06 Nothing restarts the modem if the main loop stops

`sdkconfig.defaults` has no watchdog options; generated `sdkconfig.s3:691-695`: the task
watchdog watches only the idle tasks and does not panic. No `enableLoopWDT` or
`esp_task_wdt_*` call in `src/` or `lib/`. A blocked loop is never detected, so the
core-dump and `AT*RESETINFO` machinery never sees a hang.

**Fix:**
1. `sdkconfig.defaults`: `CONFIG_ESP_TASK_WDT_PANIC=y`, a time-out (suggest 10-15 s;
   longer than any remaining legitimate wait), keep idle-task checks as they are unless
   they cause trouble. Regenerate/check `sdkconfig.s3` the way the project does (look at
   how earlier versions changed sdkconfig; `docs/history/v0.13-plan.md` has notes).
2. Subscribe the loop task (`esp_task_wdt_add(nullptr)` in `setup()`), feed it once per
   `loop()` pass.
3. Feed (or temporarily unsubscribe) in the long operations: OTA (`ArduinoOTA` handle
   while a transfer runs), web image staging and LittleFS update, factory reset,
   `AT*HWTEST` steps, Wi-Fi join waits, anything else found by Grep for `delay(` with
   large values and `while (` loops in `src/` and `lib/hw`. List each in a comment by
   the watchdog set-up.
4. Make sure a watchdog reset shows in `AT*RESETINFO` with a clear reason (check the
   reset-reason text table).
5. Decisions log: time-out value and the list of fed operations.

### STA-08 A full phonebook may not fit in NVS (needs confirmation)

`lib/calls/src/phonebook.cpp:124-144`: one blob of 9 + 64 × 83 = 5,321 bytes;
`partitions_16mb.csv` NVS is 0x5000 (20 KB, about 16 KB usable); NVS writes the new blob
before freeing the old one, so a save needs about 10.6 KB with all other settings
present. **Add an on-board test** (in `test/embedded/test_calls` or a new suite, using a
scratch namespace but with realistic other settings written too): write 64 entries, edit
one, delete one, all succeed. Only if it fails: move the phonebook to a LittleFS file
through `hw::FileStore`, as the call history does (bigger change; then plan a follow-up
rather than squeezing it in). A larger NVS partition would need a full USB flash: avoid.

### 5.5-08 lwIP stack use

`lib/hw/src/internet_network.cpp:77`, `:122`: two 1.5 KB packet copies on the lwIP task's
6 KB stack. Add a high-water-mark print to the on-board Internet test (or `AT*STATUS`
diagnostics if that exists) and record the value; act only if headroom is under about
1 KB.

## Steps

1. Branch, bump (`minor "Loop watchdog; phonebook capacity test"`).
2. STA-08 on-board test (compile: `pio test -e s3 -f test_calls --without-uploading
   --without-testing`).
3. STA-06 steps 1-5. Build.
4. 5.5-08 measurement code.
5. If Jon agrees: run the on-board tests and a soak (calls, web page, PPP, an OTA
   upload) and record results in the decisions log.

**Cut line:** 5.5-08 can move to WP-19 (the v3.0 bench run measures RAM anyway).

## End checks

Full `pio test -e native`, `pio run -e s3`, `pio check`, clang-format, on-board compile.

## Done when

A stuck loop resets the modem with a recorded reason, no known long operation trips it,
and the phonebook capacity is measured.
