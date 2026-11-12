# WP-05 (v2.5): Wi-Fi join, factory reset, scan names

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / medium |
| Size | S-M |
| Branch | `firmware/v2.5-wifi-reset-scan` |
| Depends on | WP-01 |
| Firmware change | yes (`lib/netcfg`, `lib/hw/src/file_store.h`, `lib/hw/src/wifi_radio.cpp`) |

Follow the session workflow and token rules in [README.md](README.md).

## Findings

### STA-04 After a failed Wi-Fi join from the browser, later browser changes are undone

- `lib/netcfg/src/net_commands.cpp:277-288`: `detachCommands` forgets the pending join, so
  `clearJoin()` at `:267`/`:272` never runs. `lib/netcfg/src/netcfg.cpp:132-145`: the
  60-second guard treats `join_ == kFailed` as "this change failed".
  `src/main.cpp:1081-1085` calls detach when the browser's socket goes.
- A browser join always drops the browser's own connection, so its result is never
  collected. After a failed join, every later browser change (host name, static IP) is
  answered `OK` and then rolled back on the next tick.
- **Fix:** clear the join result when its listener is detached and the join is no longer
  running (or give the 60 s guard its own flag). **Test** in `test_netcfg`: join fails,
  detach, browser change, tick → the change stays.

### STA-05 `AT*FACTORY` cannot finish without a mounted filesystem

- `lib/hw/src/file_store.h:58-62`: `erase()` returns false when LittleFS is not mounted.
  `lib/devicecfg/src/devicecfg.cpp:189-210`: the reset stops at the first store that
  fails and leaves the "reset pending" marker. `src/main.cpp:152-153` (history is the
  fourth of eight stores), `:1183`, `:1189-1194` (retried at every boot).
- Effect: every boot erases the same settings again; the device password is unavailable,
  so web and OTA stay off; only a USB `uploadfs` gets out.
- **Fix:** in `FileStore::erase()`, "not mounted" means "nothing to erase" (return true).
  **Test** in `test_devicecfg` with a store that reports no filesystem: the reset
  completes and clears the marker. `file_store.h` is in `lib/hw`; if it can't be compiled
  natively, test through the devicecfg fake that mimics the new contract and keep the
  `lib/hw` change to the one line.

### SEC-04 Wi-Fi scan names printed unfiltered

`lib/hw/src/wifi_radio.cpp:249` copies the SSID as received;
`lib/netcfg/src/net_commands.cpp:172-177` (`showScan`) prints it with `%-32s`. A
neighbour's SSID with escape sequences can clear the screen or fake a prompt on
`AT*WIFISCAN`. **Fix:** replace bytes below 0x20 and 0x7F with `?`. The review says in
`scanResult()`; prefer doing it in `lib/netcfg` where `showScan` prints (testable), and
keep the configuration screen and web JSON as they are (they already filter). **Test**
in `test_netcfg` with an escape in an SSID. Leave non-ASCII bytes alone here (that is
5.5-16, WP-12).

### 5.5-04 (confirm first) `AT*WIFI=OFF` or `AT*WIFIFORGET` during the 60 s window

`lib/netcfg/src/netcfg.cpp:314-358`: reportedly saves the unverified browser change.
Confirm with a native test (browser change, then `AT*WIFI=OFF` inside 60 s, tick past
60 s). If real, cancel the pending verification and restore the previous settings (or
refuse the command during the window; choose the smaller change and record it).

## Steps

1. Branch, bump (`minor "Wi-Fi join, factory reset and scan list fixes"`).
2. SEC-04, STA-05, STA-04 (each test first), then 5.5-04.
3. Decisions log only if 5.5-04 changes behaviour. `test/README.md` matrix.

**Cut line:** 5.5-04 can move to WP-12.

## End checks

`pio test -e native -f test_netcfg -f test_devicecfg`, then full, `pio run -e s3`,
`pio check`, clang-format.

## Done when

The three fixes have tests that fail without them; finding map rows STA-04, STA-05,
SEC-04, 5.5-04 and the TG-08 parts updated.
