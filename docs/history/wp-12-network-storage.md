# WP-12 (v2.12): Network and storage follow-ups

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | M-L |
| Branch | `firmware/v2.12-network-storage` |
| Depends on | WP-05 (same files in `lib/netcfg` and `wifi_radio.cpp`) |
| Firmware change | yes, for the rows that turn out to be real |

Follow the session workflow and token rules in [README.md](README.md).

These are reader findings the review lead did **not** verify. For each: read the cited
code, decide real or not, and write the outcome in the finding map ("not a bug: <one
line why>" or the fix). Where the logic is in `lib/netcfg` or `lib/webinterface`,
confirm with a native test first. `lib/hw` rows can't be unit-tested on the PC: confirm
by reading, keep fixes small, and list them for Jon's bench check.

## Rows, in suggested order

| ID | Where | Reported problem | Likely fix if real |
| --- | --- | --- | --- |
| 5.5-15 | `lib/netcfg/src/net_settings.cpp` | `validPassword` rejects UTF-8 passphrases (WPA2 allows any 8-63 bytes); `validServer` accepts `a..b` | Accept bytes 0x20-0x7E and >= 0x80 for passwords (no control bytes); reject empty labels in host names. `docs/COMMANDS.md` if the rule text changes |
| 5.5-16 | `lib/webinterface/src/web_policy.cpp:268-272` | Non-ASCII SSIDs reach the page as mojibake (safe, but wrong) | Pass valid UTF-8 through as UTF-8 in the JSON escape; escape invalid bytes. Test in `test_web` |
| 5.5-05 | `lib/netcfg/src/netcfg.cpp:432-453`, `:535-557` | Wi-Fi join, scan or IP change from another port drops a call in progress without warning | Refuse during a call with a clear message, or warn and ask; choose with the existing confirmation style. Decisions log |
| 5.5-07 | `lib/hw/src/net_probe.cpp:94-98` | DNS time counts against the 10 s ping limit; a late time-out reports failure although replies arrived | Start the ping limit after DNS; report replies received |
| 5.5-01 | `lib/hw/src/wifi_radio.cpp:204-207`, `lib/netcfg/src/netcfg.cpp:179-185` | After `AT*WIFISCAN` with Wi-Fi off, the radio stays on | Turn the radio off when the scan ends if Wi-Fi is off |
| 5.5-03 | `lib/hw/src/wifi_radio.cpp:208-224` | A second scan while one runs can end the new scan early with a short list | Refuse or join the running scan |
| 5.5-02 | `lib/hw/src/wifi_radio.cpp:24-25`, `:58-81` | Link state shared between the Wi-Fi event task and the loop with `volatile` only; a late event can overwrite `leave()` | `std::atomic` plus a generation counter, or a small critical section; ignore events from an older generation |
| 5.5-19 | `lib/devicecfg`, `lib/hw/src/wifi_radio.cpp:100` | `AT*FACTORY` does not remove Wi-Fi credentials the ESP-IDF driver may have stored under a pre-v0.6 firmware | Call `esp_wifi_restore()` (or erase the driver's NVS keys) during factory reset; check the call exists in IDF 4.4 |
| 5.5-06 | `lib/hw/src/file_store.h:21-57` | Power loss between the two renames loses the newest history save; a primary file with a bad inner CRC does not fall back to the backup | On load, try the primary, then the backup, then a leftover temp file, each with its CRC. If `FileStore` logic can be split from the LittleFS calls, unit-test it |

## Steps

1. Branch, bump (`minor "Wi-Fi, ping and history file follow-ups from the review"`).
2. Rows in the order above (testable ones first). Commit after each real fix so a
   follow-up can pick up cleanly.
3. Decisions log for behaviour changes (5.5-05, 5.5-15); `docs/COMMANDS.md`;
   `test/README.md`. Add a short "bench checks for Jon" list to the PR description for the
   `lib/hw` rows.

**Cut line:** stop after any row; the rest stay "open" in the finding map for a
follow-up session.

## End checks

`pio test -e native -f test_netcfg -f test_web -f test_devicecfg`, full suite,
`pio run -e s3`, `pio check`, clang-format.

## Done when

Every row is fixed or marked "not a bug" with a reason.
