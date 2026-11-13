# WP-18 (v3.0, session 1): Framework spike

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high |
| Size | L |
| Branch | `firmware/v3.0-idf5` (the whole v3.0 work uses this one branch and one version bump) |
| Depends on | WP-01 to WP-17 merged before v3.0 merges. The spike itself changes nothing on `main` and can run earlier, but then rebase before WP-19 |
| Firmware change | experimental, on the branch only |

Follow the token rules in [README.md](README.md). This session is research plus build
experiments; its output is a plan, not a release.

## Finding

### SEC-03 The framework is end-of-life

`platformio.ini:23-28` pins `espressif32@6.13.0`, Arduino core 2.0.17 and
`framework-espidf@3.40407.240606` (ESP-IDF 4.4.7). ESP-IDF 4.4 support ended July 2024
(advisory AR2024-008). **Decision:** target Arduino core 3.3.x on ESP-IDF 5.5.x (3.3.12 on
5.5.5 was the current pioarduino stable release at review time), as **v3.0** (Jon,
12 Nov 2026), pinned exactly (a release tag, verified like `scripts/network_dependencies.py`
does), after a spike.

Open question that decides the plan: the project builds Arduino as an ESP-IDF component
(`framework = arduino, espidf`) for its own sdkconfig (NAPT for internet dial-up, TinyUSB
for the USB modem port, core dumps to flash). It is not known whether pioarduino or the
official platform supports that on the S3. If neither does, the route is Espressif's
"Arduino as an ESP-IDF component" with a plain ESP-IDF CMake build, which replaces
PlatformIO for the firmware build (CI, VS Code tasks, upload scripts all change).

## Spike steps

1. Branch from `origin/main`; bump `major "Arduino core 3.3 on ESP-IDF 5.5"` (3.0).
2. Check the current releases (web search, standard mode first): pioarduino
   platform-espressif32 latest stable tag and its Arduino/IDF versions; whether the
   official PlatformIO platform now supports Arduino 3 with `arduino, espidf`; Espressif
   advisories since July 2024 that apply to the components in use (Wi-Fi, lwIP, mbedTLS,
   HTTP server, OTA): list them.
3. Try route A: pioarduino with `framework = arduino, espidf`, the current
   `sdkconfig.defaults`, unchanged source. Capture the first errors (`pio run -e s3 2>&1
   | grep -m 60 -E "error|Error"`). Then route B (official platform) if A fails at the
   build-system level. Route C (plain ESP-IDF CMake) only on paper unless A and B fail.
4. For the chosen route, list every compile error by area, compared with the review's
   expected list:
   - mbedTLS 3: `mbedtls_md5_ret`, `mbedtls_sha256_*_ret` in
     `lib/hw/src/web_service.cpp:187`, `:776`, `:790`, `:798`,
     `lib/hw/src/device_crypto.cpp:57` (five mechanical edits).
   - RMT: `lib/hw/src/hw.cpp:446-461` (autobaud capture, legacy RMT) and
     `lib/hw/src/wifi_radio.cpp:290` (`neopixelWrite`, core RMT). ESP-IDF 5 refuses old
     and new RMT drivers in one program: the capture needs the new RMT receive API.
     `lib/hostport/src/autobaud.cpp` and its tests are unaffected only if the run format
     stays the same. **Most likely real obstacle.**
   - I2S legacy driver `lib/hw/src/hw.cpp:67-93`, `:195` (deprecated; works with
     warnings).
   - UART registers `lib/hw/src/hw.cpp:312-319` (`id.reg_update`, `id.high_speed`),
     `:421` (pad mux).
   - USB: `lib/hw/src/usb_port.cpp` uses `tud_cdc_n_*` (stable), but
     `scripts/network_dependencies.py`, `scripts/arduino_tinyusb/CMakeLists.txt` and the
     `CONFIG_TINYUSB_*` keys are replaced by the managed `esp_tinyusb` component. Pinned
     `esp_littlefs`/`littlefs` downloads probably replaced by the core's LittleFS.
   - Smaller: `esp_core_dump_get_summary` (`src/main.cpp:690`), `WiFi.onEvent`
     (`wifi_radio.cpp:101`), Arduino Wi-Fi internals, `httpd_*` and the 1 KiB header
     buffer comment (`web_service.cpp:64`).
5. Fix the mechanical ones on the branch if quick (mbedTLS), to see how far the build
   gets. Record flash and RAM if it links.
6. **Write `docs/plans/wp-19-framework-migration.md` in detail**: the chosen route, one
   section per session (each fitting a 5-hour Pro window), model/effort per session, the
   bench test list, and what changes in CI, tasks, scripts and `credits/LICENSES.md`.
   Decisions log entry for the route. Commit the spike state with a clear "WIP, does not
   build" message if it doesn't build; don't open a PR yet (or open it as a draft).

**Cut line:** steps 1-4 and 6 are the package; step 5 is a bonus.

## Done when

The build route is chosen with evidence, the error list is complete, and WP-19 has a
session-by-session plan Jon can follow.

## Result (13 Nov 2026)

**Route chosen: B, the official PlatformIO platform `espressif32@8.0.0`, with Arduino
3.3.12 as a component on ESP-IDF 6.1** (decisions log, 13 Nov 2026). Jon asked mid-spike
for the newest stable version of every component, which moved the target from IDF 5.5
(in maintenance since 21 Jul 2026) to IDF 6.1.

Evidence:

- **Step 2, releases.** pioarduino `55.03.312-1` = Arduino 3.3.12 on IDF 5.5.5. The
  official platform supports Arduino 3 since **8.0.0** (19 Oct 2026; 7.x still shipped
  2.0.17), including `framework = arduino, espidf` (its `platform.py` pins IDF 5.5.5 for
  mixed projects; we override to 6.1). Arduino 3.3.12's component allows IDF 5.3-6.1.99
  and its CI builds against release-v6.1. Arduino 4.0.0 is RC1 only. IDF 6.1 (27 Aug
  2026) is in service to 25 Aug 2027; 5.5 is in maintenance.
- **Step 2, advisories** since July 2024 that touch this firmware: CVE-2026-45541
  "Remote Null Pointer Dereference in WebSocket Server" (high; fixed in 6.0.1 and
  5.5.5, so in 6.1; the end-of-life 4.4.7 is not listed and presumably still exposed);
  AR2026-003 "OTA Bug Advisory for WPA3-SAE H2E Configuration Issues in ESP-IDF" (bug,
  fixed); AR2024-010 S3 reset failure after OTA with 120 MHz flash (bug, not our 80 MHz
  setting); AR2024-005 S3 RTC memory corruption (bug, fixed); AR2026-005 S3 AES key
  recovery by voltage fault injection (hardware, physical access, no flash encryption
  here). IDF 5.5.6 and 6.1 also carry upstream wpa_supplicant fixes. Nothing found for
  lwIP, Mbed TLS hashes, core dump or TinyUSB. The rest (Bluetooth, BLE provisioning,
  ESP-NOW, WPS, TEE, other chips) don't apply.
- **Step 3, route A (pioarduino)** failed at the tooling level twice: its first run
  replaced PlatformIO Core 6.2.0 in the shared `~/.platformio/penv` with its own fork
  (`pioarduino-6.2.0.post1`), then refused to run under it ("not compatible with
  PlatformIO Core v6.2.0-post.1"). Core 6.2.0 was reinstalled (`pip check` clean). In an
  isolated `PLATFORMIO_CORE_DIR` it failed on a package-mirror DNS error, a bad
  `tool-esptoolpy` install and the IDF Python step. It is also tied to IDF 5.5. Rejected.
- **Step 3, route B** with IDF 6.1: installs, sets up the IDF 6.1 Python environment,
  resolves the managed components, configures CMake and compiles all of IDF and
  Arduino. Route C was not needed.
- **Step 4, error list:** complete, by area, in
  [wp-19-framework-migration.md](wp-19-framework-migration.md) ("What the spike found").
  Against the review's expected list: Mbed TLS as expected but bigger (Mbed TLS 4: PSA,
  not renames); RMT as expected (legacy driver now removed, not just in conflict); I2S
  **removed**, not just deprecated; UART registers **still compile**; TinyUSB as
  expected (new component and TinyUSB); core dump, `WiFi.onEvent` and `httpd` compile
  unchanged. New: IDF 6 warnings-as-errors, one removed Wi-Fi reason code, a GCC 15
  `std::min` type mismatch, `PIN_INPUT_ENABLE`, `neopixelWrite` deprecated, Picolibc.
- **Step 5 (bonus):** done for the mechanical ones (PSA hashes and random, Wi-Fi reason,
  `std::min`, LittleFS CMake line, warnings switch for discovery), marked `v3.0 SPIKE`
  in the code. The firmware does not link yet (TinyUSB, `hw.cpp`), so no flash/RAM
  figures; session 3 of WP-19 records them.
- **Step 6:** WP-19 rewritten as a five-session plan.
