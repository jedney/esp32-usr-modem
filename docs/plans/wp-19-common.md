# WP-19 (v3.0): Framework migration and fresh start: common information

Shared by the session plans WP-20 to WP-26. Read this file and the one session plan you
are doing, and only the files that plan names. Follow the token rules in
[README.md](README.md).

| Item | Value |
| --- | --- |
| Branch | `firmware/v3.0-idf5` (same branch as WP-18; the name predates the IDF 6.1 decision and stays) |
| Depends on | WP-18 (done 13 Nov 2026). WP-01 to WP-17 are merged; rebase on `origin/main` at the start of each session |
| Firmware change | yes, platform-wide |
| Push | the branch stays **local**. One push and one pull request after WP-26, when Jon says so ("Rules" below) |
| Device | any session may flash and test the connected board as needed, without asking; no backups, no migration ("Testing on the board" below) |

## Sessions

Sessions 1 and 2 of WP-19 are done (results below). The rest are one plan each:

| WP | Plan | Model / effort | Size |
| --- | --- | --- | --- |
| 20 | [I2S, RMT capture and first link](../history/wp-20-i2s-rmt-first-link.md) (done) | Opus 5.5 / high | L |
| 21 | [Networking, tooling, CI and the OTA hash](wp-21-networking-ci-ota.md) | Opus 5.5 / high | L |
| 22 | [Network console](wp-22-network-console.md) | Opus 5.5 / high | L |
| 23 | [Fresh start: code](wp-23-fresh-start-code.md) | Sonnet 5.5 / high | L |
| 24 | [Fresh start: documentation and credits](wp-24-fresh-start-docs.md) | Sonnet 5.5 / high | L |
| 25 | [DCD and RI on the USB modem port](wp-25-usb-dcd-ri.md) | Sonnet 5.5 / high | M |
| 26 | [Bench run with Jon](wp-26-bench-run.md) | Sonnet 5.5 / medium | M |

Do them in order: WP-22 needs the Wi-Fi events and OTA from WP-21, and WP-23 to WP-26 can
use WP-22's network console. The spike's evidence is in
[../history/wp-18-framework-spike.md](../history/wp-18-framework-spike.md) (section
"Result").

## Decisions from Jon (13 Nov 2026, decisions log)

1. **Newest stable versions** of every component, unless unstable or experimental, or
   not supported by the hardware (ESP32-S3 N16R8, MAX3232, MAX98357A, TinyUSB device on
   the S3's OTG port). Versions below.
2. **v3.0 is a fresh start.** Every version so far ran only on Jon's test unit; there is
   no production data and nobody else uses it. So:
   - **No data migration and no upgrade path.** Stored layouts start again at 1; anything
     else loads as factory settings. No OTA from v2.x: the bench flashes v3.0 over USB
     after a full flash erase. No rollback to v2.x.
   - **Keep only what is material** to someone picking the project up afresh with a new
     ESP32-S3: remove superseded decisions, licences and credits for things no longer in
     the build or the repo, migration code, old pin assignments, version-by-version
     behaviour notes, version tags and review-finding IDs in comments, `docs/history/`,
     and the change log before 3.0. This covers **every file, including Jon's own
     `.env`**, except the `case/` and `electronics/` folders.
3. **ArduinoOTA password hash: SHA-256**, no MD5 compatibility.
4. **IDF's warnings-as-errors also apply to the project's own code.**
5. **The S3 board's RGB LED is removed** (done on the branch: code, pin, tests, docs and
   the key in Jon's `.env`).
6. **Functional changes** in v3.0 are limited to 3, 5, 8, 10 and 11 and to removing
   migration code. Anything else found on the way gets a decisions-log row and a follow-up, so a
   regression can be traced to the framework move.
7. **`docs/project-brief.md` is trimmed to the current firmware spec** (WP-24). This
   copy stops being a snapshot of the shared master and becomes the repo's firmware spec,
   kept current with the code.
8. **Obsolete features are removed** in WP-23, not just listed (definition there).
9. **Rewrite to the new framework's way, don't patch.** Where the framework move touches
   code, use the approach Arduino 3.3 and IDF 6.1 now recommend (current drivers,
   components and APIs), even if that means rewriting the module, rather than the
   smallest change that compiles. No shims that keep an old API or approach alive, and
   no deprecated APIs in project code (`-Werror=deprecated-declarations`). The same
   applies to project code the build doesn't force to change: see "Modernisation
   review" below. Behaviour seen from outside (commands, results, timing, the web
   protocol) stays the same unless a decisions-log row says otherwise; the native, e2e
   and bench tests are the check.
10. **The USB modem port shows DCD and RI** (Jon, 13 Nov 2026), through CDC
    serial-state notifications, which TinyUSB 0.21.0 supports. WP-25.
11. **A network console** (Jon, 13 Nov 2026): the maintenance console over a raw TCP
    port, like ser2net, protected by the device password, so the board can be
    debugged, and with ArduinoOTA flashed, over Wi-Fi while the USB cable is on the
    USB modem port. WP-22, so that WP-23 to WP-26 can use it.

## Target versions (checked 13 Nov 2026)

| Component | v2.17 | v3.0 | Source |
| --- | --- | --- | --- |
| PlatformIO platform | `espressif32@6.13.0` | `espressif32@8.0.0` (19 Oct 2026) | platformio/platform-espressif32 releases |
| Arduino core | 2.0.17 | **3.3.12** (`framework-arduinoespressif32@4.30312.261024`) | espressif/arduino-esp32 releases; 4.0.0 is RC1 only |
| ESP-IDF | 4.4.7 (EOL July 2024) | **6.1.0** (`framework-espidf@4.60100.0`) | esp-idf releases; support periods chart |
| GCC (Xtensa, RISC-V ULP) | 8.4 | **15.2.0+20251204** | `tools/tools.json` at IDF v6.1 |
| Mbed TLS | 2.28 | 4.x with TF-PSA-Crypto (part of IDF 6.1) | IDF 6.0 migration guide, security |
| C library | Newlib | **Picolibc** (IDF 6 default) | IDF 6.0 migration guide, system |
| TinyUSB | `a0e5626` | **0.21.0** (30 Jun 2026), as the registry's `espressif/tinyusb` `==0.21.0~6` (done) | hathach/tinyusb releases |
| LittleFS | own download of esp_littlefs + littlefs | managed `joltwallet/littlefs` 1.23.1, pinned exactly (done) | arduino-esp32 `idf_component.yml` |
| esptool | from platform | platform's 5.4.0; pin `platformio/tool-esptool` to 5.6.0 if the registry has it | espressif/esptool releases |
| PlatformIO Core | 6.2.0 | 6.2.0 (newest) | platformio-core releases |
| Unity | from PlatformIO | 2.7.0 if `test_framework` lets it be pinned | ThrowTheSwitch/Unity |
| cppcheck (CI) | runner's | 2.22.0 | danmar/cppcheck |
| clang-format | WinLibs LLVM 19 | same style; LLVM 23.1.5 is newest | llvm-project |
| CI Python | 3.12 | newest that PlatformIO 6.2.0 and the IDF 6.1 tools install on (3.15.0 out 9 Oct 2026; IDF needs 3.10+): try 3.15, else 3.14 | python.org |
| CI Node | 22 | **26** (Active LTS from 28 Oct 2026) | nodejs/release |
| pytest / pytest-timeout / pyserial | 9.1.1 / >=2.3 / >=3.5 | 9.2.0 / 2.5.0 / 3.5 (exact pins) | GitHub releases |
| GitHub Actions | by SHA | newest tags by SHA (checkout 7.0.1, setup-python 7.1.0, setup-node 7.1.0, cache 6.1.0, upload-artifact 7.0.2) | GitHub releases |
| xterm.js, IBM Plex Mono | 6.0.0, Fontsource 5.3.0 | unchanged (newest; recheck Fontsource) | xtermjs releases |

At the start of each session, check for a newer **stable** release (Arduino 3.3.13 or
4.0.0, IDF 6.1.1, platform 8.0.x). A patch release of the same line is taken; a new minor
or major is a question for Jon.

**Fallback:** if IDF 6.1 hits a problem the session can't fix, drop to platform 8.0.0's
own pairing (IDF 5.5.5, `framework-espidf@4.50505.0`, GCC 14.2.0+20260121). The driver
and Mbed TLS work is still needed there (deprecated in 5.5).

## Modernisation review (decision 9)

Compiling is not the test. Each session checks the code it touches against the current
Arduino 3.3 and IDF 6.1 documentation and migration guides (read the current docs online,
not from memory), and rewrites where the recommended approach has changed. The candidates
for each session are in its plan; keep the current code where it is already the
recommended way, and say why in the session's notes. A rewrite that changes behaviour is a
decisions-log row; a large one that doesn't fit its session gets its own follow-up session
rather than a shim.

## Done: sessions 1 and 2 (13 Nov 2026)

Decisions-log rows and `CHANGES.md` are written for both. What is left for the later
sessions is in their plans; this is what they all need.

- **Session 1 (build policy, fixes, modernisation):** `-Wall -Wextra` and
  `-Werror=deprecated-declarations` apply to the project's own code, and IDF's default
  warnings are errors (`CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS` is gone).
  `sdkconfig.defaults` uses IDF 6.1 names. Arduino's managed dependencies are trimmed with
  `CONFIG_ARDUINO_SELECTIVE_COMPILATION`. LittleFS is `joltwallet/littlefs` 1.23.1 in a
  project `idf_component.yml`, with `dependencies.lock` committed. Random numbers, MD5,
  SHA-256 and HMAC-SHA-256 go through PSA (`psa_crypto_init()` once in `setup()`). GPIO uses
  `driver/gpio.h` only. SNTP uses `esp_netif_sntp`. Picolibc stays.
- **Session 2 (TinyUSB):** the USB route is the registry's `espressif/tinyusb`
  `==0.21.0~6`, a public dependency of the in-repo `components/arduino_tinyusb` (Kconfig, a
  trimmed `tusb_config.h`, CMake). Arduino's USB layer (`USB.begin`) stays; `esp_tinyusb`
  clashes with it (decisions log says why). `scripts/network_dependencies.py` and
  `scripts/arduino_tinyusb/` are gone; `scripts/sdkconfig_refresh.py` (with tests) keeps the
  `sdkconfig` regeneration. Arduino's `USBHost.cpp` and `USBMSC.cpp` compile to nothing;
  the Arduino `USB` library itself is not built.
- **Where the build stands (after WP-20):** the firmware builds with no warnings in project
  code and runs on the board, set up from `.env` (Wi-Fi joined, `.env` device password).
  App image 1,496,025 bytes (35.7 % of the 4 MB slot), static RAM 151,696 bytes. Clean
  build about 200 s. What WP-20 left for later sessions is in its plan's "Result"
  ([../history/wp-20-i2s-rmt-first-link.md](../history/wp-20-i2s-rmt-first-link.md)).

### Build tips

- **See every build error at once:** `SCONSFLAGS=-k pio run -e s3` (SCons keeps going past
  a failed file). `pio run -e s3 -t compiledb` writes `compile_commands.json` (about 15 s)
  to show the exact flags of any file; delete it afterwards (the repo root is not ignored).
- **PlatformIO does not re-run CMake** when only a component's `idf_component.yml`
  changes: touch `CMakeLists.txt` after editing one.
- If the PPP class is wanted (WP-21), turn on `CONFIG_ARDUINO_SELECTIVE_PPP`. Arduino's
  `USB.h` is part of the core, so selective compilation does not hide it.

## Testing on the board (WP-19 only)

Jon's permission for WP-19 (13 Nov 2026); it ends with WP-19 and doesn't carry over to
later work.

- **Any session may flash and test the connected board as needed**, without asking
  first: USB or OTA uploads, `pio run -e s3 -t erase`, `uploadfs`, on-board suites
  (`pio test -e s3`; re-upload the firmware after) and end-to-end tests. Use it from
  WP-20 on (the firmware doesn't link before then), whenever a change needs checking
  on real hardware. Don't save it all for WP-26.
- **Over the network from WP-22 on:** once the network console works and the modem
  is on Wi-Fi, sessions may debug through `socket://usr-modem.local:2001` (the e2e
  `--console-port` takes it) and flash by OTA (`pio run -e s3_ota -t upload`) instead
  of the console cable. The cable is still needed for the first flash after a full
  erase, for `pio test -e s3`, and for a modem that doesn't boot or join Wi-Fi.
- **No backups and no migration.** It is a development unit; settings, NVS and LittleFS
  may be wiped at any time. Erase fully rather than work around old data.
- **Setup credentials are in `.env`:** `USR_MODEM_WIFI_SSID`, `USR_MODEM_WIFI_PASS` and
  `USR_MODEM_PASSWORD`. Use them only through the project's scripts and test fixtures
  (`scripts/load_env.py`, `scripts/ota_upload.py`, the e2e `conftest.py`), or code that
  reads them into a variable and never prints them. Never print, log or echo `.env` or
  any of these values, and keep them out of commits, the decisions log and command
  lines that get shown.
- **Ports:** list them first (`pio device list`); the last known set is in Claude's
  bench memory (RS232 on COM18, USB modem port on COM21, console COM20 when plugged in).
  The first v3.0 flash after an erase is over USB (no OTA from v2.x), which needs the
  console port, or BOOT held while tapping RST on the native port. If a port the test
  needs isn't there, ask Jon to plug it in rather than skip the test.
- Close any serial monitor before uploading; only one program can open a port.
- Things a person must see or hear (sound, LEDs, cables) are for Jon: ask, or leave them
  for WP-26.

## Rules for this version

- One version bump for the whole branch (v3.0, done in WP-18).
- **Keep CI runs to a minimum.** CI runs on every push. Commit locally after each
  session; don't push and don't open a pull request between sessions. Run the CI checks
  locally instead: native tests, firmware build, lint, clang-format, `test/scripts` and
  `web` tests. The whole of WP-19 goes up as **one push and one pull request** after the
  WP-26 bench run, when Jon says so. If CI then fails, fix it in as few pushes as
  possible: reproduce the failure locally first, then push once.
- Decisions log entry per decision; `credits/LICENSES.md` for third-party code.
- **Never run the pioarduino platform against the shared PlatformIO install**: its first
  run replaced PlatformIO Core in `~/.platformio/penv` (WP-18). Use an isolated
  `PLATFORMIO_CORE_DIR` if it is ever tried again.
- Never print `.env`; scripts that touch it print key names only.
