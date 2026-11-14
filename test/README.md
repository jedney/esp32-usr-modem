# Test plan

Every modem feature has tests, at these levels:

| Level | Where it runs | Command | When |
|---|---|---|---|
| **Unit** (`test/native/`) | PC, no board | `pio test -e native` | Every push (CI) and before every upload |
| **Script** (`test/scripts/`, `ui/tests/`) | PC | `python -m pytest test/scripts`; `python -m unittest discover -s ui/tests` | Every push (CI) |
| **Web unit** (`web/test/`) | PC, Node.js 20+ | `cd web; npm test` | Every push (CI) |
| **On-board** (`test/embedded/`) | ESP32-S3 | `pio test -e s3` | Before each release |
| **End-to-end** (`test/e2e/`) | PC driving the modem's serial ports and Wi-Fi | `python -m pytest test/e2e --console-port COMx [--rs232-port COMy] [--usb-port COMz]` (`--console-port socket://usr-modem.local:2001` uses the network console; `--net-console <host>` runs `test_net_console.py`) | Before each release |
| **Interoperability** (`test/interop/`) | PC with lrzsz / pppd | see `test/interop/README.md` | Before releases that touch transfers or PPP |

Shared fakes for the unit tests live in `test/native/support/fakes.h`: `fakes::MemoryStore` (`failSave`, `failLoad`, `failErase`, `failNextSaves`, counters), `fakes::TextSink` and `fakes::RecordingSound` (`fail`, `broken`). A suite includes it as `"../support/fakes.h"` after `#define FAKES_STORE`, `FAKES_SINK` and/or `FAKES_SOUND`. Suites still on their own copies: the `MemStore` in `test_atcmd`, `test_hostport`, `test_netcfg` and `test_modemcfg`, the `Memory` in `test_devicecfg`, the `Store` in `test_web`, the radios in `test_netcfg`, `test_config_services` and `test_internet`, and the sockets in `test_answer` and `test_calls`.

Feature logic lives in `lib/` with no hardware calls, so most of it is unit-tested on the PC. On-board tests cover the hardware layer (`lib/hw`); end-to-end tests cover behaviour as a user sees it. Running the on-board tests replaces the firmware with a test program: upload the firmware again afterwards. End-to-end options are in `test/e2e/README.md`.

On the fitted board the RS232 receivers drive the UART pins, so the internal-loopback cases in `embedded/test_rs232` skip; `e2e/test_rs232.py` covers the port through the Unitek adapter instead. All nine `embedded/test_rs232` cases are ignored on the fitted board (no loopback plug), so the `test_rs232` entries in the matrix below mean "present, but skipped on the bench". `embedded/test_board` writes only the scratch namespace `test_board`, and keeps the watchdog fed for 17 s (a failure resets the board). `embedded/test_calls` writes its full phonebook to scratch namespaces (`scratch_book`, `scratch_o0`-`scratch_o7`) and removes them.

## Feature matrix

Section numbers refer to the firmware spec (`docs/project-brief.md`).

| Feature (spec §) | Unit | On-board | End-to-end |
|---|---|---|---|
| Board: 16 MB flash, 8 MB PSRAM, partitions (2.1); task watchdog set to panic after 15 s and kept fed past it | — | `test_board` | — |
| Pin map, no reserved or shared pins, pot on ADC1 (2.5); numbers come from `.env.example`; the lock test fails if `.env` overrides them unless `USR_MODEM_PINS_OVERRIDDEN=1`; script rules reject absent, flash/PSRAM and USB GPIOs | `test_pins`; `test/scripts/test_pin_rules.py` | — | `test_hwtest.py` |
| Dial strings: IP + port, host names, phonebook, special numbers, invalid → ERROR (3.3) | `test_dialstring` | `test_board` | `test_console.py` |
| Version in banner and `AT*VER` (3.9) | — | — | `test_console.py` |
| AT parser: several commands per line, `A/`, echo, S3/S5, S-registers, `&F &W &V &Z`, `Y`, `Z` (3.2, 3.6) | `test_atcmd` | — | `test_settings.py` |
| Result codes, X0-X4, verbal/numeric/quiet, when answering (3.5) | `test_atcmd`, `test_answer` | — | `test_settings.py`, `test_calls.py`, `test_answer.py` |
| `ATI0`-`ATI7`, `+FCLASS` (3.6) | `test_atcmd` | — | `test_settings.py` |
| Settings kept across a restart, `&F0`-`&F2`, damaged or unreadable settings → factory; S3/S4/S5 limited to 0-127 and S3 never printable (3.7) | `test_modemcfg`, `test_atcmd` | `test_board` | `test_settings.py`, `test_session.py` |
| RS232 speed from "AT" at 300-115200, fixed with `AT*BAUD` (3.2) | `test_hostport` (`CallSlot::heldBy` for the "refused during an RS232 call" rule) | `test_rs232` | `test_rs232.py`, `test_session.py` |
| RTS/CTS and XON/XOFF, DTR `&D` with S25, DCD `&C`, RI (3.2) | `test_hostport` | `test_rs232` | `test_rs232.py`, `test_answer.py` |
| A port nobody reads never blocks the modem: `RING` dropped at once, a reply waits once then the port is stalled | `test_hostport` (`test_sink_gate_*`), `test_answer` | — | `test_rs232.py`, `test_usb.py` (bench) |
| `+++` guard time, DTR hold and throttle credit across the 2^32 ms clock wrap, S3/S5 at 0 and 127, `ATO`, `ATH`, `ATZ`, `F0`, `AT*TESTCALL` (3.2) | `test_hostport`, `test_atcmd` | — | `test_session.py`, `test_rs232.py` |
| `&N` line speed, `AT*MODEL`, throttling at 300, 2400 and 115200 bps, CONNECT text; a sound that cannot start or breaks ends the call at every stage (3.2) | `test_calls`, `test_hostport`, `test_atcmd` | `test_calls` | `test_session.py`, `test_calls.py` |
| Phonebook; damaged book refuses edits until `AT*BOOKCLEAR` (3.3); a full book of 64 longest entries saves, changes and shrinks in NVS beside the other settings | `test_calls` | `test_calls` | `test_calls.py` |
| Outgoing calls: Telnet negotiation, 0xFF escaping, raw mode, ringing and connect delays, host-name digits (3.6) | `test_calls` | `test_calls` | `test_calls.py` |
| Call errors: BUSY, NO ANSWER, NO DIAL TONE, NO CARRIER (3.5) | `test_calls` | — | `test_calls.py` |
| Answer mode: port 23, RI, ring, `ATA`, `S0`, `AT*ANSWERPORT` incl. USB and AUTO by DTR (3.6) | `test_answer`, `test_modemcfg` | `test_answer` | `test_answer.py`, `test_usb.py` |
| USB modem port (3.2): TinyUSB serial session, flow control, DTR, no reboot on 1200 baud, one call across ports; DCD, DSR and RI sent as serial-state notifications on change, on configure and on open, a short ring still sent on then off | `test_answer`, `test_hostport` | — | `test_usb.py` |
| USB modem port lines: DCD by `&C`, DSR on, RI with the ring cadence, sent on change, again on configure and port open, retried while the endpoint is busy, a short ring still sent; USB names from the selected model, serial number from the Wi-Fi MAC | `test_hostport` (`SerialStateSender`, `usbSerialNumber`), `test_modemcfg` (`productName`) | `test_usb` (serial state) | `test_usb.py` (`.dsr`, `.cd`, `.ri`, serial number) |
| Remote callers can't reach command mode (3.6) | `test_answer` | — | `test_answer.py` |
| Remote `+++` with guard times stays data on every answer port; Telnet commands from a caller before and after `ATA` | `test_answer` | — | `test_answer.py` (bench) |
| Unanswered call dropped after 8 rings for `S0` 0 and above 8; `S0` above 8 answers on ring 8; journal entry | `test_answer` | — | `test_answer.py` (bench) |
| Telnet storm bounded, two ends settle, full send queue stops reading; `S7` doesn't cut the handshake | `test_calls` | — | `test_calls.py` |
| TCP keepalive on call sockets | — (lwIP only; checked by the s3 build) | — | Bench: pull the caller's network cable, `NO CARRIER` in about 2 minutes |
| PPP and SLIP, aliases, private /30, NAT, hang-up; table of malformed LCP/IPCP/PAP/IP frames; lwIP and loop task stack headroom at least 1 KB after the traffic; PPP replies built off the stack (3.6) | `test_internet` | `test_internet` | `test_internet.py`; `interop/serial_pppd.py` |
| Tones: UK dial, busy, ring, DTMF, pulse (3.4) | `test_hwtest`, `test_answer` | `test_hw`, `test_answer` | `test_hwtest.py`, `test_calls.py` |
| Handshake WAV playback per speed (3.4) | `test_calls`, `scripts/test_sound_catalog.py` | `test_calls` | `test_calls.py` |
| Volume: pot, `ATL`, `ATM`, no cap (full volume = full scale); call tones at the recordings' RMS (2.3, 3.4) | `test_hwtest`, `test_calls` | `test_calls`, `test_hw` | `AT*HWPOT=TONE`, `AT*HWTEST` |
| LEDs per table 2.4, `AT*HWLED=AUTO`; CD/TR/RD/SD follow RS232 or USB | `test_hostport` | `test_hw` | `test_session.py`, `AT*HWTEST` |
| Hardware tests `AT*HW...`; line, loopback and bridge refused on RS232 (3.13); 5-minute key time-out, LED patterns refused during a call, per-owner holds | `test_hwtest`, `test_config` | `test_hw` | `test_hwtest.py`, `test_config.py` |
| Wi-Fi: scan, masked join (UTF-8 passphrases), retry, static IP, mDNS, ARQ light, radio off after a scan with Wi-Fi off, join/scan/IP refused during a call, banner (3.6, 3.7) | `test_netcfg`, `test_config_services`, `test_atcmd` | `test_wifi`, `test_rs232` | `test_wifi.py`, `test_calls.py`, `test_answer.py` |
| Clock: NTP (no empty labels), time zones, daylight saving, cached offset for the web status (3.8) | `test_netcfg` | `test_wifi` | `test_wifi.py` |
| Browser Wi-Fi changes: 60 s check, failed join, `AT*WIFI=OFF`/`FORGET` in the window; scan names without control bytes | `test_netcfg` | — | — |
| Wi-Fi and device password, OTA hash and Digest hash never in any output: canary sweep over every `AT*` command, `AT&V`, `ATI`, export, logs and every configuration screen (3.6, 3.8) | `test_netcfg`, `test_config_services` (`test_no_output_contains_either_password_or_its_stored_forms`) | — | `test_wifi.py`, `test_config.py` |
| Device password: first boot, first use, change, hashes only, masked input, random words from a seed drawn once at boot with the hardware noise source on (3.6) | `test_devicecfg` | `test_device` | `test_device.py` |
| OTA: password, ALWAYS/DEMAND, 10-minute window (3.6); the OTA hash is SHA-256 (64 hex) in a 142-byte record; the uploader signs in once with PBKDF2-HMAC-SHA256, refuses an MD5 challenge, never prints the password (`--device-tests --factory-reset --ota-upload` uploads for real) | `test_devicecfg`, `scripts/test_ota_upload.py` (fake modem; `.env` BOM, UTF-16) | `test_device` (SHA-256 and Digest vectors) | `test_device.py` |
| `AT*FACTORY`: Y/N, timeout, every namespace, interrupted reset, no filesystem (3.7) | `test_devicecfg` | `test_device` | `test_device.py --factory-reset` |
| Configuration screen, styles, sizes, `AT*` parity, `AT*HELP`, no idle repaint, closes after 5 minutes idle, Esc cancels a waiting command, `AT*SETTING=` takes one command (3.8); a key sends only the changed cells (under 240 bytes at 80x25), full frames after resize, Ctrl-L, style, colour, theme or other port output, changes-only screen equals a full repaint, output pages step by the lines shown, Alt+letter ignored, "too long" shown at once | `test_config`, `test_config_services`, `scripts/test_generate_ui.py` (single-key hotkeys), `ui/tests` | `test_config` | `test_config.py` |
| Export and replay without secrets, logs, call history; refused store writes leave the live setting alone (3.8) | `test_config`, `test_config_services` | `test_config` | `test_config.py` |
| About generated from the licensing record | `scripts/test_generate_credits.py` | — | `test_config.py` |
| Network console (TCP 2001): only the model, status and password prompt before log-in, no echo, constant-time check, one-second lock-out across connections, three tries, 60 s to log in, one logged-in client (a new log-in takes over; a connection without the password replaces only one at the prompt), replay of the 16 KB console log, output that never waits (skipped-bytes note), on/off setting and its NVS record, address change, prompt held 250 ms for pyserial | `test_netconsole`, `test_devicecfg` (`matches`), `test_config` (menu entry) | — | `test_net_console.py` (with `--net-console`, `--rs232-port`); every console suite through `--console-port socket://...` |
| Information replies start on their own line (S3 S4 first), on the console and through the configuration screen (which doesn't show that first line break) | `test_config` | — | `test_console.py` (12 commands) |
| A long console reply (`AT*ABOUT`, 9.4 KB) doesn't hold up the other ports: 16 KB UART0 transmit buffer | — | — | `test_console.py` (with `--rs232-port`) |
| Web server: Digest login (stale nonce, wrong-password lock-out, parameter/size limits, nonce table, real MD5, falling count), filesystem image check, Host/Origin, tickets, CSRF, WebSocket (checked before the handshake), raw header checks (duplicates, 4 KiB, no bytes after the body), one request per connection, a page loaded in parallel without resets, upload and unmount hand-shakes (late answers), browsers start read-only (3.11) | `test_web`, `scripts/test_image_manifest.py`, `scripts/test_web_peer.py` | `test_web` | `test_web.py` |
| Network names as UTF-8 in the web page's JSON | `test_web` | — | — |
| History file recovery order: file, temporary file, backup, each CRC-sealed | `test_modemcfg` | — | — |
| Web page: status bar, reconnect and 30 s back-off while an update is written, sizing, upload hashing, the whole upload flow (preflight, conflicting identity, confirmation, login refresh, lost connection, refusal, double press), demo with the firmware's frame (3.11) | `web/test/` | — | `test_web.py` |
| The web file list agrees in `build_web.py`, `web_service.cpp`, the page imports, the preview and the e2e check | `scripts/test_web_file_lists.py` | — | — |
| Browser limit (3), idle connection floods on ports 23 and 80, an interrupted upload, USB unplug during a call (opt-in) | — | — | `test_web.py --flood-tests`, `--upload-tests`; `test_usb.py --usb-unplug` |
| Client Tests: display, size (echoed query), keys, transfers (block numbers wrap past 255), hostile X/Y/ZMODEM blocks and headers, noise and slow-link timing, ZFIN without "OO", cable, client settings per port incl. USB (3.12) | `test_client` | `test_client` | `test_client.py`; `interop/serial_lrzsz.py` |
| Build: `sdkconfig` regenerated when `sdkconfig.defaults` changes; image identities | `scripts/test_sdkconfig_refresh.py`, `scripts/test_image_manifest.py` | — | — |
| Repo: `.gitignore` keeps build output, downloads, generated files and `.env` out of git | `scripts/test_gitignore.py` | — | — |
| Build: compiler warnings apply to the project's own code only (`lib/`, `src/`), the two lists agree, a deprecated API is an error | `scripts/test_project_warnings.py` | — | — |

Checks that need a person (sound, LEDs, loopback plug, Amiga and Windows 9x clients) are listed in `docs/ROADMAP.md`.
