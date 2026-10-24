# Test plan

Every modem feature has tests. Three levels:

| Level | Where it runs | Command | When |
|---|---|---|---|
| **Unit** (`test/native/`) | PC, no board | `pio test -e native` | Every push (GitHub Actions) and automatically before every upload |
| **On-board** (`test/embedded/`) | ESP32-S3 bench prototype | `pio test -e s3` | Before each tagged release |
| **End-to-end** (`test/e2e/`) | PC driving the modem's serial ports | `python -m pytest test/e2e --console-port COMx [--rs232-port COMy]` | Before each tagged release |
| **Web unit** (`web/test/`) | PC, Node.js | `cd web && npm test` | Every push (GitHub Actions) |

Feature logic lives in `lib/` with no direct hardware calls, so most of it can be
unit-tested on the PC. On-board tests cover the hardware layer; end-to-end tests cover
behaviour as a user sees it, including a live call to the Amiga Retro BBS.

Running the on-board tests replaces the firmware on the board with the test program.
Upload the firmware again afterwards (**Modem: upload over USB (tests first)**).

## Last bench run

| Version | Date | Bench | On-board | End-to-end |
|---|---|---|---|---|
| v0.4 | 2026-10-24 | Bare board, console COM15 | `test_board` 5/5; `test_hw` not run (nothing wired) | 34 passed, 1 skipped (bridge test, no RS232 port) |
| v0.5 | 2026-10-24 | Bare board, console COM15 | `test_board` 5/5, `test_rs232` 8/8 (no wires needed); `test_hw` not run | 46 passed, 22 skipped (`test_rs232.py` and the bridge test: no RS232 port yet) |

## Feature matrix

Status: ✅ tests written, ⬜ planned. Section numbers refer to the project brief.

| Feature (brief §) | Unit | On-board | End-to-end | Status |
|---|---|---|---|---|
| Board: 16 MB flash, 8 MB PSRAM, OTA partitions (5.2) | — | `test_board` | — | ✅ |
| Pin map avoids the S3's reserved pins, no pin used twice, pot on ADC1 (5.2) | `test_pins` | — | — | ✅ |
| Dial-string rules: IP + port, hostnames, phonebook, reserved numbers, invalid → ERROR (6.3) | `test_dialstring` | `test_board` | `test_console.py` (`AT*DIALTEST`) | ✅ |
| Version in banner / `AT*VER` (6.9) | — | — | `test_console.py` | ✅ |
| AT command parser: several commands per line, `A/`, echo, S3/S5, S-registers, `&F` / `&W` / `&V` / `&Z`, `Y` / `Z` (6.2, 6.6) | `test_atcmd` | — | `test_settings.py` | ✅ |
| Result codes, X0–X4 sets, verbal/numeric/quiet modes (6.5) | `test_atcmd` | — | `test_settings.py` | ✅ codes; ⬜ call results (v0.8) |
| Host speed from "AT" at 300–115200, fixed with `AT*BAUD` (6.2) | `test_hostport` (waveforms at every rate, clock error, jitter, 140-character line) | `test_rs232` (every rate, inside the chip) | `test_rs232.py` (every rate through the Unitek adapter); `test_session.py` (`AT*BAUD`) | ✅; ⬜ RS232 end-to-end run (needs wiring) |
| RTS/CTS and XON/XOFF flow control, DTR `&D` with S25, DCD `&C`, RI (6.2) | `test_hostport` | `test_rs232` (CTS drops with nothing lost, RTS pauses sending, DTR read) | `test_rs232.py` (CTS/DCD seen by the PC, DTR drop, 16 KB echo with RTS/CTS) | ✅; ⬜ RS232 end-to-end run (needs wiring) |
| `+++` escape guard time (S2, S12), `ATO`, `ATH`, `ATZ` hang-up, `F0` (6.2) | `test_hostport`, `test_atcmd` | — | `test_session.py` | ✅ |
| Test call `AT*TESTCALL`: echo, far end hangs up (NO CARRIER), one call at a time (6.2) | `test_hostport` | — | `test_session.py`, `test_rs232.py` | ✅ |
| Line-speed emulation `&N`, optional throttling, `CONNECT` text (6.2) | `test_hostport` (CONNECT text follows `&N`) | — | `test_session.py` (CONNECT 9600) | ✅ CONNECT text; ⬜ throttling (v0.8) |
| Phonebook storage and dialling (6.3) | ⬜ | ⬜ NVS | ⬜ | ⬜ |
| Outgoing call: Telnet negotiation, 0xFF escaping, raw mode (6.6) | ⬜ | — | ⬜ live BBS | ⬜ |
| Errors: refused → BUSY, timeout → NO ANSWER, DNS/Wi-Fi → NO DIALTONE, drop → NO CARRIER (6.5) | ⬜ | — | ⬜ local test server | ⬜ |
| Answer mode: port 23, RI, ring, `ATA` / `S0` (6.6) | ⬜ | — | ⬜ | ⬜ |
| Remote callers can't reach command mode (6.6) | ⬜ | — | ⬜ | ⬜ |
| PPP (`ATDT666`) and SLIP (`ATDT667`), NAT, hang-up (6.6) | — | ⬜ | ⬜ | ⬜ |
| Tones: UK dial, busy, ring, DTMF, pulse clicks (6.4) | `test_hwtest` (frequency, cadence, timing) | `test_hw` (I2S) | `test_hwtest.py`; listen with `AT*HWTEST` | ✅ generator; ⬜ call sequence |
| Handshake WAV playback per speed (6.4) | ⬜ WAV header | ⬜ LittleFS + I2S | manual listen | ⬜ |
| Volume: pot, `ATL`, `ATM`, hard cap (5.3, 6.4) | `test_hwtest` (curve, cap, pot dead zones) | `test_hw` (ADC) | `AT*HWPOT=TONE`, `AT*HWTEST` step 3 | ✅ curve and cap; ⬜ `ATL`/`ATM` |
| LEDs per table 5.4 (RD/SD stretched), hand-back with `AT*HWLED=AUTO` | `test_hostport` (panel rules) | `test_hw` (GPIO) | `AT*HWLED`, `AT*HWTEST` step 1; `test_session.py` | ✅ rules; ⬜ watched on the wired LEDs |
| Hardware tests `AT*HW...`: LEDs, amp, tones, pot, RS232 lines, loopback, bridge, pin map (6.14); on the RS232 port the line, loopback and bridge tests are refused (Q79) | `test_hwtest`, `test_pins` | `test_hw` | `test_hwtest.py` (bridge test needs `--rs232-port`), `test_session.py`, `test_rs232.py` | ✅ |
| Wi-Fi: join, static IP, mDNS name, loss → NO CARRIER (6.6) | — | ⬜ | ⬜ | ⬜ |
| OTA: password required, on-demand mode (6.6) | ⬜ settings | — | ⬜ | ⬜ |
| Settings kept across a restart, factory templates `&F0`–`&F2`, damaged saved settings → factory, v0.4 settings converted (6.7) | `test_modemcfg`, `test_atcmd` | `test_board` (NVS) | `test_settings.py` (restart), `test_session.py` (`AT*BAUD` restart) | ✅ |
| `AT*FACTORY` (6.7) | ⬜ | ⬜ NVS wipe | ⬜ | ⬜ |
| Configuration screen and `AT*` parity, `AT*HELP` (6.8) | `test_atcmd` (`AT*HELP` lists the whole `AT*` table); ⬜ menu model | — | `test_settings.py` | ⬜ menu (v0.10) |
| Wi-Fi password never displayed or exported (6.6, 6.8) | ⬜ | — | ⬜ | ⬜ |
| `ATI0`–`ATI7` Sportster identity, `+FCLASS` fax handling (6.6) | `test_atcmd` | — | `test_settings.py` | ✅ |
| Call log, NTP clock (6.8) | ⬜ | ⬜ | ⬜ | ⬜ |
| Web page status bar: LED decode, clock in the modem's time zone, call label, remote host/IP, uptime, Wi-Fi bars, reconnect back-off, 80 × 25 font fit (6.11) | `web/test/status.test.mjs` | — | demo-mode check in a browser | ✅ |
| Web server: Digest login, session token, Host check, CSRF, WebSocket messages per `web/PROTOCOL.md`, browser firmware upload (6.11) | ⬜ | ⬜ | ⬜ | ⬜ |
| Client Tests: display, screen size, X/Y/ZMODEM, keyboard, cable check (6.13) | ⬜ | — | ⬜ with `lrzsz` | ⬜ |
