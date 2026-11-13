# WP-19 (v3.0, sessions 2-6): Framework migration

| Item | Value |
| --- | --- |
| Model / effort | per session, below |
| Size | 4 sessions of L, then a bench session with Jon |
| Branch | `firmware/v3.0-idf5` (same branch as WP-18; the name predates the IDF 6.1 decision and stays) |
| Depends on | WP-18 (done 13 Nov 2026). WP-01 to WP-17 are merged; rebase on `origin/main` at the start of each session |
| Firmware change | yes, platform-wide |

Follow the token rules in [README.md](README.md). Read this file and, for the session you
are doing, only the files it names. The spike's evidence is in
[wp-18-framework-spike.md](wp-18-framework-spike.md) (section "Result").

## Target versions (decided 13 Nov 2026, decisions log)

Rule (Jon, 13 Nov 2026): the newest stable version of every component, unless it is
unstable or experimental, or doesn't support the hardware (ESP32-S3 N16R8, MAX3232,
MAX98357A, TinyUSB device on the S3's OTG port).

| Component | v2.17 | v3.0 | Source checked on 13 Nov 2026 |
| --- | --- | --- | --- |
| PlatformIO platform | `espressif32@6.13.0` | `espressif32@8.0.0` (19 Oct 2026) | platformio/platform-espressif32 releases |
| Arduino core | 2.0.17 | **3.3.12** (`framework-arduinoespressif32@4.30312.261024`) | espressif/arduino-esp32 releases; 4.0.0 is RC1 only |
| ESP-IDF | 4.4.7 (EOL July 2024) | **6.1.0** (`framework-espidf@4.60100.0`) | esp-idf releases; support periods chart |
| GCC (Xtensa, RISC-V ULP) | 8.4 (IDF 4.4) | **15.2.0+20251204** | `tools/tools.json` at IDF v6.1 |
| Mbed TLS | 2.28 | 4.x with TF-PSA-Crypto (part of IDF 6.1) | IDF 6.0 migration guide, security |
| C library | Newlib | **Picolibc** (IDF 6 default) | IDF 6.0 migration guide, system |
| TinyUSB | `a0e5626` (2.0.17 era) | **0.21.0** (30 Jun 2026) | hathach/tinyusb releases |
| `arduino_tinyusb` component | lib-builder `3eb9cb06af` | lib-builder `release/v6.1` at `dc06dec270dd` (or newer on that branch at session start) | esp32-arduino-lib-builder; no TinyUSB patches pending |
| LittleFS | esp_littlefs `41873c2` + littlefs `f53a0cc` (own download) | managed `joltwallet/littlefs` (Arduino requires `^1.22.1`; resolve to the newest, 1.23.1 on 21 Oct 2026, littlefs 2.11.x) | arduino-esp32 `idf_component.yml`; joltwallet/esp_littlefs releases |
| esptool | from platform | platform's `tool-esptool` 5.4.0; 5.6.0 is newest: pin `platformio/tool-esptool` if the registry has it | espressif/esptool releases |
| PlatformIO Core | 6.2.0 | 6.2.0 (newest) | platformio-core releases |
| Unity | from PlatformIO | 2.7.0 if PlatformIO's `test_framework` lets it be pinned | ThrowTheSwitch/Unity |
| cppcheck (CI) | runner's | 2.22.0 | danmar/cppcheck |
| clang-format | WinLibs LLVM | keep 100-column style; LLVM 23.1.5 is newest | llvm-project |
| CI Python | 3.12 | newest that PlatformIO 6.2.0 and IDF 6.1 tools install on (3.15.0 released 9 Oct 2026; IDF needs 3.10+): try 3.15, else 3.14 | python.org |
| CI Node | 22 | **26** (Active LTS from 28 Oct 2026) | nodejs/release schedule |
| pytest / pytest-timeout / pyserial | 9.1.1 / >=2.3 / >=3.5 | 9.2.0 / 2.5.0 / 3.5 | GitHub releases |
| GitHub Actions | checkout v7, setup-python v7, setup-node v7, cache v6, upload-artifact v7 (by SHA) | newest tags (checkout 7.0.1, setup-python 7.1.0, setup-node 7.1.0, cache 6.1.0, upload-artifact 7.0.2), still pinned by SHA | GitHub releases |
| xterm.js, IBM Plex Mono | 6.0.0, Fontsource 5.3.0 | unchanged (6.0.0 is newest; check Fontsource at session 4) | xtermjs releases |

Check again at the start of each session whether a newer **stable** release has appeared
(Arduino 3.3.13 or 4.0.0, IDF 6.1.1, platform 8.0.x). A patch release of the same line is
taken; a new minor or major is a question for Jon.

**Fallback:** if IDF 6.1 hits a problem that can't be fixed in the session, drop to
platform 8.0.0's own mixed pairing (IDF 5.5.5, `framework-espidf@4.50505.0`, GCC
14.2.0+20260121). Then the legacy-driver and Mbed TLS 4 work is still worth doing (both
are deprecated in 5.5), so nothing below is wasted.

## What the spike found (full list, route B)

Build stages that already work on the branch: package install, IDF Python environment,
component manager, CMake configure, compiling all of IDF 6.1 and Arduino 3.3.12, and the
project's `main.cpp`, web server, OTA, Wi-Fi, LittleFS and all `lib/` logic. Remaining
errors, by area:

1. **Warnings as errors (build policy).** IDF 6 makes default warnings errors
   (`CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS` now `n`). The project's `-Wall -Wextra` in
   `build_flags` reach framework sources, so IDF and Arduino headers fail with
   `-Wmissing-field-initializers` (`gpspi_flash_ll.h:190`, `ESP_I2S.cpp:35`), plus
   `#warning` in Arduino's `ssl_client.cpp:28` (mbedTLS key exchange) and IDF's
   `freertos/xtensa_api.h` (moved header). The spike sets
   `CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y` only to see past them.
2. **TinyUSB.** The 2.0.17-era component and TinyUSB don't match Arduino 3.3.12:
   `esp32-hal-tinyusb.c:240-251` needs `tusb_rhport_init_t`, `TUSB_ROLE_DEVICE` and the
   two-argument `tusb_init()`; the old `tusb_config.h` redefines `CFG_TUD_*` and
   `CFG_TUSB_OS`. The new lib-builder component builds the DWC2 driver
   (`portable/synopsys/dwc2/dcd_dwc2.c`), not `dcd_esp32sx.c`.
3. **Mbed TLS 4.** `mbedtls/ctr_drbg.h` and `mbedtls/md5.h` are no longer public. The
   spike moved `device_crypto.*` and `web_service.cpp` to PSA (`psa_hash_compute`,
   `psa_hash_setup/update/finish`, `psa_generate_random`; MD5 stays available,
   `CONFIG_MBEDTLS_MD5_C` defaults on). Builds; **not reviewed** (see session 1).
4. **Legacy I2S driver removed** (`driver/i2s.h`): `lib/hw/src/hw.cpp:49-96`, `:180-204`.
5. **Legacy RMT driver removed** (`driver/rmt.h`, ring buffer API): `hw.cpp:428-517`.
   The new API (`rmt_new_rx_channel`, `rmt_receive` with `flags.en_partial_rx`,
   `rmt_rx_done_event_data_t.flags.is_last`) exists in IDF 6.1.
6. **`PIN_INPUT_ENABLE`** gone for the S3 (`hw.cpp:424`): use `gpio_input_enable()`.
7. **Wi-Fi:** `WIFI_REASON_ASSOC_EXPIRE` removed (`wifi_radio.cpp:61`). Spike: folded into
   `WIFI_REASON_AUTH_EXPIRE`, as the migration guide says.
8. **GCC 15 / types:** `std::min(6u, uint32_t)` and `std::max(1u, uint32_t)` in
   `lib/clienttest/src/client_service.cpp:452`, `:469` (`uint32_t` is `unsigned long`).
   Spike: explicit `<uint32_t>`; native `test_client` passes.
9. **Deprecation:** `neopixelWrite()` -> `rgbLedWrite()` (`wifi_radio.cpp:310`).

Not errors, but they change: the UART register poke `uartSync()` (`hw.cpp:315-322`)
still compiles, and Arduino 3.3.12 now changes speed through IDF's `uart_set_baudrate()`,
whose HAL calls `uart_ll_update()`: bench-check whether `uartSync()` is still needed.
`esp_core_dump_get_summary` compiles unchanged.

## Session 1: build policy and mechanical fixes (Sonnet 5.5 / high, L)

Goal: everything except TinyUSB, I2S and RMT is final, reviewed and tested.

1. Rebase; check for newer stable releases (above). Confirm the `platformio.ini` pins.
2. **Warnings policy.** Move `-Wall -Wextra` so they apply only to the project's own
   code (`build_src_flags`, and check whether `lib/` sources get them; if not, use a
   `pre:` script that adds them to the project libraries' environments). Remove
   `CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y`. IDF's default warning errors then apply
   to project code too: fix what appears. If a framework file still fails, record it
   and suppress that one warning for that file, not globally.
3. **`sdkconfig.defaults`** rewritten with IDF 6.1 names (no rename-table reliance):
   `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`, `CONFIG_SPIRAM`, `CONFIG_ESP_TASK_WDT_INIT`;
   drop `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF` (ELF is the only format now) and the
   LittleFS keys if the managed component renamed them. Check each key against the
   generated `sdkconfig.s3`. Keep the comments' reasons.
4. **Trim Arduino's managed dependencies.** Arduino 3.3.12's manifest pulls Zigbee,
   Modbus, ESP-DSP, `esp_modem`, MP3, mDNS, network provisioning and Ethernet PHY drivers
   into `managed_components/`. Use `CONFIG_ARDUINO_SELECTIVE_COMPILATION` (keep the
   libraries the project includes: WiFi, Network, LittleFS, FS, Preferences, Update,
   ArduinoOTA, ESPmDNS if OTA needs it, USB) and, if the component manager still
   downloads them, a project `idf_component.yml` override. Measure build time and flash.
5. **LittleFS:** remove `esp_littlefs`/`littlefs` from `scripts/network_dependencies.py`
   (spike already dropped the CMake line); pin `joltwallet/littlefs` to an exact version
   in a project `idf_component.yml`; commit `dependencies.lock`. **Check the on-disk
   format:** littlefs 2.11 must mount the v2.17 file system, and decide whether it may
   upgrade it (a downgrade to v2.17 then needs the old littlefs to read it). If the
   component offers a "disk version" setting, keep 2.0 so a rollback keeps working.
6. **PSA review** (spike code in `device_crypto.cpp`, `web_service.cpp`): where
   `psa_crypto_init()` belongs (once in `setup()` before Wi-Fi, ADC or I2S, while
   `bootloader_random_enable()` is on); how IDF 6.1's PSA generator reseeds and from what
   (`esp_fill_random`; the old code refused to reseed with the radio off). If PSA can
   reseed from a weak source, use `esp_fill_random()` only while the radio is on, as
   `WebCrypto::random()` already does, and keep a seeded generator otherwise. Wipe rules
   (SEC-07e) for the PSA hash contexts. Native tests are unchanged (the logic is in
   `lib/devicecfg`); add a native test only if logic moves.
7. Small items: `PIN_INPUT_ENABLE` -> `gpio_input_enable()`; `rgbLedWrite()`; review the
   Wi-Fi reason mapping; `.gitignore` gets `/managed_components/`.
8. Picolibc: keep the IDF default unless something breaks. Grep for `<sys/signal.h>`,
   per-task `stdout`, `%ll` printf use and `printf` float formatting; note in the
   decisions log.
9. Native tests (full), lint, clang-format. Firmware build expected to fail only at TinyUSB
   and `hw.cpp`.

Cut line: 1-3 and 7 are the session; 4-6 can move to session 2.

## Session 2: TinyUSB (Opus 5.5 / high, L)

1. `scripts/network_dependencies.py`: replace the lib-builder and TinyUSB pins with the
   `release/v6.1` commit and the TinyUSB `0.21.0` tag (codeload tarballs, SHA-256 checked
   as now; `scripts/network_dependencies.py`'s tests in `test/scripts` updated).
   Rename the script if LittleFS has gone and it only fetches USB sources.
2. Rewrite `scripts/arduino_tinyusb/CMakeLists.txt` from the lib-builder's new one: S3
   (`OPT_MCU_ESP32S3`), device only (no host classes), CDC only, the DWC2 device driver;
   register as an ordinary component as before. Arduino adds it through
   `maybe_add_component(arduino_tinyusb)` when `CONFIG_TINYUSB_ENABLED`.
3. Reconcile `tusb_config.h` with the project's `CONFIG_TINYUSB_*` keys (the spike saw
   redefinitions); check which Kconfig the new component reads.
4. `lib/hw/src/usb_port.cpp`: keep the project's CDC registration and flow control
   (decisions log 2026-11-08) and the `tud_ready()` guard (2026-11-13: the empty-socket
   crash was in `dcd_esp32sx.c`; the DWC2 driver replaces it, keep the guard anyway).
   Arduino's own USB classes (`USBHost*`, `USBMSC`) must not be built or linked.
5. Build to the `hw.cpp` errors only. `credits/LICENSES.md` row for TinyUSB and the
   component updated.

Bench (Jon, end of session 3 or in session 5): `pio test -e s3 -f embedded/test_usb`,
`e2e/test_usb.py`, and the rear USB-C left empty and with a power-only lead for 5 minutes
(no reset).

## Session 3: I2S, RMT capture and first link (Opus 5.5 / high, L)

1. **I2S** to the standard-mode driver (`driver/i2s_std.h`): one TX channel, 16-bit
   stereo as now, same pins and sample rate, DMA sizes like the old `dma_buf_count/len`.
   `i2s_zero_dma_buffer` -> disable/enable or writing silence; the non-blocking write at
   `hw.cpp:198` (`timeout 0`) must stay non-blocking. Every sample still goes through
   `volume::outputGain()`. The MAX98357A SD pin handling is unchanged.
2. **RMT speed capture** to the new RX API: `rmt_new_rx_channel` on `pins::kTxd`
   (shared with UART1 RX; check the IDF 6 note that several drivers may share one GPIO),
   1 MHz resolution, `signal_range_min_ns` 1000, `signal_range_max_ns` from
   `kCaptureIdleUs` (32 ms is under the 15-bit limit at 1 MHz; confirm), memory as large
   as allowed (consider `with_dma`: the S3 has RX DMA on one channel), and
   `en_partial_rx` so a 128-character line still fits. The receive-done callback (ISR)
   copies symbols into a FreeRTOS ring or stream buffer; `captureRead()` keeps its
   current contract and run format (`autobaud::Run`, merge rule, zero duration = end), so
   `lib/hostport/src/autobaud.cpp` and `test/native/test_autobaud*` don't change. Restart
   `rmt_receive()` after each `is_last`. `captureBegin/End/Active` keep their meaning.
3. **First full link.** Record flash and RAM (`pio run -e s3` size lines) beside v2.17's,
   in this file and the decisions log. Check the 4 MB app slot has room (Mbed TLS 4 PSA
   is larger, per the IDF 6.0 migration guide).
4. `uartSync()`: leave it in, add a bench item to test without it.

Bench (Jon): `embedded/test_rs232`, `embedded/test_hw`, sound playback at full volume and
at the pot's ends, `AT*BAUD=AUTO` at 300, 2400, 9600, 38400, 115200 with a 128-character
line, speed change during a call refused (STA-09b).

## Session 4: tooling, CI and docs (Sonnet 5.5 / high, L)

1. **CI** (`.github/workflows/ci.yml`, `release.yml`): cache keys include the new
   platform and package versions; Python and Node versions (table above); pytest 9.2.0;
   newest action SHAs; cppcheck 2.22.0 if CI installs it; `managed_components/` cached or
   rebuilt; the release still publishes bootloader, partition table, app and LittleFS
   images (check the IDF 6.1 bootloader path and name in `.pio/build/s3`).
2. **Scripts:** `scripts/ota_upload.py` and `load_env.py` call `espota.py` internals; the
   3.3.12 `espota.py` changed (SHA-256 / PBKDF2 challenge, `use_md5_password`,
   `use_old_protocol`), so adapt and test with `test/scripts`. `pin_config.py`,
   `version.py`, `build_web.py`, `pre_upload_tests.py`, `image_manifest.py`: run and fix.
   `test/e2e/requirements.txt`: exact pins.
3. **VS Code tasks:** unchanged commands; check each still runs (they use `pio`).
   During WP-18 something replaced `.gitignore` with PlatformIO's 5-line default (CRLF)
   after `platformio.ini` changed, most likely the VS Code extension re-initialising the
   project; a `git add -A` then picked up `build/`, `managed_components/` and
   `web/preview.html`. Find the cause, and always check `git status` before committing.
4. **Docs:** CLAUDE.md (build table, hardware summary: Arduino 3.3.12, IDF 6.1, pins in
   `platformio.ini`), README, `credits/LICENSES.md` (Arduino-ESP32 3.3.12, IDF 6.1 lwIP
   and HTTP server, Mbed TLS 4 / TF-PSA-Crypto, TinyUSB 0.21.0, joltwallet/littlefs,
   managed components that end up linked), `test/README.md` (anything moved),
   `web/PROTOCOL.md` (no change expected), `CHANGES.md` [3.0] entry in full.
5. Full end checks from README.md step 7.

## Session 5: bench run with Jon (Sonnet 5.5 / medium)

Ask Jon for COM ports and what is wired first. In this order:

1. **USB flash** of v3.0 (bootloader, partitions, app, LittleFS): the clean route.
2. All on-board suites: `pio test -e s3` (then re-upload the firmware).
3. End-to-end: `test/e2e` full run with `--rs232-port`, plus `test_usb.py`,
   `test_internet.py` (PPP and SLIP; stack headroom check), `test_web.py --browser-upload
   --upload-tests`, transfers (X/Y/ZMODEM with lrzsz).
4. **OTA from v2.17 to v3.0** on a second flash of v2.17: ArduinoOTA with the device
   password (v2.17 has the old MD5 protocol; the new `espota.py` must detect it), then
   the web firmware upload. The old IDF 4.4 bootloader stays: check the board boots,
   PSRAM (octal) works and `AT*RESETINFO` reads the old core dump without crashing (the
   core-dump format and checksum changed: SHA-256 now). Then OTA from v3.0 to v3.0.
5. **Settings survive:** NVS (settings, phonebook, Wi-Fi) and LittleFS (sounds, web
   files, call history) from v2.17 are read by v3.0 unchanged. Then a rollback OTA to
   v2.17: does it still mount LittleFS (see session 1 item 5)?
6. Wi-Fi join (WPA2, WPA3 if Jon's network has it, open), browser 60 s rollback, scan,
   `AT*FACTORY` (also clears `nvs.net80211`).
7. Heap and stack low-water marks with a call, the web page and PPP together
   (`AT*STATUS`); 30-minute soak with the watchdog (STA-06) on.
8. `uartSync()` removed on a test build: speed changes at 300 and 115200 are right.

Release notes for v3.0 say: USB flash is the clean route; OTA works but keeps the old
bootloader.

## Questions for Jon (before or during session 1)

1. **Rollback after upgrade:** is a downgrade to v2.17 by OTA something the release must
   support? (Decides the LittleFS disk-version setting and whether v3.0 may rewrite
   stored data.)
2. **OTA password hash:** the device stores an MD5 hash for ArduinoOTA. Arduino 3.3.12
   accepts it but logs it as deprecated and prefers SHA-256. Keep MD5 for v3.0 (no
   functional change) and move to SHA-256 in a later version? (Recommended: yes.)
3. **Warnings:** may IDF's warnings-as-errors apply to the project's own code (stricter
   than v2.x)? (Recommended: yes.)

## Rules for this version

- One version bump for the whole branch (v3.0, done in WP-18).
- No functional changes mixed in, so a regression can be traced to the framework move.
  Bugs found on the way get a decisions-log row and, unless they block, a follow-up.
- Decisions log entry per decision; `credits/LICENSES.md` for changed third-party code.
- **Never run the pioarduino platform against the shared PlatformIO install**: its first
  run replaced PlatformIO Core in `~/.platformio/penv` (WP-18). Use an isolated
  `PLATFORMIO_CORE_DIR` if it is ever tried again.
- The spike's code edits are marked `v3.0 SPIKE`; each session removes the marks it
  resolves. None may be left at merge.
