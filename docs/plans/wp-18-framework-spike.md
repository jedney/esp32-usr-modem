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
