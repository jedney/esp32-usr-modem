# Code review: USR Modem Emulator v2.0

Reviewed commit `1f51a09` (branch `firmware/v2.0-usb-modem`), 10-11 Nov 2026.
Phases 1 (understand, baseline) and 2 (audit). **No source file was changed.**

## 1. Scope and method

- In scope: `src/`, `lib/`, `web/src/`, `scripts/`, `.github/workflows/`, build
  configuration, and all tests (`test/`, `web/test/`, `ui/tests/`).
- Not reviewed: `case/`, `electronics/`, `images/`, sound files, `docs/history/`, the
  vendored `web/vendor/xterm.mjs` (version and licence only) and the two generated headers
  in `lib/config/src/` (their generators are reviewed instead). `lib/config/ui-bindings.json`
  and `web/src/ui-data.js` are data and were searched, not read line by line.
- Every source file was read in full by a reader (one per module group, eleven in all).
  The highest-risk files were also read by the lead reviewer: `src/main.cpp`,
  `src/rs232.cpp`, `lib/hw/src/web_service.cpp`, `web_service.h`, `tcp_socket.cpp`,
  `tcp_listener.cpp`, `ota_service.cpp`, `device_crypto.cpp`, `file_store.h`,
  `lib/webinterface/src/web_policy.cpp`, `web_policy.h`, `lib/calls/src/incoming.cpp`,
  `telnet.cpp`, `telnet.h`, `byte_queue.h`, `tcp_connection.cpp`, `tcp_connection.h`,
  `lib/hostport/src/session.cpp`, `session.h`, both workflows and `sdkconfig.defaults`.
- **Confirmation rule.** A reader's report is only a candidate. Findings in sections 5.1
  to 5.4 were listed only after the lead opened the cited code and traced the path.
  Section 5.5 lists low-severity reader findings that were **not** re-verified; treat
  them as needing confirmation. Anything that could not be proved from the code says
  **needs confirmation** and what would settle it.
- Severity, for a hobby modem on a home network:
  - **Critical**: compromise, secret disclosure or a bricked unit from the network
    without the password.
  - **High**: crash, hang, lock-out or data loss reachable from the network or in normal
    use.
  - **Medium**: needs an unusual sequence, local access or a hostile LAN.
  - **Low**: quality, clarity, small inefficiency.
- Result in one line: **no critical findings, 1 high, 11 medium, the rest low**, plus
  the test gaps in section 7. The network-facing parsers and the web security layer are
  careful and well bounded. The weak spots are places where one blocked port or one open
  screen holds up the whole single-loop modem, and tests that would not notice if a
  protection were removed.

## 2. Baseline (before any change)

Run on Jon's PC on 10 Nov 2026 with the CI commands. PC only: nothing was uploaded.

| Check | Command | Result |
| --- | --- | --- |
| Version and change log | `python scripts/check_version.py` | Pass (2.0) |
| UI catalogue | `python scripts/generate_ui.py --check` | Pass (clean) |
| UI layout tests | `python -m unittest discover -s ui/tests -v` | 5 passed |
| Script tests | `python -m pytest test/scripts -q` | 15 passed |
| Web unit tests | `node --test test/*.test.mjs` in `web/` | 23 passed |
| clang-format | `clang-format --dry-run --Werror` on tracked `*.cpp`/`*.h` | Clean |
| Unit tests | `pio test -e native` | 402 cases in 15 suites: 401 passed, 1 skipped, 31 s |
| cppcheck | `pio check -e native --fail-on-defect=medium` | Pass: 0 high, 0 medium, 412 low (style) |
| Firmware build | `pio run -e s3` | Success, 122 s. Flash 1,249,873 bytes (29.8% of a 4 MB slot). RAM 145,080 bytes (44.3%) |
| Build warnings | from the build log | 12, all in the Arduino core (`I2S.cpp`, `ssl_client.cpp`); none in project code |
| On-board tests compile | `pio test -e s3 --without-uploading --without-testing` | 12 suites built |

- The skipped unit test is `test_pin_map_is_the_confirmed_one`: it skips when `.env`
  overrides the pin map (see TG-07).
- `git status` was the same before and after: only the untracked `build/` folder.
- **Not run:** on-board tests and end-to-end tests (they need the bench, replace the
  firmware and change the modem's saved state) and the lrzsz/pppd interoperability tests
  (Linux only). There is no type-checker for the JavaScript; the C++ compile is the type
  check.
- The last recorded bench result is in `test/README.md` (v1.0, 27 Oct 2026). No bench run
  is recorded there for v1.1 or v2.0.

## 3. Architecture and data flows

- **Language and build.** C++17 firmware, Arduino core 2.0.17 built as an ESP-IDF 4.4.7
  component (PlatformIO, platform and packages pinned exactly). Plain JavaScript page with
  vendored xterm.js 6.0.0. Python build scripts. GitHub Actions for CI and releases.
- **Entry point.** `src/main.cpp`: `setup()` builds every object; `loop()` polls them
  about once a millisecond (`delay(1)`). Almost everything runs on this one task.
- **Layers.** `lib/<module>` holds logic with no hardware calls (unit-tested on the PC).
  `lib/hw` is the hardware layer. `src/` wires them together.
- **Serial side.** Three ports, each a `hostport::Session` with its own `atcmd::Port`
  parser: console (UART0), RS232 (UART1, `src/rs232.cpp`), USB modem port
  (`src/usbport.cpp`). A fourth session serves the browser. One `CallSlot` is shared: one
  call, menu or test at a time.
- **Outgoing call.** `ATD` → `dialstring` → `calls::Outgoing` → `hw::TcpSocket`
  (asynchronous DNS, non-blocking connect) → `calls::TcpConnection` (Telnet or raw) →
  session goes online.
- **Incoming call.** `hw::TcpListener` on port 23 → `calls::Incoming` (RING, `ATA`, `S0`)
  → `TcpConnection` → session goes online.
- **Internet dial-up.** `ATDT666/667` → `dialup::Dialer` → PPP/SLIP engine
  (`lib/internet`) ↔ `hw::InternetNetwork` (lwIP interface with NAT).
- **Web.** `hw::WebService` runs ESP-IDF's HTTP server in its own task. Requests pass a
  header gate, then Digest authentication. The loop and the server task share state only
  through a mutex, bounded queues and status snapshots.
- **OTA.** ArduinoOTA, polled from the loop only while the modem is idle.
- **Storage.** NVS (20 KB partition): modem settings, `net`, `device`, `phonebook`, `web`,
  `clients`, `internet`, `diagnostics`. LittleFS: `/sounds`, `/www`, `/call-history.bin`.
- **Execution contexts.** Arduino loop task; HTTP server task (core 0, 12 KB stack); lwIP
  thread (DNS, SNTP and NAT callbacks); ping task; TinyUSB task; Wi-Fi event task.

## 4. Trust boundaries

| # | Boundary | Who is on the other side | Entry code | Outcome |
| --- | --- | --- | --- | --- |
| 1 | TCP port 23 | Anyone who can reach the modem; no login | `tcp_listener.cpp`, `incoming.cpp`, `telnet.cpp` | Cannot reach commands (sound). Can hold the line: SEC-01, STA-01 |
| 2 | Outgoing calls | Any Internet host the user dials | `outgoing.cpp`, `telnet.cpp`, `tcp_socket.cpp` | Parser bounded. No keepalive: SEC-01 |
| 3 | HTTP and WebSocket, port 80 | LAN hosts; Digest login for everything | `web_service.cpp`, `web_policy.cpp` | Sound. Availability only: SEC-02 |
| 4 | ArduinoOTA, UDP 3232 | LAN hosts; password | `ota_service.cpp` (Arduino core) | Library's MD5 scheme (accepted); framework age: SEC-03 |
| 5 | PPP/SLIP | The attached computer; Internet traffic through NAT | `internet.cpp`, `internet_network.cpp` | All wire lengths checked |
| 6 | X/Y/ZMODEM, terminal probes | The attached computer | `lib/clienttest/src/` | Buffers bounded; weak time-outs (5.5) |
| 7 | Serial ports | Local operator | `lib/atcmd`, `lib/hostport`, `rs232.cpp`, `usbport.cpp` | Input bounded. Blocking writes: STA-01 |
| 8 | Stored data | NVS blobs, LittleFS files, uploaded images, WAV files | `lib/modemcfg`, `lib/netcfg`, `wav.cpp`, `image_check.cpp` | Validated with CRC and ranges; STA-05, STA-08 |
| 9 | Radio and network strings | SSIDs from a scan, DNS and NTP replies | `wifi_radio.cpp`, `lib/netcfg` | SSIDs printed raw: SEC-04 |
| 10 | Build and release | Downloads at build time, GitHub Actions | `scripts/`, `.github/workflows/` | Downloads hash-pinned; workflow gaps: SEC-05, MNT-02 |

## 5. Findings

IDs: `SEC-` security, `STA-` stability, `PERF-` performance, `MNT-` maintainability and
quality, `TG-` test gap. Effort: S = under an hour, M = half a day, L = more.

### 5.1 Security

#### SEC-01 A caller who never hangs up holds the line, the browser menu and OTA

- **Severity:** Medium. **Category:** security (availability).
- **Where:** `lib/calls/src/incoming.cpp:199-208` (ringing has no limit; `rings_` only
  saturates at 255, line 53); `lib/hw/src/tcp_socket.cpp:11-31` and `:107-134` (no TCP
  keepalive; no `SO_KEEPALIVE` anywhere in `lib/` or `src/`); `src/main.cpp:1104` and
  `:1121` (a browser can enter the menu only when the call slot is free); `:549`
  (`AT*CONFIG` refused); `:160-163`, `:1365` and `lib/devicecfg/src/devicecfg.cpp:185-187`
  (OTA listener closed while not idle); `lib/calls/src/outgoing.cpp:25-27` (`ATD` gives
  `BUSY`).
- **What is wrong:** An accepted port-23 connection claims the single call slot and rings
  every 3 s for as long as the TCP connection stays open. There is no ring limit, no idle
  limit once answered, and no keepalive to detect a peer that vanished.
- **Scenario:** Any host that can reach port 23 opens a connection and sends nothing.
  With `S0=0` the modem rings for ever (speaker, RI, AA light). While it rings, the
  browser cannot enter the configuration screen or upload firmware, OTA is closed,
  `AT*CONFIG` is refused and outgoing calls give `BUSY`. Someone at a serial terminal can
  clear it with `ATH`; someone managing the modem only from the browser cannot. The caller
  can reconnect at once. The same happens by accident when a real caller's Wi-Fi drops.
- **Decision (Jon, 11 Nov 2026): a call that has not been answered after 8 rings is
  dropped.** At the 3 s ring period that is about 24 s. It is a fixed limit, so there is
  no new `AT*` command and no parity work. Implementation notes: end it as an unanswered
  call (close the socket, release the slot, record it in the call journal, stop the ring
  sound). `S0` accepts 1-255, so a value above 8 could never answer; treat `S0` above 8
  as 8 for incoming calls and say so in `docs/COMMANDS.md` and the decisions log.
  `test_answer` must change: it now asserts that 260 rings are still ringing.
- **Proposed fix:** (a) Give up after 8 rings, as decided above. (b) Turn on
  `SO_KEEPALIVE` with short idle/interval/count values in `TcpSocket::adopt()` and
  `connectAddress()`, so a dead peer ends in `NO CARRIER` within about two minutes.
  (c) Optional: let the browser's status view reject a ringing call.
- **Effort and risk:** (b) S, low risk. (a) S to M: a behaviour change, so it needs a
  decisions-log entry, the `S0` note above and a changed test. (a) does not cover a
  caller that answers and then goes silent; (b) does, for a vanished peer only. A
  connected but silent caller is still allowed, as on a real line.

#### SEC-02 Unauthenticated requests can stall the web server task

- **Severity:** Medium (hostile LAN only; calls are not affected). **Category:** security
  (availability).
- **Where:** `lib/hw/src/web_service.cpp:340-343` (`delay(1000)` after a wrong Digest
  response); `:40-46` with `cfg.recv_wait_timeout = 3` at `:271` (the receive gate blocks
  up to 3 s waiting for the rest of a header); `:572-574` (a browser whose 16 KB output
  queue fills is disconnected).
- **What is wrong:** ESP-IDF's HTTP server handles every connection in one task. Both
  waits block that task, so no other request or WebSocket frame is served meanwhile.
- **Scenario:** A LAN host sends a wrong Digest response once a second, or opens
  connections that send half a header. An administrator's page stops updating; when its
  output queue fills it is disconnected.
- **Proposed fix:** Replace the blocking delay with a non-blocking lock-out: record the
  time of the last wrong response and answer `401` at once without checking for the next
  second. Lower `recv_wait_timeout` to 1 s.
- **Effort and risk:** S to M, low risk. `web/PROTOCOL.md` and the wrong-password e2e
  test need the same change.

#### SEC-03 The framework is end-of-life

- **Severity:** Medium. **Category:** security (dependencies).
- **Where:** `platformio.ini:23-28` pins `espressif32@6.13.0`, Arduino core 2.0.17 and
  `framework-espidf@3.40407.240606` (ESP-IDF 4.4.7).
- **What is wrong:** Espressif ended support for ESP-IDF 4.4 in July 2024 (advisory
  AR2024-008). 4.4.7 is not the last 4.4 release either: 4.4.8 was. Wi-Fi, lwIP, mbedTLS,
  the HTTP server and ArduinoOTA in this firmware get no more security fixes.
- **Scenario:** A flaw found after July 2024 in the Wi-Fi stack or lwIP stays in every
  release of this firmware. Which published advisories apply to this build **needs
  confirmation** (compare Espressif's advisory list with the components in use).
- **Recommended target: Arduino core 3.3.x on ESP-IDF 5.5.x** (3.3.12 on 5.5.5 is the
  current pioarduino stable release), as its own roadmap version, e.g. `v2.1`. Not
  4.4.8: it is the last release of a branch that is already end-of-life, and it would
  mean a full bench run for a few fixes. Not ESP-IDF 6.x: Arduino 4.0 is still a
  release candidate. ESP-IDF 5.4 is already in its maintenance period. ESP-IDF 5.5 is
  supported to about January 2028 (sources disagree on the exact date), which is the
  longest runway a released Arduino core offers today.
- **Pin it exactly.** The pioarduino "stable" URL is a moving target. Pin a release tag
  and verify it, as `network_dependencies.py` does for the other downloads.
- **Open question that decides the plan.** This project builds Arduino as an ESP-IDF
  component (`framework = arduino, espidf`) because it needs its own sdkconfig: NAPT for
  internet dial-up, TinyUSB for the USB modem port, core dumps to flash. Neither page I
  could read says whether pioarduino or the official platform (which added Arduino 3
  support on 19 Oct 2026; its platform version and `platformio.ini` syntax are not
  stated on the page I read) supports that combination on the S3. If neither does, the
  route is Espressif's documented "Arduino as an ESP-IDF component" with a plain
  ESP-IDF CMake build, which replaces PlatformIO for the firmware build and touches CI,
  the VS Code tasks and the upload scripts. **Do a one- or two-day spike first:** build
  the unchanged tree against 5.5 and list the errors.
- **What changes in the code** (found by searching the tree; whether each compiles on
  5.5 is for the spike to confirm):
  - mbedTLS: `lib/hw/src/web_service.cpp:187`, `:776`, `:790`, `:798` and
    `lib/hw/src/device_crypto.cpp:57` use `mbedtls_md5_ret` and
    `mbedtls_sha256_*_ret`. ESP-IDF 5 ships mbedTLS 3, which dropped the `_ret`
    suffix. Five mechanical edits.
  - RMT: `lib/hw/src/hw.cpp:446-461` (autobaud capture) uses the legacy RMT driver, and
    `lib/hw/src/wifi_radio.cpp:290` (`neopixelWrite`) uses the Arduino core's RMT. In
    Arduino 3 the core uses the new RMT driver; ESP-IDF 5 refuses to run the old and
    new RMT drivers in one program. This is the most likely real obstacle: the
    capture needs rewriting for the new RMT receive API, which changes the autobaud
    code path. Everything on the RS232 port's speed detection depends on it, and
    `lib/hostport/src/autobaud.cpp` and its tests are unaffected only if the run
    format stays the same.
  - I2S: `lib/hw/src/hw.cpp:67-93`, `:195` use the legacy I2S driver. It still exists in
    5.5 but is deprecated (build warnings); move to the standard-mode driver later.
  - UART: `lib/hw/src/hw.cpp:312-319` writes the UART's `id.reg_update` and
    `id.high_speed` registers directly, and `:421` reads a pad mux register. Register
    names can differ in 5.5; check, and prefer the driver call where one exists.
  - USB: `lib/hw/src/usb_port.cpp` uses `tud_cdc_n_*` (TinyUSB's own API, stable). The
    way TinyUSB gets into the build does not carry over: `scripts/network_dependencies.py`
    and `scripts/arduino_tinyusb/CMakeLists.txt` replace Arduino 2.0.17's component and
    the `CONFIG_TINYUSB_*` keys in `sdkconfig.defaults`. Arduino 3 uses the managed
    `esp_tinyusb` component, so this mechanism, and its pinned hashes, are rewritten.
    The pinned `esp_littlefs` and `littlefs` downloads are probably replaced by the
    core's own LittleFS.
  - Smaller: `esp_core_dump_get_summary` (`src/main.cpp:690`), `WiFi.onEvent`
    (`wifi_radio.cpp:101`) and the Arduino Wi-Fi class, whose internals changed in 3.x;
    `httpd_*` calls, whose comment about a fixed 1 KiB header buffer
    (`web_service.cpp:64`) is true of 4.4 only and must be re-tested.
- **What does not change.** All logic in `lib/` that has no hardware calls, which is
  most of the code, and its 402 PC unit tests. NVS contents and the settings blobs
  (same layout). The web page and protocol. The AT command set and every user-visible
  behaviour.
- **Compatibility and behaviour risks to check on the bench:**
  - Firmware size and RAM: now 1.25 MB of 4 MB and 145 KB of 327 KB internal RAM. An
    ESP-IDF 5 and Arduino 3 build is larger and uses more internal RAM; there is room,
    but measure heap low-water marks with a call, the web page and PPP running together.
  - OTA to the new firmware: the app image format is the same, but OTA does not replace
    the bootloader or partition table, so a unit upgraded over Wi-Fi keeps the old
    bootloader. Test OTA from 2.0 to the new build on a real unit, and a USB flash as
    well. The first release should say that a USB flash is the clean route.
  - LittleFS: a v2.0 image mounted by the new LittleFS code, and the web LittleFS
    update path (`web_service.cpp:163-177`, `:801-840`), need a round trip test.
  - Wi-Fi: ESP-IDF 5 changed defaults (minimum security, power save, scan behaviour).
    Check that old WPA or open networks still join, and that join, retry and the
    browser's 60-second rollback behave as before.
  - Timing: the whole serial path (autobaud, flow control, the 2 s sink waits) depends
    on driver timing. Rerun `test_rs232` and `e2e/test_rs232.py` with the adapter, and
    the PPP and transfer interop tests.
  - Tools: pioarduino and the official platform need a recent Python (3.10 to 3.14 per
    the pioarduino README); CI uses 3.12.
- **Process.** Branch `firmware/v2.1-idf5`, bump the minor version, add a decisions-log
  entry, update `credits/LICENSES.md` for the changed third-party sources, and run the
  full bench suite (on-board, end-to-end, interop) before any release. Do it after the
  fixes above, not mixed with them, so a regression can be traced to one change.
- **Effort and risk:** L (several days, mostly RMT, USB and bench time), medium to high
  risk until the spike shows which build route works.
- **Sources:** <https://documentation.espressif.com/AR2024-008%20End-of-Life%20Advisory%20for%20ESP-IDF%20v4.4%20Release%20Branch%20EN.html>,
  <https://github.com/espressif/esp-idf/releases/tag/v4.4.8>,
  <https://github.com/pioarduino/platform-espressif32>,
  <https://piolabs.com/blog/press-releases/esp32-arduino-v3-ai-development.html>,
  <https://docs.espressif.com/projects/arduino-esp32/en/latest/esp-idf_component.html>,
  <https://github.com/espressif/esp-idf/blob/master/SUPPORT_POLICY.md>

#### SEC-04 Wi-Fi scan results are printed to the terminal unfiltered

- **Severity:** Low. **Category:** security (terminal injection).
- **Where:** `lib/hw/src/wifi_radio.cpp:249` (SSID copied as received);
  `lib/netcfg/src/net_commands.cpp:172-177` (`showScan` prints it with `%-32s`).
- **What is wrong:** An SSID is up to 32 arbitrary bytes from any access point in range.
  `validSsid()` rejects control characters only for names the user types or joins. The
  configuration screen and the web JSON are safe (both filter), the plain `AT*WIFISCAN`
  listing is not.
- **Scenario:** A neighbour names a network with escape sequences. `AT*WIFISCAN` on a
  serial terminal clears the screen, moves the cursor or draws a fake prompt; some
  terminals answer certain sequences, and the answer is typed into the modem.
- **Proposed fix:** Replace bytes below 0x20 and 0x7F with `?` in `scanResult()`.
- **Effort and risk:** S, none. Add a unit test with an escape in an SSID.

#### SEC-05 Release workflow can publish from any branch, and build steps hold a write token

- **Severity:** Medium (needs repository write access; an accident is plausible).
  **Category:** security (release process).
- **Where:** `.github/workflows/release.yml:7-13` (`workflow_dispatch` with no branch
  guard; `contents: write` for the whole job), `:23-25` (checkout keeps the token),
  `:53` (`pip install platformio`, unpinned), `:78-79` (tag at `$GITHUB_SHA`).
- **What is wrong:** A manual run on a feature branch reads that branch's `VERSION`,
  finds no tag and publishes a release tagged at an unmerged commit. After the merge the
  `main` run sees the tag and releases nothing, so the published binaries differ from
  `main`. Separately, an unpinned package installed during the build runs with a token
  that can write to the repository.
- **Proposed fix:** Add `if: github.ref == 'refs/heads/main'` to the job. Set
  `persist-credentials: false` on checkout (the publish step already gets `GH_TOKEN`).
  Pin `platformio` to a version.
- **Effort and risk:** S, none.

#### SEC-06 `AT*PASS` with no argument prints the new device password

- **Severity:** Low. **Category:** security (rule and behaviour disagree).
- **Where:** `lib/devicecfg/src/device_commands.cpp:38-47` (comment: "Deliberate one-time
  disclosure to the requesting local port only"); documented in `docs/COMMANDS.md:211`.
- **What is wrong:** CLAUDE.md says the device password is never printed except in the
  boot banner. This command prints a generated one on RS232, USB or the console (the
  browser is refused). The typed form `AT*PASS=<text>` is masked and safe.
- **Decision (Jon, 11 Nov 2026): this is permitted. The rule is amended.** New wording
  for CLAUDE.md and the decisions log: the device password is shown in the boot banner
  until first use, and once on the requesting local serial port when `AT*PASS` generates
  a new one. It is never shown to the browser, never logged, exported or sent to the web
  page, and never shown on a remote (port 23) session. The browser refusal at
  `lib/devicecfg/src/device_commands.cpp:34-37` stays. `docs/project-brief.md` is not
  edited (shared master); Jon merges the log entry back.
- **Proposed fix:** None in code. Edit CLAUDE.md and `docs/decisions-log.md` in the fix
  phase; add a native test that the browser form still refuses.
- **Effort and risk:** S, none.

#### SEC-07 Smaller hardening items (all Low)

| Where | What is wrong | Proposed fix | Effort |
| --- | --- | --- | --- |
| `lib/config/src/commands.cpp:63-78` | `AT*SETTING=` checks only the first character, so `AT*SETTING=E0DT555` dials and `E0A`/`E1H` answer or hang up. The comment says only setting commands are accepted. Local only | Accept one setting command and reject trailing letters | S |
| `scripts/pin_config.py:20`, `:40-41` | Only 0-48 is checked. GPIO 22-25 (absent), 26-37 (flash and PSRAM), 19/20 (USB modem port) and a pot outside ADC1 are accepted and build | Reject those sets | S |
| `.github/workflows/ci.yml`, `release.yml` | Actions referenced by tag, not commit; `pip install` unpinned; CI caches restored into the release build | Pin versions and commits | S |
| `scripts/network_dependencies.py:78-88` | A download is written to the cache before its SHA-256 is checked, so a bad body makes every later build fail with no hint to delete it | Verify, then write; delete on mismatch | S |
| `src/main.cpp:862-871`, `src/rs232.cpp:156`, `:185` | Read buffers that can hold a typed password are not wiped; `src/usbport.cpp:70-79` does wipe its buffer | Wipe after use | S |

### 5.2 Stability

#### STA-01 A serial port that is not being read freezes the whole modem for 2 s per result code

- **Severity:** High. **Category:** stability. Code path confirmed; one electrical point
  needs a bench check (below).
- **Where:** `src/rs232.cpp:22-44` (`UartSink::write` waits up to 2 s in `delay(1)` steps
  when the UART has no room); `src/usbport.cpp:18-37` (same for USB);
  `lib/hw/src/hw.h:62` (`kHostTxBuffer = 0`: only the 128-byte UART FIFO);
  `lib/hw/src/hw.cpp:396-410` (`&R2` makes the UART obey the computer's RTS);
  `lib/modemcfg/src/modemcfg.cpp:195`, `:201` (factory settings are `&H1 &R2`);
  `src/rs232.cpp:250-255` (flow control applies in command mode too);
  `lib/calls/src/incoming.cpp:45-63` (RING is written to all three ports, with no check
  that anyone is listening).
- **What is wrong:** Result codes are written straight to the port from the main loop.
  If the port cannot take them, the loop sleeps for up to 2 s per write and nothing else
  runs: the other ports, the web pump, sound, RI timing, Wi-Fi tick, OTA.
- **Scenario:** Factory settings, nothing plugged into RS232 (or the retro computer is
  switched off), the modem used from the USB port or the browser. With RTS off the UART
  never sends, so the first 128 bytes of results stay in the FIFO: that is 16 `RING`s.
  From then on every `RING`, every 3 s, blocks the modem for 2 s. Ring sound breaks up,
  typing on the USB port lags by seconds, the browser page freezes. With SEC-01 this
  lasts as long as the caller likes. The bytes never drain, so it happens again on every
  later call until a computer raises RTS. The USB port does the same if a program holds
  it open without reading (1024-byte buffer).
- **Needs confirmation on the bench:** that the RS232 Pal's receiver reads RTS as *off*
  with nothing connected. Test: defaults, RS232 unplugged, call the modem and let it ring
  20 times, watch `AT*STATUS` response time on the console.
- **Proposed fix:** Never wait in a sink. For unsolicited results (`RING`), drop the
  write when the port has no room. For command replies, wait once, then mark the port
  stalled and drop at once until a write succeeds.
- **Effort and risk:** M, low risk. Unit test with a sink that reports no room.

#### STA-02 An open configuration screen blocks every call, and a browser tab opens it automatically

- **Severity:** Medium. **Category:** stability (design). Needs Jon's decision.
- **Where:** `src/main.cpp:1104-1112` (the first browser to connect is put straight into
  the menu, which claims the call slot); `lib/config/src/menu.cpp:1061-1107` (`poll()` has
  time-outs only for a confirmation, 30 s, and a masked field, 120 s);
  `lib/calls/src/incoming.cpp:165-166` and `lib/calls/src/outgoing.cpp:25-27` (calls
  refused while the slot is busy).
- **What is wrong:** The README says incoming calls are turned away while the screen is
  open. Nothing ever closes an idle screen, and opening the web page counts as opening
  the screen.
- **Scenario:** The web page is left open in a tab to watch the lights. From then on
  callers are turned away and `ATD` from the retro computer gives `BUSY`, until the tab
  is closed. The same happens with `AT*CONFIG` left open on a terminal that keeps DTR on.
- **Decision (Jon, 11 Nov 2026): the browser starts read-only.** It shows the status
  view and the lights, and takes the configuration screen only when the user presses `E`
  and the modem is free (that path already exists, `src/main.cpp:1113-1131`). Remove the
  automatic claim at `src/main.cpp:1104-1112`. `web/PROTOCOL.md` already describes the
  read-only view, so the wording of "Browsers connected while busy display read-only
  status" changes to "every browser starts read-only". An idle time-out for a screen the
  user opened on purpose (`AT*CONFIG`, `E`) is not part of this decision and stays
  open.
- **Proposed fix:** As decided. Update the browser tests that expect to land in the
  menu (`test/e2e/test_web.py`, `web/test/`), and the README line "Open `http://usr-modem.local`
  ... a terminal with the same configuration screen".
- **Effort and risk:** S. A behaviour change: decisions log, `web/PROTOCOL.md`, tests.

#### STA-03 Interactive hardware tests can block the modem for ever, and one can start during a call

- **Severity:** Medium (local commands). **Category:** stability.
- **Where:** `src/hwconsole.h:16` (`connected()` is true by default; only the browser
  terminal overrides it, `src/main.cpp:225-285`); `src/hwconsole.cpp:116-126` (`waitKey`),
  `:272-282` (`ledChase`), `:284-300` (`ledWalk`), `:365-375` (endless tones);
  `src/hwconsole.cpp:319` (`AT*HWLED` with no argument starts the chase);
  `src/main.cpp:354-361` (anything starting `LED` is allowed during a call).
- **What is wrong:** A test runs inside the command handler, so the main loop stops
  until a key arrives on that port. There is no time-out and, on the three serial ports,
  no check that the terminal is still there.
- **Scenario 1:** `AT*HWTONE=DIAL` on the USB port, then the terminal program is closed.
  The tone plays for ever; port 23, the web page and the other ports are dead until the
  USB port is reopened and a key is pressed, or power is cycled.
- **Scenario 2:** A call is up on RS232. `AT*HWLED` typed on the console starts the
  chase and freezes the call until a key is pressed.
- **Proposed fix:** Give `UsbTerm` and `Rs232Term` a `connected()` that follows DTR, add
  an inactivity time-out to the "any key stops" loops, and refuse `LED`, `LED=CHASE` and
  `LED=WALK` while a call is up.
- **Effort and risk:** S to M, low risk.

#### STA-04 After a failed Wi-Fi join from the browser, later browser network changes are undone

- **Severity:** Medium. **Category:** stability.
- **Where:** `lib/netcfg/src/net_commands.cpp:277-288` (`detachCommands` forgets the
  pending join, so `clearJoin()` at `:267`/`:272` never runs);
  `lib/netcfg/src/netcfg.cpp:132-145` (the 60-second guard treats `join_ == kFailed` as
  "this change failed"); `src/main.cpp:1081-1085` (called when the browser's socket goes).
- **What is wrong:** A join from the browser always drops the browser's own connection,
  so its result is never collected. If the join failed, `join_` stays `kFailed` until the
  next join or a restart.
- **Scenario:** In the browser, join a network with the wrong password. The modem falls
  back to the old network and the page reconnects. Now change the host name or the static
  IP in the browser: the modem answers OK, then on the next tick restores the old setting
  and reconnects Wi-Fi. This repeats for every change.
- **Proposed fix:** Clear the join result when its listener is detached and the join is
  no longer running, or give the guard its own flag.
- **Effort and risk:** S, low risk. Unit test: join fails, detach, browser change, tick.

#### STA-05 `AT*FACTORY` cannot finish without a mounted filesystem, and then wipes settings at every boot

- **Severity:** Medium. **Category:** stability.
- **Where:** `lib/hw/src/file_store.h:58-62` (`erase()` returns false when LittleFS is
  not mounted); `lib/devicecfg/src/devicecfg.cpp:189-210` (the reset stops at the first
  store that fails, leaving the "reset pending" marker); `src/main.cpp:152-153` (the
  history file is fourth of eight stores), `:1183`, `:1189-1194` (the reset is retried at
  every boot).
- **What is wrong:** With no filesystem (a board that has firmware but no `uploadfs`
  yet, or a LittleFS damaged by an interrupted web update) the reset erases the modem,
  Wi-Fi and phonebook settings, fails on the history file and stays pending.
- **Scenario:** After a failed LittleFS update the user tries `AT*FACTORY`. Every boot
  from then on erases the same settings again and prints "Pending factory reset could not
  finish". The device password is unavailable, so the web interface and OTA stay off.
  Only a USB `uploadfs` gets out of it.
- **Proposed fix:** Treat "not mounted" as "nothing to erase" in `FileStore::erase()`.
- **Effort and risk:** S, low risk. Unit test with a store that reports no filesystem.

#### STA-06 Nothing restarts the modem if the main loop stops

- **Severity:** Medium. **Category:** stability.
- **Where:** `sdkconfig.defaults` (no watchdog options); generated `sdkconfig.s3:691-695`
  (task watchdog watches only the idle tasks and does not panic); no `enableLoopWDT` or
  `esp_task_wdt_*` call in `src/` or `lib/`.
- **What is wrong:** A blocked loop task (STA-03, STA-07, or a future bug) is never
  detected. The core-dump and `AT*RESETINFO` machinery is not triggered by a hang.
- **Proposed fix:** After STA-01 and STA-03 bound the known blocking paths, subscribe the
  loop task to the watchdog, feed it in `loop()` and in the few long operations (OTA,
  image staging), and enable panic on time-out.
- **Effort and risk:** M. Risk of false resets if a long operation is missed, so do it
  after the blocking paths are fixed and with a bench soak.

#### STA-07 Upload hand-shakes between the two tasks can leave stale state

- **Severity:** Low (needs the main loop to stall for about 2 s at the wrong moment).
  **Category:** stability.
- **Where:** `lib/hw/src/web_service.cpp:750-768` (the server gives up after 2 s and
  clears `uploadRequest_`), `:678-683` (`allowUpload()` sets `uploadActive_` without
  checking that anyone is still waiting), `src/main.cpp:1059-1076` and `:1299-1303`;
  second case `web_service.cpp:801-818` with `src/main.cpp:1043-1057`.
- **What is wrong:** (1) If the loop takes the upload request just before the server's
  2 s wait ends, and answers just after, `uploadActive_` becomes true with no upload
  running. Nothing clears it: `loop()` returns early on every pass until a power cycle.
  (2) If the loop sees the unmount request only after the server's 2 s wait, a failed
  upload remounts LittleFS and the late request unmounts it again.
- **Scenario:** A firmware upload starts while the loop is held up for about 2 s. The
  2 s sink wait in STA-01 is exactly that long, which makes this less unlikely than it
  looks.
- **Proposed fix:** Make both hand-shakes cancellable: the server sets a "waiting" flag
  under the mutex and clears it on time-out; `allowUpload()` and the unmount handler act
  only while it is set.
- **Effort and risk:** S, low risk.

#### STA-08 A full phonebook may not fit in NVS (needs confirmation)

- **Severity:** Low until measured. **Category:** stability.
- **Where:** `lib/calls/src/phonebook.cpp:124-144` (one blob: 9 + 64 × 83 = 5,321 bytes);
  `partitions_16mb.csv` (NVS is 0x5000 = 20 KB, about 16 KB usable); NVS writes the new
  blob before it frees the old one.
- **What is wrong:** A save needs about 10.6 KB of the 16 KB while every other setting
  lives there too. If it does not fit, edits and deletes of a nearly full book fail
  (they roll back cleanly). No test fills the book on real NVS.
- **How to confirm:** An on-board test that writes 64 entries, then edits and deletes,
  with the normal settings present.
- **Proposed fix (if it fails):** Keep the phonebook in a LittleFS file through
  `hw::FileStore`, as the call history already does. A larger NVS partition needs a full
  USB flash.
- **Effort and risk:** Test S. Fix M.

#### STA-09 Smaller stability items (all Low, confirmed)

| Where | What is wrong | Proposed fix | Effort |
| --- | --- | --- | --- |
| `lib/atcmd/src/atcmd.cpp:799-805`, `:419` | S3, S4, S5 accept 0-255. `ATS3=49` then `AT&W` makes every `1` end the line on all ports after restart (recover with `AT&F&W` followed by `1`) | Limit S3/S4/S5 to 0-127 and refuse a printable S3 | S |
| `src/main.cpp:419-441`, `lib/atcmd/src/atcmd.cpp:105-115` | `AT*BAUD=<n>` from the console or USB changes the RS232 speed while a call is up on RS232 | Refuse while another port has a call | S |
| `src/hwconsole.cpp:981-987` | `restoreBrowserResources()` runs on any browser disconnect and cancels LED, amp and signal holds set from another port | Remember who set the hold | S |
| `lib/config/src/menu.cpp:1244-1248`, `lib/atcmd/src/atcmd.cpp:510-519` | Esc during a scan, ping, DNS or play started from the screen calls `cancelPrompt()`, which only cancels masked prompts. The command carries on uncaptured. On-screen effect needs confirmation | Feed the key to the port so the wait is cancelled | S |
| `lib/modemcfg/src/modemcfg.cpp:402-406` | A failed NVS read is passed to `deserialize()` as a huge length; the modem starts on factory values and the next `&W` overwrites the stored settings | Treat a load error as a bad size | S |
| `lib/calls/src/phonebook.h:26` and `put()` | The comment says a damaged phonebook is not saved over, but the next `AT*PHONEBOOK=` edit replaces it with the factory entry plus the new one | Refuse edits while damaged, or load the good records | S |
| `lib/hw/src/client_platform.h:66`, `lib/sounds/src/catalog.h:21` | `sizes_[8]` fits today's 7 sounds + 1 exactly; an eighth sound writes past it | `static_assert` or size from `kAssetCount + 1` | S |

### 5.3 Performance

#### PERF-01 The configuration screen is repainted in full on every key

- **Severity:** Medium at low speeds (usability). **Category:** performance. The size
  figure **needs measurement**.
- **Where:** `lib/config/src/menu.cpp:255-258` (every key sets the dirty flag),
  `:794-827` (the whole frame is sent), `lib/config/src/cell_grid.cpp:120-160`.
- **What is wrong:** There is no comparison with the previous frame. A reader estimates
  2.3 to 3.5 KB for an 80 x 25 colour frame: about 3 s per key at 9600, 10 to 15 s at
  2400, over a minute at 300. The README offers 300-115200.
- **How to measure:** In `test/native/test_config`, open at 80 x 25, press one arrow key
  and print the size of the drained output; divide by (speed / 10).
- **Proposed fix:** Keep the previous cells and send only changed rows. Or state a
  minimum useful speed in the README.
- **Effort and risk:** M, medium risk (resize and abort paths).

#### PERF-02 Web status is rebuilt 10 to 20 times a second with no browser connected

- **Severity:** Low. **Category:** performance.
- **Where:** `src/main.cpp:947-1038` (`publishWebStatus()`), called on every loop pass
  from `:1169`; it also calls `tz::offsetAt()` (`lib/netcfg/src/net_tz.cpp:254`, several
  64-bit divisions) on **every** pass at `:954-955`.
- **How to measure:** Loop passes per second and heap low-water mark, web on, no browser,
  before and after.
- **Proposed fix:** Return early when no browser is connected; cache the UTC offset per
  minute.
- **Effort and risk:** S, low risk.

#### PERF-03 Smaller performance items (all Low; measure before changing)

| Where | What | How to measure |
| --- | --- | --- |
| `lib/hw/src/web_service.cpp:605`, `:617`, `:657` | Browser terminal output is capped at 512 bytes per 20 ms, about 25 kB/s per client | Time a full repaint at 132 x 50 in the browser |
| `lib/internet/src/internet.cpp:473-478` | All 1,508 bytes of the receive buffer are wiped on every frame flag, including idle flags | Time `write()` with 100 KB of `0x7E` |
| `web/src/upload.js:13-42` | The page hashes an 8 MB image with two array allocations per 64-byte block and a full padded copy | `performance.now()` around `inspectImage` |

### 5.4 Maintainability and quality

#### MNT-01 The CI "Version and change log" job can never fail

- **Severity:** Medium (a project rule is not enforced). **Category:** process.
- **Where:** `.github/workflows/ci.yml:27`:
  `run: echo "version=$(python3 scripts/check_version.py)" >> "$GITHUB_OUTPUT"`.
- **What is wrong:** The exit status of a command inside `echo "...$(...)"` is thrown
  away. `scripts/check_version.py:16-22` prints its error and returns 1; the job stays
  green and the error text becomes the "version" used in the artifact name
  (`ci.yml:125`). `release.yml:30-31` does it correctly.
- **Proposed fix:** `version=$(python3 scripts/check_version.py)` on its own line, then
  the `echo`.
- **Effort and risk:** S, none.

#### MNT-02 Two gaps in the upload and lint gates

- **Severity:** Low. **Category:** process.
- **Where and what:** (a) `scripts/pre_upload_tests.py:28-29` gates `upload` and
  `uploadfs` only; the platform also has `uploadfsota`
  (`espressif32/builder/main.py:581`), so a LittleFS upload over Wi-Fi skips the unit
  tests. (b) `ci.yml:75` passes `--fail-on-defect=medium`; PlatformIO compares exact
  labels (`platformio/check/cli.py:168-171`), so a cppcheck *error* ("high") does not
  fail the job through this option. Whether cppcheck's own exit code catches it needs
  confirmation.
- **Proposed fix:** Gate `uploadfsota` too; add `--fail-on-defect=high`.
- **Effort and risk:** S, none.

#### MNT-03 Text that still describes a volume limit

- **Severity:** Low. **Category:** documentation.
- **Where:** `README.md:16` ("a speaker with a hard volume limit"), `README.md:170`
  (`AT*HWTEST` list includes "volume limit"), `lib/config/ui-bindings.json:435` ("cap
  always applies", shown as help on the Hardware Tests screen). `README.md:132-134` and
  CLAUDE.md say there is no limit (decided 7 Nov 2026).
- **Proposed fix:** Correct the three lines and regenerate the UI data. **Effort:** S.

#### MNT-04 `src/main.cpp` and `lib/config/src/menu.cpp` carry too much in a few functions

- **Severity:** Low. **Category:** maintainability.
- **Where:** `src/main.cpp:481-704` (`ConfigHardware::command`, 220 lines, 14 commands in
  one `strcmp` chain), `:1039-1171` (`pollBrowser`, 130 lines), `:947-1038`;
  `lib/config/src/menu.cpp:1243-1456` (`Terminal::key`, 214 lines), `:352-483`
  (`buildFrame`); `lib/config/src/commands.cpp:52-214`.
- **Duplicates and dead code:** browser claim block at `main.cpp:1104-1112` and
  `:1121-1129`; client-settings block at `:1226-1234` and `:1336-1348`; terminal dispatch
  at `:316-326` and `:372-386`; `Rs232Term` and `UsbTerm` (`:234-256`) differ by one
  call; `if (allow && owner)` at `:1065` is redundant; `internetDialer->poll()` is called
  twice per pass (`:1297`, `:1334`); the file header (`:3-19`) still says "two serial
  ports".
- **Proposed fix:** Table-driven dispatch and three small helpers. Behaviour-preserving.
- **Effort and risk:** M, low risk, but only the bench tests cover `src/`.

#### MNT-05 Smaller quality items (all Low, confirmed)

| Where | What is wrong | Proposed fix |
| --- | --- | --- |
| `src/main.cpp:1156-1161` | "Dialling on the RS232 host port." is printed before checking the port can take the dial; otherwise the dial is silently dropped | Check first; report "busy" |
| `web/src/demo.js:29-33`, `:39` | Box-drawing characters were replaced by literal `?` (the file has no non-ASCII bytes), so the demo draws `?????` borders | Restore them as `\u` escapes |
| `.gitignore` | The untracked `build/` folder (an IDE CMake configure of the root `CMakeLists.txt`) is not ignored | Add `/build/` |
| `CLAUDE.md:98` | Says `espressif32@^6.9.0`; `platformio.ini:23` pins `6.13.0` | Update the note |
| `lib/clienttest/src/transfer.h:1` | Zimodem-derived, but the header names no licence and no "changed" mark (the `.cpp` has both) | Add "Apache-2.0, changed for USR Modem" |
| `web/PROTOCOL.md` | Three small mismatches with the code: `wifi` is republished every 10 s in every state, not only while up; `npm test` runs five suites, not only the status bar; statuses 404 and 503 are not listed | Update the document |
| `lib/modemcfg/src/modemcfg.cpp:19`, `lib/config/src/settings_help.cpp:31` | Tables sized by hand against `kSettingCount` with no compile-time check; a new setting without its row is a null-pointer crash | `static_assert` on the row count |
| `scripts/build_web.py:24-38`, `lib/hw/src/web_service.cpp:379-391`, `web/src/app.js:11-15` | The list of web files is kept in three places with no test that they agree | One pytest that parses all three |
| `src/hwconsole.h:1-2`, `lib/hostport/src/connection.h:1-3`, `lib/atcmd/src/atcmd.cpp:764` | Comments describe v0.5 to v0.8 behaviour that has changed | Update |
| cppcheck notes | `src/hwconsole.cpp:895` (index before bound) and `lib/config/src/menu.cpp:449` (duplicate condition) are harmless but easy to tidy; the other 410 are style notes, mostly in tests | Reorder two lines; leave the rest |

### 5.5 Reader findings not re-verified by the lead (all Low; treat as needing confirmation)

| Where | Reported problem |
| --- | --- |
| `lib/hw/src/wifi_radio.cpp:204-207`, `lib/netcfg/src/netcfg.cpp:179-185` | After `AT*WIFISCAN` with Wi-Fi switched off, the radio stays on |
| `lib/hw/src/wifi_radio.cpp:24-25`, `:58-81` | Link state is shared between the Wi-Fi event task and the loop with `volatile` only; a late event can overwrite `leave()` |
| `lib/hw/src/wifi_radio.cpp:208-224` | Starting a second scan while one runs can end the new scan early with a short list |
| `lib/netcfg/src/netcfg.cpp:314-358` | `AT*WIFI=OFF` or `AT*WIFIFORGET` during the browser's 60-second window saves the unverified change |
| `lib/netcfg/src/netcfg.cpp:432-453`, `:535-557` | Wi-Fi join, scan or IP change from another port drops a call in progress without warning |
| `lib/hw/src/file_store.h:21-57` | Power loss between the two renames loses the newest history save; a primary file with a bad inner CRC does not fall back to the backup |
| `lib/hw/src/net_probe.cpp:94-98` | DNS time counts against the 10 s ping limit; a late time-out reports failure although replies arrived |
| `lib/hw/src/internet_network.cpp:77`, `:122` | Two 1.5 KB packet copies on the lwIP task's 6 KB stack; measure the high-water mark |
| `lib/internet/src/internet.cpp:386-393` | An IPCP Terminate-Request resets the 30 s negotiation time-out even before PPP is up |
| `lib/clienttest/src/transfer.cpp:92-98`, `:168-170`, `:629-632` | Any received byte resets the transfer inactivity timer (only the 30-minute cap bounds a babbling peer); a missing "OO" after ZFIN ends in a time-out |
| `lib/clienttest/src/client_service.cpp:304-307` | The size probe only looks at the first `ESC[` in its buffer, so an echoed query hides the real reply |
| `lib/calls/src/outgoing.cpp:259-263` | The S7 limit keeps running during the handshake sound; a short S7 can drop an open connection |
| `src/rs232.cpp:215-218`, `:257-263` | `captureBegin()` failure is ignored (port deaf in AUTO speed); `runs` is used unchecked if both allocations fail |
| `lib/config/src/menu.cpp:259-261`, `:1194-1198`, `:1294-1306` | Paging step does not match tall screens; Alt+letter cancels a form then acts as a hotkey; the "input too long" flag survives Backspace |
| `lib/netcfg/src/net_settings.cpp` | `validPassword` rejects UTF-8 passphrases; `validServer` accepts `a..b` |
| `lib/webinterface/src/web_policy.cpp:268-272` | Non-ASCII SSIDs reach the page as mojibake (safe, but wrong) |
| `web/src/app.js:267-303`, `web/src/link.js:57-61` | Upload button can be pressed twice during the preflight; other tabs reconnect in a loop during an upload; the page does not reload itself after an update |
| `scripts/ota_upload.py:13-16`, `bump_version.py:50-51`, `generate_ui.py:20` | A UTF-16 or BOM `.env` breaks the build; `bump_version` writes CRLF on Windows; the hotkey check is a substring test |
| `lib/devicecfg`, `lib/hw/src/wifi_radio.cpp:100` | `AT*FACTORY` does not remove Wi-Fi credentials the ESP-IDF driver may have stored under a pre-v0.6 firmware |

## 6. Checked and found sound

- **Remote callers cannot reach commands.** Bytes from a call go only through
  `Session::toHost()` (`lib/hostport/src/session.cpp:202-226`); `Port::feed()` and the
  `+++` detector are called only from `fromHost()` (`:142-181`). `AT*HW...` is reachable
  only from a port in command mode. Three readers traced this independently.
- **Telnet parser.** Subnegotiation buffer bounded with an overflow flag
  (`lib/calls/src/telnet.cpp:116-137`); option replies cannot loop (`:49-79`); replies
  cannot overflow the send queue because reads are limited to 1/16 of its free space
  (`lib/calls/src/tcp_connection.cpp:53-57`).
- **PPP/SLIP.** Frame store bounded before each byte, FCS checked before parsing, every
  option loop rejects length < 2 or past the end, IP header validated before forwarding
  (`lib/internet/src/internet.cpp:44-54`, `:201`, `:281`, `:336`, `:407`, `:496-499`).
- **X/Y/ZMODEM.** Sub-packet store bounded, offsets checked against file size, totals
  capped, silent peer times out (`lib/clienttest/src/transfer.cpp:142-150`, `:527`,
  `:579`, `:607`).
- **AT parser.** Line buffer and numeric arguments bounded; masked prompts and
  `AT*PASS=` cannot be replayed by `A/`; buffers are wiped
  (`lib/atcmd/src/atcmd.cpp:31-43`, `:418-441`, `:550-602`).
- **Secrets.** No path prints, logs or exports the Wi-Fi password. `AT*EXPORT` contains
  no password or hash. The debug log holds event codes only. The device password
  generator uses rejection sampling over 56 symbols, 12 characters, seeded from the
  boot-time hardware entropy source (`lib/devicecfg/src/devicecfg.cpp:101-114`,
  `lib/hw/src/device_crypto.cpp:30-43`). The one exception is SEC-06.
- **Web.** Static files come from a fixed list, so no path traversal; Host and Origin
  must match the modem; duplicate security headers, `Transfer-Encoding`, oversized headers
  and bytes beyond `Content-Length` are refused before ESP-IDF parses the request; Digest
  comparison is constant-time; nonces, tickets and CSRF tokens are 128 random bits; JSON
  strings escape every non-printable byte (`lib/webinterface/src/web_policy.cpp:134-300`,
  `lib/hw/src/web_service.cpp:375-417`). The page has one `innerHTML`, fed by constants;
  everything else is `textContent`. `web/PROTOCOL.md` was compared with the code
  statement by statement and matches except for the three small points in MNT-05.
- **Upload.** Length, target, version and hash format are checked before any write; the
  image's identity and SHA-256 are checked before it is activated; LittleFS is staged and
  verified before the live filesystem is touched
  (`lib/hw/src/web_service.cpp:709-856`, `lib/webinterface/src/image_check.cpp`).
- **Stored data.** Every blob has magic, version, exact size and CRC, then field checks
  (`lib/modemcfg/src/modemcfg.cpp:284-381`, `lib/netcfg/src/net_settings.cpp:30-125`).
  WAV headers are fully bounded (`lib/sounds/src/wav.cpp:14-62`).
- **Project rules.** GPIO numbers appear only in `lib/hw/src/pins.h`. Every I2S write
  goes through `volume::outputGain()` (`lib/hw/src/hw.cpp:190`, `:229`). Every item on
  the configuration screen has an `AT*` command, and every registered command is in
  `docs/COMMANDS.md`.
- **Build.** Downloads are HTTPS with pinned SHA-256; the OTA password is never on a
  command line or in a log; the test gate fails closed; the partition table adds up to
  exactly 16 MB.

## 7. Test gaps

The suites are large and mostly sound: all 402 unit-test functions are registered, the
fakes are simple, and clock wrap-around, storage failure and masking are tested in many
places. The gaps below are where a protection could be removed without any test
failing, or where the bench harness can hurt itself. "Priority" uses the same scale as
severity. TG-01, TG-02, TG-07 and TG-09 to TG-11 were checked by the lead; the others
are reader findings.

| ID | Priority | Where | Gap | Proposed fix |
| --- | --- | --- | --- | --- |
| TG-01 | High | `test/native/test_answer/test_main.cpp:362-388` | The test named for "remote `+++` is data" sends the remote `+++` 30 ms after data, so there is no guard time, and its final check is made after a *local* escape. It would pass if remote bytes reached the escape detector. The e2e version covers only the console as answer port | Remote silence 1.2 s, `+++`, silence 1.2 s, assert still online; repeat for RS232 and USB |
| TG-02 | High | `test/native/test_config_services/test_main.cpp:134-194` | The "export has no secrets" test never stores a Wi-Fi password, and checks command names only. No test sets both passwords and then searches every output (export, status, logs, every screen) for them and their hashes. The Digest hash is searched nowhere | One "canary sweep" test over every registered command and screen |
| TG-03 | High | `lib/internet/src/internet.cpp:201`, `:281`, `:336`, `:441`; `lib/clienttest/src/transfer.cpp:184-212`, `:331-347`, `:527`, `:543-549` | No test feeds malformed PPP control packets (bad lengths, option length 0 or 1) or hostile X/Y/ZMODEM input (bad block numbers, YMODEM block 0, bad headers, cancel sequences). The guards read correctly today but could be deleted with every test passing. The X-1K "wrap" test sends 33 blocks, so it never wraps | Table-driven hostile-input tests fed straight into the engines |
| TG-04 | Medium | `lib/calls/src/telnet.cpp:49-79`, `lib/calls/src/tcp_connection.cpp:53-57` | Only a `WILL 99` storm is tested. No Telnet IAC from an answer-mode caller in the unit suite; the send-queue guard can be removed with no failure | Storm and full-queue cases; IAC from a caller before and after `ATA` |
| TG-05 | Medium | `lib/webinterface/src/web_policy.cpp:52-111`, `:306-352`; `image_check.cpp` | Authorization parser guards (unterminated quote, more than 12 parameters, 900-byte limit), nonce eviction after 8 challenges and a nonce count going backwards are untested; the fake MD5 is a constant. `ImageCheck(true, ...)` (LittleFS path, exact size) is never constructed | Add the cases; use a real MD5 vector once |
| TG-06 | Medium | `lib/hostport/src/linectl.cpp:19-65`, `lib/hostport/src/session.cpp:228-242` | `+++` guard time, DTR hold and throttle credit are never tested across the 2^32 ms wrap; throttle only at 300 bps; S3/S5 edge values | Start the fake clock near `0xFFFFFF00` |
| TG-07 | Medium | `test/native/test_pins/test_main.cpp:19-21` | The pin lock test is skipped when `.env` overrides any pin, and the upload gate still passes. It was skipped in this baseline, so on this PC the lock is not protecting uploads | Fail (not skip) unless an explicit "pins overridden on purpose" flag is set |
| TG-08 | Medium | (new tests) | No test exists for the conditions behind STA-01 (sink with no room), STA-04 (stale join), STA-05 (reset without a filesystem), SEC-04 (escape in an SSID) or SEC-01 (the ring test records 260 rings as expected) | Add with each fix |
| TG-09 | High | `test/e2e/conftest.py:136`, `:151`; `test/e2e/test_rs232.py:137`; `test/interop/serial_pppd.py:75` | Serial ports are opened with RTS/CTS and no write time-out, so a silent modem makes the run **hang** instead of fail, including in teardown. `test_rs232.py:137` sets `write_timeout = 0` and never restores it | `write_timeout=5` everywhere; add `pytest-timeout` |
| TG-10 | High if masking ever breaks | `test/e2e/test_wifi.py:71-72`; `test/e2e/test_device.py:201` | `assert echoed == b"*" * len(password)` makes pytest print `echoed` on failure, which would be the Wi-Fi password in clear. Other asserts print the boot banner, which can hold the first-boot device password | Compute a boolean first, then assert on it (the file already does this elsewhere) |
| TG-11 | Medium | `test/embedded/test_board/test_main.cpp:39-67` | The on-board test uses the production settings store, erases it and restores it only at the end; a failed assertion in between leaves test data in the modem. Every other suite uses a scratch namespace | Use a scratch namespace |
| TG-12 | Medium | `test/e2e/test_hwtest.py:95-110`, `test/e2e/test_wifi.py:64-76`, `test/e2e/test_device.py:45-68` | State left behind on failure: bridge test without `try/finally`; a wrong `.env` Wi-Fi password replaces the saved network; `--device-tests` without `--save-password` can set a random device password nobody knows | `try/finally`; refuse `--device-tests` without a known password |
| TG-13 | Medium | `test/e2e/test_calls.py:262`, `test/e2e/test_internet.py:303-336`, `:139` | A default run dials phonebook entry 1 (a real BBS) and needs the Internet. `test_internet.py:139` looks for `NO DIALTONE`; the modem prints `NO DIAL TONE` | Use a PC-local number; fix the string |
| TG-14 | Medium | `web/src/app.js`; `web/test/upload.test.mjs:11-20` | `app.js` (message handling, the whole upload flow) has no tests. The test titled "conflicting identity" never builds a conflict | Move the upload flow into a testable function; add the case |
| TG-15 | Medium | `web/PROTOCOL.md` | No test at any level for: the three-browser limit, connection floods on ports 23 and 80, an interrupted upload, the wrong-password delay end to end, USB unplug during a call | Add e2e cases behind flags |
| TG-16 | Low | `test/native/*/test_main.cpp` | Fakes are copied between suites and have drifted (`FakeSound::fail` is never set in `test_calls`; several fake stores cannot fail), which hides whole classes of failure path | One shared fixture header |
| TG-17 | Low | `test/native/test_hwtest/test_main.cpp:176-183`, `:279-291`; `test/embedded/test_rs232` | Two assertions cannot fail (they test values that are clamped by definition). All nine on-board RS232 tests are ignored on the fitted board | Assert on the unclamped value; correct the matrix in `test/README.md` |

## 8. Prioritised summary

### Fix now

Small, low-risk changes with a clear benefit. Each needs a unit test and one version bump
for the branch.

1. **STA-01** Stop sinks blocking the loop (after the 5-minute bench check). This is the
   only High finding.
2. **STA-05** Let `AT*FACTORY` finish without a filesystem.
3. **STA-04** Clear the stale join result.
4. **STA-03** Bound the interactive hardware tests; refuse the LED chase during a call.
5. **SEC-01** The 8-ring limit and TCP keepalive on call sockets.
6. **STA-07** Make the upload hand-shakes cancellable.
7. **SEC-04** Filter control bytes in scan results.
8. **MNT-01, MNT-02, SEC-05** One-line workflow and gate fixes.
9. **TG-01, TG-02** The two tests that guard the project's main security rules, and
   **TG-09, TG-10, TG-11** so the bench harness cannot hang, leak a password or wipe
   settings.
10. **MNT-03, MNT-05, STA-09** Text, comments and one-line guards.

### Decisions made (Jon, 11 Nov 2026)

- **SEC-01** Drop an unanswered call after 8 rings (fixed; `S0` above 8 counts as 8).
- **STA-02** Browsers start read-only; `E` takes the configuration screen.
- **SEC-06** `AT*PASS` may print a generated password on the local port; amend the rule
  in CLAUDE.md and the decisions log.
- **SEC-03** Target Arduino core 3.3.x on ESP-IDF 5.5.x as `v2.1`, after a spike (see
  SEC-03). Still open: an idle time-out for a configuration screen the user opened on
  purpose.

### Fix later

- **STA-06** Loop watchdog, once STA-01 and STA-03 have removed the known long waits.
- **SEC-02** Non-blocking wrong-password lock-out.
- **SEC-03** Framework move, as its own roadmap version with a full bench run.
- **PERF-01** Measure the frame size first; then row-level repaint or a stated minimum
  speed. **PERF-02** with it.
- **STA-08** On-board test with a full phonebook; move it to LittleFS only if it fails.
- **TG-03 to TG-08, TG-12 to TG-17** Hostile-input tests, shared fixtures, harness
  clean-up.
- **MNT-04** Break up the long functions in `main.cpp` and `menu.cpp`, after the tests
  above exist.
- **Section 5.5** Confirm each, then fix the ones that are real.

### Leave alone, and why

- **Digest MD5 over plain HTTP, and ArduinoOTA's MD5 scheme.** Deliberate for a LAN
  device with no certificate. The code around them (nonces, Host and Origin checks,
  tickets, CSRF) is sound.
- **Password hashes stored unsalted in NVS.** Digest and ArduinoOTA both require this
  form. Reading them needs physical access to the flash.
- **The image identity marker is not a signature.** Documented as such; a signature
  would need key handling that does not fit this project.
- **No flash encryption or secure boot.** They would block USB recovery, which the
  design relies on.
- **Nonce table of 8 and strict nonce-count order.** At worst a silent retry.
- **PPP with no idle time-out, the 63-character host limit, Telnet without CR-NUL
  handling.** Design choices that match the Sportster or Zimodem.
- **The 410 cppcheck style notes.** No defects among them; most are in test code.
- **PERF-03 items.** Not worth changing unless a measurement says so.
- **Vendored xterm.js 6.0.0.** Version and licence are recorded; nothing to change.

## 9. Files covered

Every tracked file in scope was assigned to a reader, by whole directory (checked
against `git ls-files`; the `lib/` units add up to all 125 tracked files in `lib/`).

| Unit | Files | Tracked files |
| --- | --- | --- |
| A | `src/` | 8 |
| B | `lib/hw/` | 29 |
| C | `lib/webinterface/`, `web/src/` (and `web/PROTOCOL.md` against the code) | 15 |
| D | `lib/atcmd/`, `lib/dialstring/`, `lib/modemcfg/`, `lib/hostport/` | 24 |
| E | `lib/calls/`, `lib/internet/`, `lib/clienttest/` | 26 |
| F1 | `lib/config/` | 17 |
| F2 | `lib/netcfg/`, `lib/devicecfg/`, `lib/hwtest/`, `lib/sounds/` | 23 |
| G1 | `scripts/`, `.github/workflows/`, `ui/tools/`, `ui/tests/`, `test/scripts/`, build configuration in the repo root | 32 + root files |
| G2 | `test/native/` (two readers) | 17 |
| G3 | `test/embedded/`, `test/e2e/`, `test/interop/`, `web/test/` | 43 |

Exceptions, as stated in section 1: `lib/config/ui-bindings.json` and
`web/src/ui-data.js` (data, searched only), the two generated headers, and `web/vendor/`.
At the end of the review `git status` shows only `REVIEW.md` and the existing `build/`
folder as untracked, and no modified files.
