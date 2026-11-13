# WP-19 (v3.0, sessions 2-8): Framework migration and fresh start

| Item | Value |
| --- | --- |
| Model / effort | per session, below |
| Size | 6 sessions of L, then a bench session with Jon |
| Branch | `firmware/v3.0-idf5` (same branch as WP-18; the name predates the IDF 6.1 decision and stays) |
| Depends on | WP-18 (done 13 Nov 2026). WP-01 to WP-17 are merged; rebase on `origin/main` at the start of each session |
| Firmware change | yes, platform-wide |
| Push | the branch stays **local**. One push and one pull request after session 7, when Jon says so ("Rules" below) |
| Device | any session may flash and test the connected board as needed, without asking; no backups, no migration ("Testing on the board" below) |

Follow the token rules in [README.md](README.md). Read this file and, for the session you
are doing, only the files it names. The spike's evidence is in
[wp-18-framework-spike.md](wp-18-framework-spike.md) (section "Result").

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
6. **Functional changes** in v3.0 are limited to 3, 5 and 8 and to removing migration
   code. Anything else found on the way gets a decisions-log row and a follow-up, so a
   regression can be traced to the framework move.
7. **`docs/project-brief.md` is trimmed to the current firmware spec** (session 6). This
   copy stops being a snapshot of the shared master and becomes the repo's firmware spec,
   kept current with the code.
8. **Obsolete features are removed** in session 5, not just listed (definition there).
9. **Rewrite to the new framework's way, don't patch.** Where the framework move touches
   code, use the approach Arduino 3.3 and IDF 6.1 now recommend (current drivers,
   components and APIs), even if that means rewriting the module, rather than the
   smallest change that compiles. No shims that keep an old API or approach alive, and
   no deprecated APIs in project code (`-Werror=deprecated-declarations`). The same
   applies to project code the build doesn't force to change: see "Modernisation
   review" below. Behaviour seen from outside (commands, results, timing, the web
   protocol) stays the same unless a decisions-log row says otherwise; the native, e2e
   and bench tests are the check.

## Target versions (checked 13 Nov 2026)

| Component | v2.17 | v3.0 | Source |
| --- | --- | --- | --- |
| PlatformIO platform | `espressif32@6.13.0` | `espressif32@8.0.0` (19 Oct 2026) | platformio/platform-espressif32 releases |
| Arduino core | 2.0.17 | **3.3.12** (`framework-arduinoespressif32@4.30312.261024`) | espressif/arduino-esp32 releases; 4.0.0 is RC1 only |
| ESP-IDF | 4.4.7 (EOL July 2024) | **6.1.0** (`framework-espidf@4.60100.0`) | esp-idf releases; support periods chart |
| GCC (Xtensa, RISC-V ULP) | 8.4 | **15.2.0+20251204** | `tools/tools.json` at IDF v6.1 |
| Mbed TLS | 2.28 | 4.x with TF-PSA-Crypto (part of IDF 6.1) | IDF 6.0 migration guide, security |
| C library | Newlib | **Picolibc** (IDF 6 default) | IDF 6.0 migration guide, system |
| TinyUSB | `a0e5626` | **0.21.0** (30 Jun 2026) | hathach/tinyusb releases |
| `arduino_tinyusb` component | lib-builder `3eb9cb06af` | lib-builder `release/v6.1` at `dc06dec270dd` (or newer on that branch) | esp32-arduino-lib-builder; no TinyUSB patches pending |
| LittleFS | own download of esp_littlefs + littlefs | managed `joltwallet/littlefs`, pinned to the newest (1.23.1 on 21 Oct 2026) | arduino-esp32 `idf_component.yml` |
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
and Mbed TLS work below is still needed there (deprecated in 5.5).

## What the spike found (route B, full list)

Working on the branch: package install, IDF Python environment, component manager, CMake
configure, all of IDF 6.1 and Arduino 3.3.12, and all project code except the items
below. Remaining errors:

1. **Warnings as errors.** IDF 6 makes default warnings errors. The project's `-Wall
   -Wextra` reach framework sources (`-Wmissing-field-initializers` in `gpspi_flash_ll.h`,
   `ESP_I2S.cpp`; `#warning` in Arduino's `ssl_client.cpp` and IDF's
   `freertos/xtensa_api.h`). The spike sets `CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y`
   to see past them.
2. **TinyUSB.** The old component and TinyUSB don't match Arduino 3.3.12
   (`esp32-hal-tinyusb.c` needs `tusb_rhport_init_t`, `TUSB_ROLE_DEVICE`, the
   two-argument `tusb_init()`; `CFG_TUD_*` redefined). The new lib-builder component
   builds the DWC2 driver, not `dcd_esp32sx.c`.
3. **Mbed TLS 4.** Spike moved `device_crypto.*` and `web_service.cpp` to PSA
   (`psa_hash_*`, `psa_generate_random`; MD5 still available). Builds; not reviewed.
4. **Legacy I2S driver removed** (`lib/hw/src/hw.cpp`, the `i2s_*` calls).
5. **Legacy RMT driver removed** (`hw.cpp`, speed capture). The new API has
   `rmt_receive` with `flags.en_partial_rx` and `is_last`. With the RGB LED gone, the
   capture is the only RMT user.
6. **`PIN_INPUT_ENABLE`** gone for the S3 (`hw.cpp`, `ctsOutAsserted()`): use
   `gpio_input_enable()`.
7. Fixed in the spike: `WIFI_REASON_ASSOC_EXPIRE` (folded into `AUTH_EXPIRE`), GCC 15
   `std::min`/`std::max` types in `client_service.cpp`, LittleFS CMake line.

Also: `uartSync()` (`hw.cpp`) still compiles, and Arduino 3.3.12 now changes speed through
IDF's `uart_set_baudrate()`, which calls `uart_ll_update()`: bench-check whether it's still
needed. `esp_core_dump_get_summary` compiles unchanged.

## Modernisation review (decision 9)

Compiling is not the test. Each session checks the code it touches against the current
Arduino 3.3 and IDF 6.1 documentation and migration guides (read the current docs online,
not from memory), and rewrites where the recommended approach has changed. Candidates
found in a survey of `lib/` and `src/` on 13 Nov 2026 (check each; keep the current code
where it is already the recommended way, and say why in the session's notes):

| Area | Now | Look at | Session |
| --- | --- | --- | --- |
| Warnings | `-Wall -Wextra` | plus `-Werror=deprecated-declarations` on project code | 1 |
| Random and hashes | PSA (spike) | review as written; no Mbed TLS 2 idioms left | 1 |
| GPIO | `soc/gpio_periph.h`, `PIN_INPUT_ENABLE` | `driver/gpio.h` calls only, no register access | 1 |
| SNTP | `esp_sntp.h` | `esp_netif_sntp` API, or Arduino's `configTime` if enough | 1 |
| mDNS | `ESPmDNS` | still right with the managed `espressif/mdns`; check | 1 |
| ADC (volume pot) | `analogReadMilliVolts` | fine if it uses `adc_oneshot` and calibration in 3.3; check | 1 |
| TinyUSB | lib-builder `arduino_tinyusb` sources fetched by a script, own CDC registration | whether Arduino 3.3's own USB stack (`USB`, `USBCDC`) or a registry component (`espressif/tinyusb`, `esp_tinyusb`) now covers the project's needs (CDC, line state, flow control, the `tud_ready()` guard), which would remove the fetch script and the custom component | 2 |
| I2S | legacy `driver/i2s.h` | `i2s_std` channel API (or Arduino's `ESP_I2S` if it meets the non-blocking need) | 3 |
| RMT | legacy `driver/rmt.h` | RX channel API, ISR callback into a stream buffer | 3 |
| UART | `hal/uart_ll.h`, `uartSync()` | IDF `uart_*` driver calls only; drop `uartSync()` if the bench shows it isn't needed | 3 and 7 |
| PPP, SLIP, NAPT | raw lwIP (`netif`, `tcpip`, `lwip_napt.h`) | whether `esp_netif` (PPP) or Arduino 3.3's `Network`/`PPP` classes fit a DCE-side server; SLIP stays raw lwIP if nothing covers it | 4 |
| Wi-Fi | `WiFi.onEvent`, `esp_wifi.h` | Arduino 3.3 `Network` events; direct `esp_wifi` calls only where Arduino has no API | 4 |
| OTA | `ArduinoOTA`, `Update`, `esp_ota_ops` | SHA-256 auth (decision 3); otherwise check | 4 |
| Watchdog | `esp_task_wdt_reset()`, set up by `sdkconfig` | check the IDF 6.1 keys and that the loop task is subscribed | 1 |
| Settings | `Preferences` in `hw/nvs_store.cpp` and `src/main.cpp` | all access through `nvs_store` (fresh start: layout 1) | 5 |

A rewrite that changes behaviour is a decisions-log row; a large one that doesn't fit its
session gets its own follow-up session rather than a shim.

## Session 1: build policy, fixes and modernisation (Sonnet 5.5 / high, L)

Goal: everything except TinyUSB, I2S, RMT and the session 4 rows of the modernisation
review is final, reviewed and tested, and written the way Arduino 3.3 and IDF 6.1
recommend (decision 9).

1. Rebase; check for newer stable releases. Confirm the `platformio.ini` pins.
2. **Warnings policy.** `-Wall -Wextra` only on the project's own code (`build_src_flags`;
   if `lib/` doesn't get them, a `pre:` script adds them to the project libraries'
   environments), plus `-Werror=deprecated-declarations`. Remove
   `CONFIG_COMPILER_DISABLE_DEFAULT_ERRORS=y`; fix what IDF's
   default errors then find in project code. A framework file that still fails gets that
   one warning suppressed for that file, recorded.
3. **`sdkconfig.defaults`** written with IDF 6.1 names (no rename tables):
   `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240`, `CONFIG_SPIRAM`, `CONFIG_ESP_TASK_WDT_INIT`;
   drop `CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF` (only format now). Check each key in the
   generated `sdkconfig.s3`.
4. **Trim Arduino's managed dependencies** (Zigbee, Modbus, ESP-DSP, `esp_modem`, MP3,
   mDNS, provisioning, Ethernet PHYs land in `managed_components/`):
   `CONFIG_ARDUINO_SELECTIVE_COMPILATION` keeping only the libraries the project includes,
   and a project `idf_component.yml` if downloads remain. Measure build time and flash.
5. **LittleFS:** drop esp_littlefs/littlefs from `scripts/network_dependencies.py`; pin
   `joltwallet/littlefs` exactly in a project `idf_component.yml`; commit
   `dependencies.lock`. Default disk version (fresh start).
6. **PSA review** (`device_crypto.cpp`, `web_service.cpp`): `psa_crypto_init()` once in
   `setup()` before Wi-Fi, ADC or I2S, while `bootloader_random_enable()` is on; how
   IDF 6.1's PSA generator reseeds and from what (the old code refused to reseed with the
   radio off). If it can reseed from a weak source, keep a generator seeded at boot.
   Wipe rules for PSA hash contexts.
7. **GPIO:** `PIN_INPUT_ENABLE` and `soc/gpio_periph.h` go; `driver/gpio.h` calls only
   (`gpio_input_enable()`).
8. **Modernisation review, session 1 rows** (SNTP, mDNS, ADC, watchdog): rewrite where
   the recommended approach changed; note the ones kept and why.
9. Picolibc stays unless something breaks (check `<sys/signal.h>`, per-task `stdout`,
   `%ll` and float `printf`).
10. Native tests (full), lint, clang-format. Firmware build should fail only at TinyUSB
    and `hw.cpp`.

Cut line: 1-3 and 7; 4-6 and 8 can move to session 2.

## Session 2: TinyUSB (Opus 5.5 / high, L)

0. **Choose the USB route first** (modernisation review, TinyUSB row). Compare, from the
   current docs: Arduino 3.3's own `USB`/`USBCDC` classes as an IDF component; the
   registry components (`espressif/tinyusb`, `espressif/esp_tinyusb`); and the
   lib-builder sources below. Pick the one Espressif recommends for an Arduino-as-component
   project that meets the project's needs (CDC device, DTR/RTS line state, flow control,
   the `tud_ready()` guard, rear USB-C with nothing attached). Prefer the route with the
   least project-owned build glue; record the choice in the decisions log. Steps 1-4
   are the lib-builder route; change them to fit if another route wins.
1. `scripts/network_dependencies.py`: lib-builder `release/v6.1` commit and TinyUSB
   `0.21.0` (codeload tarballs, SHA-256 checked; `test/scripts` updated). Rename the
   script if it now only fetches USB sources.
2. Rewrite `scripts/arduino_tinyusb/CMakeLists.txt` from the lib-builder's: S3
   (`OPT_MCU_ESP32S3`), device only, CDC only, DWC2 device driver; ordinary component.
3. Reconcile `tusb_config.h` with the project's `CONFIG_TINYUSB_*` keys.
4. `lib/hw/src/usb_port.cpp`: keep the project's CDC registration, flow control and the
   `tud_ready()` guard. Arduino's own USB classes (`USBHost*`, `USBMSC`) not built.
5. Build to the `hw.cpp` errors only. `credits/LICENSES.md` row for TinyUSB.

## Session 3: I2S, RMT capture and first link (Opus 5.5 / high, L)

1. **I2S** standard-mode driver (`driver/i2s_std.h`): one TX channel, 16-bit stereo, same
   pins and rate, DMA sizes like now; the non-blocking write stays non-blocking; every
   sample through `volume::outputGain()`; MAX98357A SD pin handling unchanged.
2. **RMT speed capture:** `rmt_new_rx_channel` on `pins::kTxd` (shared with UART1 RX),
   1 MHz, `signal_range_min_ns` 1000, `signal_range_max_ns` from `kCaptureIdleUs`
   (32 ms; check the 15-bit limit), largest memory (consider `with_dma`),
   `en_partial_rx`. The ISR callback copies symbols into a FreeRTOS ring or stream
   buffer; `captureRead()` keeps its contract and run format (`autobaud::Run`, merge rule,
   zero duration = end), so `lib/hostport` and its tests don't change. Restart
   `rmt_receive()` after each `is_last`.
3. **First full link.** Record flash and RAM beside v2.17's in the decisions log; check
   the 4 MB app slot.
4. **UART:** speed changes through IDF `uart_*` calls only, no `hal/uart_ll.h`.
   `uartSync()` stays only if it can be written that way; the bench (session 7) tests a
   build without it.
5. **First run on the board** (once it links): full erase, USB flash of the firmware and
   LittleFS, setup from `.env`, then a smoke test (boot banner, `ATI`, a test call, the
   sound, `AT*BAUD=AUTO` at one speed, the USB modem port). Problems found here are
   fixed in this session where they belong to its work.

## Session 4: networking, tooling, CI and the OTA hash (Opus 5.5 / high, L)

0. **Modernisation review, session 4 rows** (PPP, SLIP, NAPT; Wi-Fi; OTA). PPP and the
   Wi-Fi events are the likely rewrites. Decide from the current docs whether `esp_netif`
   or Arduino 3.3's `Network` classes can serve PPP on the DCE side (the modem is the
   server, the host computer the client). If they can, move to them; if not, record why
   raw lwIP stays. `e2e/test_internet.py` must pass unchanged on the bench. If the PPP
   rewrite is large, it becomes its own session after this one (renumber the rest).

1. **CI** (`ci.yml`, `release.yml`): cache keys for the new packages; Python, Node,
   pytest and action versions (table); `managed_components/`; the release still publishes
   bootloader, partitions, app and LittleFS images (IDF 6.1 paths).
2. **Scripts:** `ota_upload.py` and `load_env.py` call `espota.py` internals, which
   changed in 3.3.12 (SHA-256 / PBKDF2 challenge): use the new protocol only; tests in
   `test/scripts`. Run and fix `pin_config.py`, `version.py`, `build_web.py`,
   `pre_upload_tests.py`, `image_manifest.py`. Exact pins in
   `test/e2e/requirements.txt`.
3. **OTA password hash = SHA-256.** `Crypto::hashes()` gives the OTA hash as SHA-256 hex
   (65-byte buffer); `lib/devicecfg` record has the longer field (layout 1, fresh start);
   `OtaService` passes it to `setPasswordHash`. The web Digest HA1 stays MD5. Native
   tests in `test_devicecfg` first; the canary sweeps (both passwords, OTA hash, Digest
   hash) still pass; e2e OTA test.
4. **VS Code tasks:** check each still runs. During WP-18 something replaced
   `.gitignore` with PlatformIO's 5-line default after `platformio.ini` changed (likely
   the VS Code extension re-initialising the project) and `git add -A` then picked up
   `build/`, `managed_components/` and `web/preview.html`. Find the cause; always check
   `git status` before committing.
5. Full end checks (README.md step 7).

## Session 5: fresh start, code (Sonnet 5.5 / high, L)

Rule for every file outside `case/` and `electronics/`: keep what someone needs to
understand, build, test and change the **current** design with a new ESP32-S3; remove the
rest. Keep the reason for a non-obvious choice; drop who decided it and when, the version
it arrived in, what it used to do, and review-finding IDs. Inventory on 13 Nov 2026: about
74 version tags, 106 finding IDs (`SEC-`, `STA-`, `PERF-`, `MNT-`, `TG-`, `5.5-`) and 82
brief or `Q` references in `lib/`, `src/`, `test/`, `scripts/`, `web/src/`.

1. **Stored data, one layout each, starting at 1.** Remove migration paths and their
   tests: `modemcfg` (`kMigrated`, layouts 1-4, the "converted from an older firmware's
   format" text, `kLayoutVersion = 5`), `clienttest/client_settings` (layout 1), and check
   `devicecfg`, `netcfg`, `internet`, the phonebook, call history, `webinterface`
   (`web_policy.h`'s separate namespace "to avoid migrating"), `hw::FileStore` and the
   `nvs.net80211` handling. Keep the version byte and the CRC: an unknown layout or a bad
   CRC still loads factory settings (that is protection, not migration). NVS namespace
   and key names may be tidied (fresh flash).
2. **Comments and messages:** strip version tags, finding IDs, `Q` numbers, names and
   dates; brief section references (such as "brief 6.6") stay, and session 6 fixes them
   after the trim. Remove "before vX this did Y" notes. Same for `sdkconfig.defaults`,
   `platformio.ini`, `CMakeLists.txt`, `.github/workflows`, `web/src` and the test files.
   Remove the `v3.0 SPIKE` marks once each is resolved.
3. **Pins:** `lib/hw/src/pins.h`, `.env.example`, `test_pins` and `scripts/pin_*.py`
   describe the current board only (no "rev A/rev B", `Q76`/`Q89`, old assignments);
   test names without `rev_b`.
4. **Tests:** names describe behaviour, not the finding or version that added them;
   delete tests that only exercise removed migration code. The remaining suites with
   their own copies of the shared fakes (listed in `test/README.md`) move to
   `test/native/support/fakes.h` if it shortens them.
5. **Dead code and files:** cppcheck `unusedFunction` and a grep for unused scripts
   (`scripts/allow_call_tests.ps1`, `allow_ota.ps1`, `test_refusal.ps1`,
   `make_preview.py`, `build_handshakes.py`, `generate_credits.py`: check what uses each).
6. **Obsolete features: remove them** (decision 8). Obsolete means a feature that is
   superseded by another one, kept only for compatibility with an older version (old
   command spellings, aliases, settings formats, protocol variants), or a development or
   diagnostic aid that served an earlier version's work and that no current test or
   bench step uses. A feature in the firmware spec (brief section 6) or in a current e2e
   or bench test is not obsolete. When it isn't clear, ask Jon rather than guess. Remove
   each one completely: code, `AT*` command and `AT*HELP` line, configuration screen and
   web page entries, settings fields, tests, `docs/COMMANDS.md`, README and
   `web/PROTOCOL.md`. **One commit per feature**, so a regression can be traced to it;
   one decisions-log row listing them all, and a CHANGES.md "Removed" line each.
7. **Jon's `.env`** (never print it): compare key names with `.env.example` and remove
   keys that are no longer read. If its pin values equal `.env.example`'s, remove those
   lines and `USR_MODEM_PINS_OVERRIDDEN`. Compare values in a script that prints only
   key names and "same/different".
8. Full native tests, lint, clang-format, firmware build.

Cut line: 1, 2 and 7; 3-6 can move to session 6.

## Session 6: fresh start, documentation and credits (Sonnet 5.5 / high, L)

1. **Delete** `docs/history/` and, as the last step of WP-19, `docs/plans/` (fold
   anything still useful, such as the session workflow and token rules, into CLAUDE.md).
2. **`docs/decisions-log.md`:** rewrite as the current decisions only, one row each,
   superseded rows gone (the file stays, and new decisions keep being appended).
3. **`CHANGES.md`:** starts at [3.0] as the first entry ("first release of the current
   design"), older entries removed; `scripts/bump_version.py` and `check_version.py`
   still work.
4. **README, `docs/COMMANDS.md`, `AT*HELP`, `test/README.md`, `web/PROTOCOL.md`,
   `docs/ROADMAP.md`:** current behaviour only; no "from vX" notes; ROADMAP becomes what
   is left to do after v3.0 (or goes).
5. **`credits/LICENSES.md` and `credits/`:** only what is in the v3.0 build or the repo
   (Arduino-ESP32 3.3.12, IDF 6.1 with lwIP, HTTP server, Mbed TLS 4 / TF-PSA-Crypto,
   TinyUSB 0.21.0, joltwallet/littlefs, the managed components that end up linked,
   Zimodem parts still used, the sound files in `sound/`, xterm.js, IBM Plex Mono, Unity,
   test peers). Remove rows and licence files for removed sounds, the old pinned LittleFS,
   Arduino 2.0.x and IDF 4.4, and the historical publication notes. Regenerate the
   About-screen credits (`credits_generated.h`).
6. **`ui/`:** keep what `scripts/generate_ui.py` and the tests use and the current
   design spec; remove the Codex prompt, reference images and previews that only record
   how the design came about.
7. **`docs/project-brief.md` → current firmware spec** (decision 7). Keep: the goal, the
   hardware the firmware drives (from section 3), the firmware spec (section 6) and the
   firmware side of the interfaces (section 7), written as the code now behaves; the
   attribution (section 8); only the open questions that are still open. Remove: the case
   and electronics workstreams (sections 4 and 5; `case/` and `electronics/` hold their
   own docs), answered questions, *Proposed* items that were decided (write the decision
   in), and anything the code no longer does. Rename the title to say it is the firmware
   spec. Then grep `lib/`, `src/`, `test/`, `scripts/`, `web/` and the docs for "brief"
   section references and fix each one against the new numbering.
8. **CLAUDE.md:** current project only (no review, history or old plan pointers; build
   table for IDF 6.1; hardware summary). The brief entry becomes "the firmware spec: keep
   it current with the code"; remove the "don't edit" rule and "Jon merges the log back".
9. **`images/`, `data/`, `.vscode/`, `.github/`:** remove anything unused.
10. End checks; `git status` before committing.

## Session 7: bench run with Jon (Sonnet 5.5 / medium)

The full bench run. Earlier sessions will have tested parts of it on the board already;
this session runs all of it on the final code. Check the ports first ("Testing on the
board"); Jon is needed for listening, watching the LEDs and changing cables.

1. **Full flash erase, then USB flash** of v3.0 (`pio run -e s3 -t erase`, then upload and
   uploadfs). Boot banner shows a new device password; factory settings.
2. All on-board suites (`pio test -e s3`; re-upload the firmware after).
3. End-to-end: full `test/e2e` with `--rs232-port`, plus `test_usb.py`,
   `test_internet.py` (PPP, SLIP, stack headroom), `test_web.py --browser-upload
   --upload-tests`, X/Y/ZMODEM with lrzsz.
4. ArduinoOTA v3.0 to v3.0 with the SHA-256 hash; web firmware upload.
5. USB: rear USB-C empty and with a power-only lead for 5 minutes (no reset).
6. Sound at full volume and at the pot's ends; `AT*BAUD=AUTO` at 300, 2400, 9600, 38400,
   115200 with a 128-character line; then a test build without `uartSync()`.
7. Wi-Fi join (WPA2, WPA3 if available, open), browser 60 s rollback, scan,
   `AT*FACTORY`.
8. Heap and stack low-water marks with a call, the web page and PPP together; 30-minute
   soak with the watchdog on.

## Testing on the board (WP-19 only)

Jon's permission for WP-19 (13 Nov 2026); it ends with WP-19 and doesn't carry over to
later work.

- **Any session may flash and test the connected board as needed**, without asking
  first: USB or OTA uploads, `pio run -e s3 -t erase`, `uploadfs`, on-board suites
  (`pio test -e s3`; re-upload the firmware after) and end-to-end tests. Use it from
  session 3 on (the firmware doesn't link before then), whenever a change needs checking
  on real hardware. Don't save it all for session 7.
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
  for session 7.

## Rules for this version

- One version bump for the whole branch (v3.0, done in WP-18).
- **Keep CI runs to a minimum.** CI runs on every push. Commit locally after each
  session; don't push and don't open a pull request between sessions. Run the CI checks
  locally instead: native tests, firmware build, lint, clang-format, `test/scripts` and
  `web` tests. The whole of WP-19 goes up as **one push and one pull request** after the
  session 7 bench run, when Jon says so. If CI then fails, fix it in as few pushes as
  possible: reproduce the failure locally first, then push once.
- Decisions log entry per decision; `credits/LICENSES.md` for third-party code.
- **Never run the pioarduino platform against the shared PlatformIO install**: its first
  run replaced PlatformIO Core in `~/.platformio/penv` (WP-18). Use an isolated
  `PLATFORMIO_CORE_DIR` if it is ever tried again.
- Never print `.env`; scripts that touch it print key names only.
