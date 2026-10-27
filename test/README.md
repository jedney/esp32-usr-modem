# Test plan

v0.13 validation (27 Oct): **376 native, 22 Node, 12 script and five design
checks pass**, with three new on-board Internet checks and all 11 embedded
programs compiling. New wired Internet acceptance passes 11 cases, with two
opt-in PC UDP echo skips; real UDP DNS and full binary ICMP round trips pass
for both PPP and SLIP. Independent pppd 2.4.9 verifies 64 KiB HTTP, DNS and LCP
hang-up through actual Wi-Fi NAT; two concurrent 8 KiB HTTP flows and authenticated
local HTTP during PPP also pass. The portable-core pppd/kernel test passes.
Full regression: **155 passed, nine skipped**. Detailed results are recorded in
`docs/v0.13-plan.md`. Windows 9x/Amiga acceptance is explicitly deferred by Jon.

v0.12 validation (27 Oct): **361 native, 22 Node, nine script and five design
checks pass**. Three new on-board client checks pass; all ten embedded programs
compile. Nine wired client cases and ten independent COM18 transfers pass, with
zero transfer retries/errors and full content/CRC checks. Both lrzsz 0.12.21rc
and 0.13.1 pass 12 independent portable-core cases. Final browser: 16 pass, one
opt-in skip. See `docs/v0.12-plan.md` for full regression history and corrections.
PuTTY automation was blocked by product policy; visual/listening and unavailable
real-Amiga checks remain manual. At that checkpoint normal v0.12 and matching
LittleFS were installed; v0.13 now retains that filesystem with the new firmware.

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
| v0.6 | 2026-10-24 | Bare board, console COM15, Jon's Wi-Fi (`.env`) | `test_board` 5/5, `test_rs232` 8/8, `test_wifi` 6/6 | 61 passed, 22 skipped (`test_rs232.py` and the bridge test: RS232 not wired) |
| v0.7 | 2026-10-25 | Console COM15, Wi-Fi at 10.0.0.221 on the PC's network, LED and pot; Unitek COM18, RS232/audio unwired | `test_board` 5/5, `test_device` 5/5, internal `test_rs232` 8/8, `test_wifi` 6/6 | Regression: 63 passed, 27 skipped. Device acceptance: 7/7 verified; the previously blocked factory/OTA case passed after reconnecting. Authenticated uploads work by IP and mDNS, wrong/old passwords fail, first-use password display stops, and Wi-Fi plus the saved credential are restored. Every upload attempt first passed 234 native checks |
| v0.8 | 2026-10-25 | COM15, LED/pot, Wi-Fi; COM18 adapter only, RS232/audio unwired | Board 5, device 5, Wi-Fi 6, internal RS232 9, calls 4: all 29 pass | Console 68 passed / 27 skipped; ten call checks passed including true refusal, live BBS, streaming audio and DNS; external RS232 and listening await wiring |
| v0.9 | 2026-10-25 | COM15, LED/pot, Wi-Fi; COM18 adapter only, RS232/audio unwired | All seven suites pass: 39 checks, including four new answer tests and six hardware-driver checks | Console regression 70 passed / 35 skipped; nine local outgoing checks pass (external BBS opt-in skipped); all ten final incoming checks pass. External signals, full panel and audio listening remain pending |
| v0.10 | 2026-10-26 | COM15, ESP32/LED/pot and Wi-Fi; COM18 unwired | All eight suites pass: 43 checks, including PSRAM/history recovery/probe/audio | Configuration 9 passed / 1 skipped; existing console/local-call regression 87 passed / 28 skipped. Normal firmware restored, 23 call records retained after restarts; real PC/Amiga observation and external signals/listening remain open |

## Feature matrix

Sportster UI working-tree coverage uses the actual terminal byte stream through a
small ANSI/CP437 emulator, including one-byte reads, SGR, delayed wrap, cursor
positioning and resize mid-CSI. It covers all requested geometries, tiny safe
fallbacks, the complete catalogue, four/unknown model themes, Back/focus, password
safety, service confirmations, export line integrity and Phonebook capacity/pages.
Generator and static handoff checks are separate contracts; Node tests cover
browser sizing and the synthetic demo. Device/Amiga/Chrome acceptance of this UI
has not been run; the earlier release bench results below do not certify it.

v0.11 bench (26 Oct): all hardware wired on COM15/COM18. All 328 native, 18 browser
unit and six script checks pass. The two new web on-board checks pass and all nine
programs compile. Wired RS232 baseline 21/21; full regression 128 pass / six skip /
two exclusions. Web security/menu/bridge/incoming signals, firmware HTTP installation
and approved USB/browser filesystem installation pass. Final browser suite: 16
pass, one opt-in installation skip. Chrome reconnects after upload with zero page
errors. See `docs/v0.11-plan.md`
for the detailed final web run and remaining manual acceptance.

v0.11 adds Digest/Host/Origin/CSRF policy, socket tickets, a shared browser menu,
read-only busy views, real status and browser hardware/update adapters. Portable
coverage is in `test_web`, plus network rollback and browser-secret regressions in
`test_netcfg`/`test_devicecfg`. `embedded/test_web` checks the real hash/RNG adapter
and UTF-8 output. `test/e2e/test_web.py --web-tests` uses the existing device
credential in memory for HTTP/WebSocket and wired COM18 signal tests. Browser
hash/image tests are in `web/test/upload.test.mjs`; build identity checks are in
`test/scripts/test_image_manifest.py`. All hardware is now wired (Jon, 26 Oct).

| v0.11 feature | Native / host | On-board / real-device acceptance |
|---|---|---|
| Request trust | Duplicate/smuggled headers, Host/Origin, Digest parsing/replay/expiry, bounded single-use tickets | MD5/RNG adapter; real Digest login, invalid Host/Origin and ticket reuse |
| Browser menu | Existing action parity, secret disclosure refusal, CP437-to-UTF-8, bounded queues | Shared menu, second browser/serial isolation, quit/disconnect cleanup |
| Network rollback | Delayed commit, 60-second IP rollback, retained old credentials | Real socket-loss/network acceptance is recorded in v0.11 plan |
| Upload | Numeric versions, image identity/header/size, independent SHA-256 vectors, build manifest checks | Invalid CSRF rejection; actual app/filesystem installation remains explicit acceptance |
| Browser hardware | Shared command policy and Q79 restrictions | COM18 DTR/RTS and DCD/RI/CTS; bridge; loopback requires a physical plug |

Run `python -m pytest test/e2e/test_web.py --console-port COM15 --rs232-port COM18
--web-tests --web-host usr-modem.local`. The device password (OTA and web) comes from
`USR_MODEM_PASSWORD` in the environment/ignored `.env`; do not put it in command arguments.
Only one process may own either COM port. On fitted hardware, the old bare-board
`embedded/test_rs232` GPIO-driving tests must skip; wired end-to-end tests cover it.

v0.9 adds incoming Telnet on port 23, UK ringing/RI/AA, ATA/S0 and a saved
`AT*ANSWERPORT` receiver (RS232 by default). Native: **290 checks pass**, including
26 incoming-call tests and migration from modem NVS layout 3. All **39 on-board
checks pass**; all **ten incoming-call acceptance cases pass** on COM15, covering
manual/automatic answer, binary transport, remote-command and escape isolation,
additional callers, Telnet control messages, reconnect and receiver persistence.
Formatting and cppcheck pass. Physical RI/DCD/DTR, all panel LEDs and ring listening
are still unverified because the external electronics are not connected.

Existing console regression: **70 passed, 35 skipped** (230.59 s). The combined
network regression passed **18 cases**, skipping only the opt-in external BBS
call (206.40 s): all nine original incoming cases and all nine local outgoing cases.
The final incoming run passed **10/10** (99.14 s), adding silent Q2 handshake
cancellation and checking exactly one carrier-loss result after Wi-Fi shutdown.
No firewall changes or new external BBS call were needed.

v0.8 implements outgoing calls, the 64-entry phonebook, Telnet/raw transport,
model selection and stable per-call CONNECT reporting, extended speeds, throttling,
streaming call audio and the password echo boundary fix. Native coverage is in
`test_calls`, `test_atcmd`, `test_modemcfg`, `test_devicecfg` and `test_hostport`.
`embedded/test_calls` covers scratch NVS, all six LittleFS WAVs and ADC/I2S;
`test/e2e/test_calls.py` drives local TCP servers and the opt-in live BBS.

v0.8 validation (25 Oct): 263/263 native checks and 29 unique on-board checks pass.
Console regression: 68 passed, 27 skipped. All ten outgoing-call acceptance checks
pass, including true TCP refusal, DNS failure, the approved live BBS and streamed
handshake. Windows refusal now uses conditional acceptance through the existing
Python rule; no Administrator access or temporary firewall rule is needed. The
previous temporary rule is absent. Formatting and cppcheck pass; current CI results
are attached to PR #10. Firmware and LittleFS uploads retain the native-test gate.
External RS232 and listening checks require the unwired electronics. Generic audio
fallback provenance is in `credits/LICENSES.md`; speed authenticity is not claimed
for fallback files.

Status: ✅ tests written, ⬜ planned. Section numbers refer to the project brief.

| Feature (brief §) | Unit | On-board | End-to-end | Status |
|---|---|---|---|---|
| Board: 16 MB flash, 8 MB PSRAM, OTA partitions (5.2) | — | `test_board` | — | ✅ |
| Pin map is the one Electronics confirmed (Q76, schematic rev A), avoids the S3's reserved pins, no pin used twice, pot on ADC1 (5.2) | `test_pins` | — | `test_hwtest.py` (`AT*HWPINS`) | ✅ |
| Dial-string rules: IP + port, hostnames, phonebook, reserved numbers, invalid → ERROR (6.3) | `test_dialstring` | `test_board` | `test_console.py` (`AT*DIALTEST`) | ✅ |
| Version in banner / `AT*VER` (6.9) | — | — | `test_console.py` | ✅ |
| AT command parser: several commands per line, `A/`, echo, S3/S5, S-registers, `&F` / `&W` / `&V` / `&Z`, `Y` / `Z` (6.2, 6.6) | `test_atcmd` | — | `test_settings.py` | ✅ |
| Result codes, X0–X4 sets, verbal/numeric/quiet modes (6.5) | `test_atcmd`, `test_answer` (Q2 answering) | — | `test_settings.py`, `test_calls.py`, `test_answer.py` | ✅ |
| Host speed from "AT" at 300–115200, fixed with `AT*BAUD` (6.2) | `test_hostport` (waveforms at every rate, clock error, jitter, 140-character line) | `test_rs232` (every rate, inside the chip) | `test_rs232.py` (every rate through the Unitek adapter); `test_session.py` (`AT*BAUD`) | ✅; ⬜ RS232 end-to-end run (needs wiring) |
| RTS/CTS and XON/XOFF flow control, DTR `&D` with S25, DCD `&C`, RI (6.2) | `test_hostport` | `test_rs232` (CTS drops with nothing lost, RTS pauses sending, DTR read) | `test_rs232.py` (CTS/DCD seen by the PC, DTR drop, 16 KB echo with RTS/CTS) | ✅; ⬜ RS232 end-to-end run (needs wiring) |
| `+++` escape guard time (S2, S12), `ATO`, `ATH`, `ATZ` hang-up, `F0` (6.2) | `test_hostport`, `test_atcmd` | — | `test_session.py` | ✅ |
| Test call `AT*TESTCALL`: echo, far end hangs up (NO CARRIER), one call at a time (6.2) | `test_hostport` | — | `test_session.py`, `test_rs232.py` | ✅ |
| Line-speed emulation `&N`, optional throttling, `CONNECT` text (6.2) | `test_calls`, `test_hostport`, `test_atcmd` (model/explicit speed, both throttle directions, result details) | `test_calls` (NVS) | `test_session.py`, `test_calls.py` | ✅; call acceptance passed |
| Phonebook storage and dialling (6.3) | `test_calls` (64 entries, exact keys, CRC, compaction, save rollback, reserved numbers) | `test_calls` (scratch NVS) | `test_calls.py` (temporary entry and binary call) | ✅ |
| Outgoing call: Telnet negotiation, 0xFF escaping, raw mode (6.6) | `test_calls` (fragmentation, partial I/O, bounded subnegotiation) | — | `test_calls.py` (local servers, opt-in live BBS) | ✅; call acceptance passed |
| Errors: refused → BUSY, timeout → NO ANSWER, DNS/Wi-Fi → NO DIALTONE, drop → NO CARRIER (6.5) | `test_calls` (cancel, DNS/TCP deadlines, DTR, rollover, Wi-Fi loss) | — | `test_calls.py` | ✅; call acceptance passed |
| Answer mode: port 23, RI, ring, `ATA` / `S0`, saved receiver (6.6) | `test_answer`, `test_modemcfg` (migration) | `test_answer` (NVS, real lwIP loopback, RI GPIO, ring I2S/pot) | `test_answer.py` | ✅ software; ⬜ external RI/panel/listening |
| Remote callers can't reach command mode (6.6) | `test_answer` (handler spies, queued payload, remote guard times, locally escaped) | — | `test_answer.py` (preanswer/online/escaped injection, Telnet controls) | ✅ |
| PPP (`ATDT666`) and SLIP (`ATDT667`), aliases, private /30, NAPT, hang-up (6.6) | `test_internet` (14), history migration, independent pppd/kernel peer | `test_internet` (board memory, 20 netif/NAPT lifecycles, scratch NVS) | `test_internet.py`; independent `serial_pppd.py` HTTP/DNS | ✅ software/available bench; legacy Windows 9x/Amiga browsing deferred |
| Tones: UK dial, busy, ring, DTMF, pulse clicks (6.4) | `test_hwtest` (frequency, cadence, timing), `test_answer` (ring clock) | `test_hw`, `test_answer` (I2S) | `test_hwtest.py`, `test_calls.py`, `test_answer.py`; listen with `AT*HWTEST` | ✅ generator/call sequences; ⬜ listening |
| Handshake WAV playback per speed (6.4) | `test_calls` (header, truncation, resampling, speed mapping) | `test_calls` (all six assets and nonblocking I2S) | `test_calls.py`; listening needs amp/speaker | ✅; generic fallback approved, listening pending wiring |
| Volume: pot, `ATL`, `ATM`, hard cap (5.3, 6.4) | `test_hwtest` (ATL levels, pot curve and final cap), `test_calls` (ATM sequence) | `test_calls` (ADC/I2S), `test_hw` | `AT*HWPOT=TONE`, `AT*HWTEST` step 3 | ✅; listening pending wiring |
| LEDs per table 5.4 (RD/SD stretched), hand-back with `AT*HWLED=AUTO` | `test_hostport` (panel rules) | `test_hw` (GPIO) | `AT*HWLED`, `AT*HWTEST` step 1; `test_session.py` | ✅ rules; ⬜ watched on the wired LEDs |
| Hardware tests `AT*HW...`: LEDs, amp, tones, pot, RS232 lines, loopback, bridge, pin map (6.14); on the RS232 port the line, loopback and bridge tests are refused (Q79) | `test_hwtest`, `test_pins` | `test_hw` | `test_hwtest.py` (bridge test needs `--rs232-port`), `test_session.py`, `test_rs232.py` | ✅ |
| Wi-Fi: scan, join (masked password, saved only on success), reconnect and retry timing, static IP, mDNS name, ARQ and RGB lights, boot banner (5.4, 6.6, 6.7) | `test_netcfg`, `test_atcmd` (masked prompt, late results) | `test_wifi` (join, scan, lights, NVS), `test_rs232` (RGB LED and speed capture together) | `test_wifi.py`, `test_calls.py`, `test_answer.py` (incoming/outgoing loss and recovery) | ✅ |
| Clock: NTP, time zones and daylight saving, `AT*TIME`/`AT*TZ`/`AT*NTP` (6.8, Q59) | `test_netcfg` (NZ, UK, US, Chatham changes) | `test_wifi` (NTP sets the clock) | `test_wifi.py` (clock within 2 min of the PC) | ✅ |
| Device password: first boot, first use, replacement, hashes only afterwards, masked command input (6.6, Q62/Q69/Q70) | `test_devicecfg` | `test_device` (real MD5/realm vectors, entropy, NVS, offline pot ADC) | `test_device.py` (opt-in password changes and authenticated uploads) | ✅ |
| OTA: password required, always/demand policy, 10-minute window, reconnect/idle gating (6.6) | `test_devicecfg` (fake clock, rollover); `test/scripts/test_ota_upload.py` (host secrecy and env precedence) | `test_device` (policy NVS) | `test_device.py` (listener opens/closes, reboot, wrong/old vs correct/new password) | ✅ |
| Settings kept across a restart, factory templates `&F0`–`&F2`, damaged saved settings → factory, v0.4 settings converted (6.7) | `test_modemcfg`, `test_atcmd` | `test_board` (NVS) | `test_settings.py` (restart), `test_session.py` (`AT*BAUD` restart) | ✅ |
| `AT*FACTORY`: Y/N, timeout, all project namespaces, interrupted reset recovery (6.7) | `test_devicecfg` (failures and restart recovery) | `test_device` (scratch NVS wipe and fresh credential) | `test_device.py` (`--factory-reset`, restores Wi-Fi) | ✅ |
| Configuration screen and `AT*` parity, `AT*HELP` (6.8) | `test_atcmd`, `test_config`, `test_config_services` | `test_config` | `test_config.py` | Software/COM15 pass; real Amiga pending |
| Wi-Fi password never displayed or exported (6.6, 6.8) | `test_netcfg`, `test_config_services` (secret-free replay) | Existing adapter coverage | `test_wifi.py`, `test_config.py` | Software/COM15 pass |
| `ATI0`–`ATI7` Sportster identity, `+FCLASS` fax handling (6.6) | `test_atcmd` | — | `test_settings.py` | ✅ |
| Call log, NTP clock (6.8) | `test_config`, `test_config_services`, `test_netcfg` | `test_config` (files/backup), `test_wifi` (clock) | `test_config.py`, incoming/outgoing regression | Software/on-board/COM15 pass |
| Web page status bar: LED decode, clock in the modem's time zone, call label, remote host/IP, uptime, Wi-Fi bars, reconnect back-off, 80 × 25 font fit (6.11) | `web/test/status.test.mjs` | — | demo-mode check in a browser | ✅ |
| Web server: Digest login, session token, Host check, CSRF, WebSocket messages per `web/PROTOCOL.md`, browser firmware upload (6.11) | `test_web`, browser upload/link tests, image manifest | `test_web` (hash/RNG/UTF-8) | `test_web.py`, real Chrome app/filesystem upload and reconnect | Software/wired browser pass; manual hardware gaps above |
| Client Tests: display, screen size, X/Y/ZMODEM, keyboard, cable check (6.13) | `test_client` | `embedded/test_client` (wired-safe) | `test_client.py`, `test/interop/` with `lrzsz` | PuTTY/physical observations; Amiga deferred |

## v0.10 configuration coverage (26 Oct 2026)

| Feature | Native | On-board | End-to-end / acceptance |
|---|---|---|---|
| Shared menu/AT* inventory and profile help | `test_config` (production registry) | Firmware compile | `test_config.py`; real Amiga pending |
| ANSI, CP437, plain, paging, bounded output, sanitized text | `test_config` | Firmware compile | `test_config.py`; PC terminal observation pending |
| Local slot ownership, dial 123, escape/ATO, DTR, cancellation | `test_config`, `test_config_services`, existing host/call suites | Firmware compile | `test_config.py`; COM15 passes |
| Password masking including F0, secret-free export replay | `test_config`, `test_config_services` | Firmware compile | `test_config.py`; no ordinary credential rotation |
| Structured logs, fallback/wrap, persistent last-50 history | `test_config`, `test_config_services` | `test_config` (PSRAM and scratch files; pass) | `test_config.py`, incoming/outgoing regressions recorded below |
| Sound preview and DNS/ping adapter | Shared cancellation/lifecycle tests; existing audio logic | `test_config` (streaming preview and DNS; pass) | Console diagnostics pass; listening pending |
| Hardware menu availability / Q79 | `test_config` (RS232 actions blocked), existing `test_hwtest` | Existing hardware programs compile | `test_config.py`; COM18 path requires wiring |
| Credits generated from licensing record | `test/scripts/test_generate_credits.py` | Included in firmware | About command passes |

All 312 native and 43 on-board checks pass; four host-script tests pass. Normal
v0.10 firmware is restored on COM15 through the upload gate. Configuration serial
acceptance: nine pass, one skip (48.75 s). The ESP32/LED/pot-only bench is confirmed;
COM18, real PC/Amiga rendering and physical signal/listening acceptance remain open.

Final existing console/incoming/local-outgoing regression: 87 passed, 28 skipped
(385.08 s), with local PC server 10.0.0.208. Together with configuration acceptance:
96 passed, 29 skipped. Final COM15 queries confirm normal v0.10 firmware and 23
retained call-history records after regression restarts. No temporary firewall
change, external BBS call, credential rotation or filesystem upload was used.

## v0.14 pre-v1 review regressions

| Finding | Native / host | On-board | Wired acceptance |
|---|---|---|---|
| R1 serial partial consumption | `test_internet`: bounded InputBuffer retains a PPP chunk tail and verifies every reply after draining | Existing UART/control coverage | `test_internet.py`: 1600 LCP replies across RTS/CTS pressure |
| R2 stale browser guard tick | `test_netcfg`: older tick and full 60-second window across clock rollover | Firmware build | Existing browser/network regression |
| R3 prompt ownership | `test_netcfg`: two ports, cancellation, invalid password, timeout, disconnect, blocked scan | Firmware build | `test_wifi.py`: prompt exclusivity in both COM15/COM18 directions without joining a network |
| R4 sound catalogue | `test_sound_catalog.py`: catalogue matches all packaged PCM files | `test_client`: every source, ALL and 9600 alias read through LittleFS | `serial_lrzsz.py --port COM18 --baud 115200 --sounds`: complete Y/Z sound batches, compare bytes/count/CRC |
| R5 PPP renegotiation | `test_internet`: late LCP/IPCP renegotiation, successful reconnect, fresh bounded timeout | Existing NAPT lifecycle | `test_internet.py`: renegotiate after 31 seconds |
| R6 failed saves | `test_netcfg`, `test_atcmd`: no live/radio mutation or later persistence of rejected settings | Existing NVS roundtrips | Existing settings/Wi-Fi regression |
| R7 invalid NVS reads | `test_devicecfg`: read error cannot generate replacement credentials | `test_device`: real oversized scratch blob, web fails closed, credential record retained | Production namespaces are never corrupted for testing |

Validation results are recorded in `docs/v0.14-validation.md`. Embedded tests
replace the installed app; restore normal firmware afterwards. LittleFS does not
need replacement for these fixes.
