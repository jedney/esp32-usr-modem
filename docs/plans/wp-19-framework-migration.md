# WP-19 (v3.0, sessions 2 onward): Framework migration

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high (mechanical sessions can use Sonnet 5.5 / high) |
| Size | 3-5 sessions of L, plus bench time |
| Branch | `firmware/v3.0-idf5` (same branch as WP-18) |
| Depends on | WP-18; WP-01 to WP-17 merged and the branch rebased |
| Firmware change | yes, platform-wide |

**This is an outline. WP-18 replaces it with the detailed session plan** once the build
route is known. Don't start from this file alone.

## Expected sessions (to be confirmed by WP-18)

1. **Build route and mechanical fixes** (Sonnet / high): platform pin, `platformio.ini`
   (or CMake) changes, mbedTLS 3 renames, core-dump and Wi-Fi API changes, `httpd`
   changes; native tests unchanged and passing; firmware builds except RMT/USB.
2. **RMT autobaud rewrite** (Opus / high): move `lib/hw/src/hw.cpp:446-461` capture to the
   new RMT receive API, keep the run format that `lib/hostport/src/autobaud.cpp` expects;
   RGB LED through the core's new RMT. On-board `test_rs232`, `test_hw`.
3. **TinyUSB and LittleFS dependencies** (Opus / high): managed `esp_tinyusb`, remove the
   project's `arduino_tinyusb` mechanism and its pinned hashes from
   `scripts/network_dependencies.py` if no longer needed, keep the USB port's own CDC
   registration and flow control (decisions log 2026-11-08); core LittleFS. On-board
   `test_usb`.
4. **I2S, UART registers, CI and tooling** (Sonnet / high): standard-mode I2S (keep
   `volume::outputGain()` on every sample), UART register access via driver calls, CI
   and release workflows, VS Code tasks, upload scripts, `credits/LICENSES.md`, CLAUDE.md
   build notes, README.
5. **Bench run** (Sonnet / medium, with Jon): full on-board suites, end-to-end suites,
   PPP and transfer interop, OTA from v2.x to v3.0 and a USB flash, LittleFS round trip,
   Wi-Fi join (WPA/open), browser 60 s rollback, heap low-water marks with a call, the
   web page and PPP together. Release notes say a USB flash is the clean route (OTA keeps
   the old bootloader).

## Rules for this version

- One version bump for the whole branch (v3.0, done in WP-18).
- Decisions log entry per decision; `credits/LICENSES.md` for changed third-party code.
- No functional changes mixed in, so a regression can be traced to the framework move.
