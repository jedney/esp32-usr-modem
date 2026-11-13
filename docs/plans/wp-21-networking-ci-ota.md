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
5. Full end checks (README.md step 7).
