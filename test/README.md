# Test plan

Every modem feature has tests, at these levels:

| Level | Where it runs | Command | When |
|---|---|---|---|
| **Unit** (`test/native/`) | PC, no board | `pio test -e native` | Every push (CI) and before every upload |
| **Script** (`test/scripts/`, `ui/tests/`) | PC | `python -m pytest test/scripts`; `python -m unittest discover -s ui/tests` | Every push (CI) |
| **Web unit** (`web/test/`) | PC, Node.js 20+ | `cd web; npm test` | Every push (CI) |
| **On-board** (`test/embedded/`) | ESP32-S3 | `pio test -e s3` | Before each release |
| **End-to-end** (`test/e2e/`) | PC driving the modem's serial ports and Wi-Fi | `python -m pytest test/e2e --console-port COMx [--rs232-port COMy] [--usb-port COMz]` | Before each release |
| **Interoperability** (`test/interop/`) | PC with lrzsz / pppd | see `test/interop/README.md` | Before releases that touch transfers or PPP |

Feature logic lives in `lib/` with no hardware calls, so most of it is unit-tested on
the PC. On-board tests cover the hardware layer (`lib/hw`); end-to-end tests cover
behaviour as a user sees it. Running the on-board tests replaces the firmware with a
test program: upload the firmware again afterwards. End-to-end options are in
`test/e2e/README.md`.

On the fitted board the RS232 receivers drive the UART pins, so the internal-loopback
cases in `embedded/test_rs232` skip; `e2e/test_rs232.py` covers the port through the
Unitek adapter instead. All nine `embedded/test_rs232` cases are ignored on the fitted
board (no loopback plug), so the `test_rs232` entries in the matrix below mean "present, but
skipped on the bench". `embedded/test_board` writes only the scratch namespace `test_board`.

## Feature matrix

Section numbers refer to the project brief.

| Feature (brief §) | Unit | On-board | End-to-end |
|---|---|---|---|
| Board: 16 MB flash, 8 MB PSRAM, partitions (5.2) | — | `test_board` | — |
| Pin map as confirmed by Electronics (Q89, rev B), no reserved or shared pins, pot on ADC1 (5.2); numbers come from `.env.example`; the lock test fails if `.env` overrides them unless `USR_MODEM_PINS_OVERRIDDEN=1`; script rules reject absent, flash/PSRAM and USB GPIOs | `test_pins`; `test/scripts/test_pin_rules.py` | — | `test_hwtest.py` |
| Dial strings: IP + port, host names, phonebook, special numbers, invalid → ERROR (6.3) | `test_dialstring` | `test_board` | `test_console.py` |
| Version in banner and `AT*VER` (6.9) | — | — | `test_console.py` |
| AT parser: several commands per line, `A/`, echo, S3/S5, S-registers, `&F &W &V &Z`, `Y`, `Z` (6.2, 6.6) | `test_atcmd` | — | `test_settings.py` |
| Result codes, X0-X4, verbal/numeric/quiet, Q2 when answering (6.5) | `test_atcmd`, `test_answer` | — | `test_settings.py`, `test_calls.py`, `test_answer.py` |
| `ATI0`-`ATI7`, `+FCLASS` (6.6) | `test_atcmd` | — | `test_settings.py` |
| Settings kept across a restart, `&F0`-`&F2`, damaged or unreadable settings → factory, old layouts converted; S3/S4/S5 limited to 0-127 and S3 never printable (6.7, STA-09a/e, v2.7) | `test_modemcfg`, `test_atcmd` | `test_board` | `test_settings.py`, `test_session.py` |
| RS232 speed from "AT" at 300-115200, fixed with `AT*BAUD` (6.2) | `test_hostport` (`CallSlot::heldBy` for the "refused during an RS232 call" rule, v2.3) | `test_rs232` | `test_rs232.py`, `test_session.py` |
| RTS/CTS and XON/XOFF, DTR `&D` with S25, DCD `&C`, RI (6.2) | `test_hostport` | `test_rs232` | `test_rs232.py`, `test_answer.py` |
| A port nobody reads never blocks the modem: `RING` dropped at once, a reply waits once then the port is stalled (STA-01, v2.2) | `test_hostport` (`test_sink_gate_*`), `test_answer` | — | `test_rs232.py`, `test_usb.py` (bench) |
| `+++` guard time, DTR hold and throttle credit across the 2^32 ms clock wrap, S3/S5 at 0 and 127, `ATO`, `ATH`, `ATZ`, `F0`, `AT*TESTCALL` (6.2, TG-06, v2.9) | `test_hostport`, `test_atcmd` | — | `test_session.py`, `test_rs232.py` |
| `&N` line speed, `AT*MODEL`, throttling at 300, 2400 and 115200 bps, CONNECT text (6.2) | `test_calls`, `test_hostport`, `test_atcmd` | `test_calls` | `test_session.py`, `test_calls.py` |
| Phonebook; damaged book refuses edits until `AT*BOOKCLEAR` (6.3, STA-09f, v2.7) | `test_calls` | `test_calls` | `test_calls.py` |
| Outgoing calls: Telnet negotiation, 0xFF escaping, raw mode, ringing and connect delays, host-name digits (6.6) | `test_calls` | `test_calls` | `test_calls.py` |
| Call errors: BUSY, NO ANSWER, NO DIAL TONE, NO CARRIER (6.5) | `test_calls` | — | `test_calls.py` |
| Answer mode: port 23, RI, ring, `ATA`, `S0`, `AT*ANSWERPORT` incl. USB and AUTO by DTR (6.6, v2.0) | `test_answer`, `test_modemcfg` | `test_answer` | `test_answer.py`, `test_usb.py` |
| USB modem port (Q25, v2.0): TinyUSB serial session, flow control, DTR, no reboot on 1200 baud, one call across ports; pinned TinyUSB download | `test_answer`, `test_hostport`; `test/scripts/test_network_dependencies.py` | — | `test_usb.py` |
| Remote callers can't reach command mode (6.6, Q44) | `test_answer` | — | `test_answer.py` |
| Remote `+++` with guard times stays data on every answer port; Telnet commands from a caller before and after `ATA` (TG-01, TG-04, v2.4) | `test_answer` | — | `test_answer.py` (bench) |
| Unanswered call dropped after 8 rings for `S0` 0 and above 8; `S0` above 8 answers on ring 8; journal entry (SEC-01, v2.4) | `test_answer` | — | `test_answer.py` (bench) |
| Telnet storm bounded, two ends settle, full send queue stops reading; `S7` doesn't cut the handshake (TG-04, 5.5-12, v2.4) | `test_calls` | — | `test_calls.py` |
| TCP keepalive on call sockets (SEC-01, v2.4) | — (lwIP only; checked by the s3 build) | — | Bench: pull the caller's network cable, `NO CARRIER` in about 2 minutes |
| PPP and SLIP, aliases, private /30, NAT, hang-up; table of malformed LCP/IPCP/PAP/IP frames (TG-03, v2.10) (6.6) | `test_internet` | `test_internet` | `test_internet.py`; `interop/serial_pppd.py` |
| Tones: UK dial, busy, ring, DTMF, pulse (6.4) | `test_hwtest`, `test_answer` | `test_hw`, `test_answer` | `test_hwtest.py`, `test_calls.py` |
| Handshake WAV playback per speed (6.4) | `test_calls`, `scripts/test_sound_catalog.py` | `test_calls` | `test_calls.py` |
| Volume: pot, `ATL`, `ATM`, no cap (full volume = full scale) (5.3, 6.4) | `test_hwtest`, `test_calls` | `test_calls`, `test_hw` | `AT*HWPOT=TONE`, `AT*HWTEST` |
| LEDs per table 5.4, `AT*HWLED=AUTO`; CD/TR/RD/SD follow RS232 or USB (v2.0) | `test_hostport` | `test_hw` | `test_session.py`, `AT*HWTEST` |
| Hardware tests `AT*HW...`; line, loopback and bridge refused on RS232 (6.14, Q79); 5-minute key time-out, LED patterns refused during a call, per-owner holds (v2.3) | `test_hwtest`, `test_config` | `test_hw` | `test_hwtest.py`, `test_config.py` |
| Wi-Fi: scan, masked join, retry, static IP, mDNS, ARQ/RGB, banner (6.6, 6.7) | `test_netcfg`, `test_atcmd` | `test_wifi`, `test_rs232` | `test_wifi.py`, `test_calls.py`, `test_answer.py` |
| Clock: NTP, time zones, daylight saving (6.8, Q59) | `test_netcfg` | `test_wifi` | `test_wifi.py` |
| Browser Wi-Fi changes: 60 s check, failed join, `AT*WIFI=OFF`/`FORGET` in the window; scan names without control bytes | `test_netcfg` | — | — |
| Wi-Fi and device password, OTA hash and Digest hash never in any output: canary sweep over every `AT*` command, `AT&V`, `ATI`, export, logs and every configuration screen (6.6, 6.8, TG-02, v2.9) | `test_netcfg`, `test_config_services` (`test_no_output_contains_either_password_or_its_stored_forms`) | — | `test_wifi.py`, `test_config.py` |
| Device password: first boot, first use, change, hashes only, masked input (6.6) | `test_devicecfg` | `test_device` | `test_device.py` |
| OTA: password, ALWAYS/DEMAND, 10-minute window (6.6) | `test_devicecfg`, `scripts/test_ota_upload.py` (`.env` BOM, UTF-16) | `test_device` | `test_device.py` |
| `AT*FACTORY`: Y/N, timeout, every namespace, interrupted reset, no filesystem (6.7) | `test_devicecfg` | `test_device` | `test_device.py --factory-reset` |
| Configuration screen, styles, sizes, `AT*` parity, `AT*HELP`, no idle repaint, closes after 5 minutes idle, Esc cancels a waiting command, `AT*SETTING=` takes one command (6.8, STA-02b/09d, SEC-07a, v2.7) | `test_config`, `test_config_services`, `scripts/test_generate_ui.py` (single-key hotkeys), `ui/tests` | `test_config` | `test_config.py` |
| Export and replay without secrets, logs, call history (6.8) | `test_config`, `test_config_services` | `test_config` | `test_config.py` |
| About generated from the licensing record | `scripts/test_generate_credits.py` | — | `test_config.py` |
| Web server: Digest login (stale nonce, wrong-password lock-out, parameter/size limits, nonce table, real MD5, falling count), filesystem image check, Host/Origin, tickets, CSRF, WebSocket, upload and unmount hand-shakes (late answers), browsers start read-only (6.11) | `test_web`, `scripts/test_image_manifest.py`, `scripts/test_web_peer.py` | `test_web` | `test_web.py` |
| Web page: status bar, reconnect, sizing, upload hashing, demo (6.11) | `web/test/` | — | — |
| Client Tests: display, size (echoed query), keys, transfers (block numbers wrap past 255), hostile X/Y/ZMODEM blocks and headers, noise and slow-link timing, ZFIN without "OO", cable, client settings per port incl. USB (6.13, TG-03, v2.0, v2.10) | `test_client` | `test_client` | `test_client.py`; `interop/serial_lrzsz.py` |
| Build: network libraries (checked before cached) and image identities | `scripts/test_network_dependencies.py`, `scripts/test_image_manifest.py` | — | — |

Checks that need a person (sound, LEDs, loopback plug, Amiga and Windows 9x clients)
are listed in `docs/ROADMAP.md`.

## v1.0 validation (27 Oct 2026)

Bench: ESP32-S3 with all hardware wired, console COM15, Unitek RS232 adapter COM18 (no
loopback plug), modem on Wi-Fi. The flash was fully erased before installing v1.0.

| Level | Result |
|---|---|
| Unit | 396 passed |
| Script and UI catalogue | 13 script tests and 5 UI layout tests passed; `generate_ui.py --check` clean |
| Web unit | 23 passed |
| Lint | cppcheck: no high or medium findings; clang-format clean |
| On-board | 45 passed, 9 skipped (internal RS232 loopback cases: receivers fitted) |
| End-to-end | 168 passed, 2 skipped (optional PC UDP echo), with `--rs232-port COM18 --call-server-ip --live-bbs --web-tests --device-tests --factory-reset --ota-upload`. `test_web.py --browser-upload` (firmware installed through the web page): 17 passed |
| Live web login | Correct login, replayed and expired nonces answered with `stale=true` and then accepted on retry, wrong password answered after about 1 s |

Not run: the two optional UDP echo cases (need a PC firewall opening) and the checks
that need a person (see `docs/ROADMAP.md`).
