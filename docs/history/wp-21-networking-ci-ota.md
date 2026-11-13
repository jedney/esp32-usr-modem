# WP-21 (v3.0): Networking, tooling, CI and the OTA hash

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high |
| Size | L |
| Depends on | WP-20 |

Read [wp-19-common.md](wp-19-common.md) first (decisions, versions, board testing, rules).

## Modernisation review rows

| Area | Now | Look at |
| --- | --- | --- |
| PPP, SLIP, NAPT | raw lwIP (`netif`, `tcpip`, `lwip_napt.h`) | whether `esp_netif` (PPP) or Arduino 3.3's `Network`/`PPP` classes fit a DCE-side server; SLIP stays raw lwIP if nothing covers it |
| Wi-Fi | `WiFi.onEvent`, `esp_wifi.h` | Arduino 3.3 `Network` events; direct `esp_wifi` calls only where Arduino has no API |
| OTA | `ArduinoOTA`, `Update`, `esp_ota_ops` | SHA-256 auth (decision 3); otherwise check |

## Steps

0. **Modernisation review** (rows above). PPP and the Wi-Fi events are the likely
   rewrites. Decide from the current docs whether `esp_netif` or Arduino 3.3's `Network`
   classes can serve PPP on the DCE side (the modem is the server, the host computer the
   client). If they can, move to them; if not, record why raw lwIP stays.
   `e2e/test_internet.py` must pass unchanged on the bench. If the PPP rewrite is large,
   it becomes its own session after this one (renumber the rest).
1. **CI** (`ci.yml`, `release.yml`): cache keys for the new packages; Python, Node,
   pytest and action versions (table in the common file); `managed_components/` (including
   the new `espressif__tinyusb` download); the release still publishes bootloader,
   partitions, app and LittleFS images (IDF 6.1 paths).
   - The native tests build as `gnu++26` (Jon, 13 Nov), so the runner needs g++ 14 or
     newer (`ubuntu-latest` has 13). The interop peers in `ci.yml` and
     `test/interop/README.md` (`-std=c++17`) move to the same standard.
   - `dependencies.lock` is written with CRLF on Windows (git converts it); CI will write LF.
   - `syncedAt` (decisions log, "found in session 1") is still open.
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
5. **Web server (found in WP-20):** the modem resets every browser WebSocket at once, so
   12 of the 18 `e2e/test_web.py` cases fail (all `/ws` cases and the wrong-password
   lock-out); plain HTTP works and nothing is logged. Start from `web_service.cpp`'s
   receive override and header gate (written for IDF 4.4's 1 KiB header buffer) and IDF
   6.1's `esp_http_server` (WebSocket upgrade, `httpd_sess_set_recv_override`); check
   whether IDF 6.1 still needs the header gate at all. `test_web.py` must pass with
   `--web-tests`.
6. Full end checks (README.md step 7).

## Result (13 Nov 2026)

Done. Reasons and details are in the decisions log (rows "v3.0 networking review", "OTA
password hash = SHA-256", "OTA uploader", "web server on IDF 6.1", "found in WP-21,
follow-up", "last NTP sync time", "CI and tools", "VS Code tasks and .gitignore", "e2e
lock-out test").

- **Review:** PPP, SLIP and NAPT stay on raw lwIP (Arduino's `PPP` is a DTE-side
  `esp_modem` client; `esp_netif` has no SLIP and its PPP server would replace the tested
  engine). Wi-Fi events through `Network.onEvent()`; `esp_wifi_scan_stop()` stays (no
  Arduino call). ArduinoOTA and `Update` stay.
- **OTA hash:** SHA-256, 142-byte device record (layout 1). Uploader rewritten
  (`scripts/ota_upload.py`, SHA-256 sign-in only, fake-modem tests).
- **Web server:** the resets came from IDF 6.1 no longer calling the WebSocket handler for
  the handshake. Upgrade checks now in `ws_pre_handshake_cb`; the header gate passes bytes
  through (server header limit 4 KiB); each HTTP response closes its connection;
  `TCP_NODELAY`. An IDF 6.1 race in the queued LRU purge is a follow-up (flood case only).
- **CI:** Python 3.14 (3.15 lacks a `pydantic_core` wheel), Node 26, g++ 14, component
  cache and lock check, exact e2e pins. Not run in CI yet (one push after WP-26).
- **Tools:** VS Code e2e task fixed (Python PATH); Node is not installed on the PC (web
  task needs it). `.gitignore` cause found (pioarduino's generator); guard test added.
- **Board (full erase, USB flash, LittleFS, setup from `.env`):** e2e `test_web.py` 23/23
  (with `--rs232-port`, floods, uploads); `test_device.py --device-tests --factory-reset
  --ota-upload` 7/7 (real OTA uploads); `test_internet`, `test_wifi`, `test_console`,
  `test_rs232`, `test_settings`, `test_session`, `test_hwtest`, `test_config`,
  `test_client`, `test_answer` 135 passed (2 optional UDP-echo skips). On-board
  `test_device` 8/8 and `test_calls` 5/5. App image 1,495,193 bytes.
- **Local checks:** native 488 (1 skip: pins overridden by the bench `.env`), cppcheck,
  clang-format, `test/scripts` 64, version, image manifest.
