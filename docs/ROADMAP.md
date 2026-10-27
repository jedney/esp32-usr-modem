# Firmware roadmap

The order the remaining firmware is built in, one version per branch. Each version
must leave the modem working, with its tests written and passing. Section numbers
refer to `docs/project-brief.md`.

Done: **v0.1** scaffold, versioning, CI · **v0.2** web page (`web/`) · **v0.3**
hardware tests (`AT*HW…`) · **v0.4** settings store and AT command parser · **v0.6**
Wi-Fi and clock (joins Jon's Wi-Fi, `ping usr-modem.local` works, password absent from
every output; checked on the bench 24 Oct). The pin map is confirmed (Q76) and applied
in v0.6.

Done: **v0.5** RS232 host port and LED driver. The newly wired baseline passes all
21 RS232 end-to-end checks (`test/e2e/test_rs232.py`), including 300-115200 baud,
on 26 Oct through COM15/COM18. Physical panel/listening observations stay separate.

Done: **v0.7** device password, authenticated OTA and factory reset (bench 25 Oct).
After reconnecting the modem to the PC's network at 10.0.0.221, OTA uploads work with
the device password, wrong/old passwords are refused, and `AT*FACTORY` generates a new
password. The initial password disappears from the boot banner after authenticated
use. All seven device acceptance checks have passed.

Done in software: **v0.9** answer mode (bench 25 Oct). A PC Telnet client calls port
23; manual ATA and S0 auto-answer work through COM15, including binary data and
remote-caller isolation while locally escaped. `AT*ANSWERPORT=RS232|CONSOLE`
selects the saved auto-answer destination, default RS232. Native, on-board and
incoming-call acceptance pass; the hardware is now wired. COM18 RI/DCD/DTR and
incoming payload are verified in v0.11. Full-panel observation and ring listening
remain separate physical checks.
See `docs/v0.9-plan.md` and `test/README.md`.

Implemented, acceptance outstanding: **v0.10** configuration terminal. Both local
entry paths, three terminal styles, shared AT* actions, logging/history, diagnostics
and export are implemented. All 312 native and 43 on-board checks pass, as do nine
COM15 configuration cases. External RS232 is verified in the v0.11 regression;
real Amiga acceptance remains open. Either
local menu reserves the call slot (Jon, 26 Oct). See `docs/v0.10-plan.md`.

Implemented, acceptance in progress: **v0.11** web interface firmware. See
`docs/v0.11-plan.md` for authenticated
HTTP/WebSocket transport, the shared browser menu, hardware tests, image uploads
and the now-wired COM15/COM18 acceptance sequence. Jon confirmed one active menu
across console, RS232 and browser, with read-only browser status while busy (26 Oct).
328 native, 18 browser-unit and six script checks pass; the two new on-board checks
pass and all nine test programs compile. Real Chrome Digest login/status, all 16
backend/wired browser checks and firmware-only HTTP installation pass. Jon approved
filesystem replacement with no further backup; USB and browser installation succeed.
The full regression passes 128 checks (six skips, two exclusions), with additional
browser acceptance recorded in the plan. A heartbeat covers half-open sockets after
reset. Loopback-plug, physical listening/panel and real Amiga checks remain open.

| Version | Feature | Brief | Done when |
|---|---|---|---|
| v0.4 | **Settings store and AT command parser** (console first): command-line parsing (several commands per line, `A/` repeat), S-registers, `E Q V X`, `&F &W &V &Z`, `Z`, `ATI0`–`ATI7` (authentic Sportster text), `+FCLASS`, result codes (verbose/numeric/quiet), `AT*` commands in one table, `AT*HELP`. Stored in NVS, logic in `lib/` | 6.2, 6.5, 6.6, 6.7 | Parser and settings unit-tested on the PC; `AT&V` shows the saved profile after a restart |
| v0.5 | **RS232 host port**: AT parser on UART1 as well as the console, `&B0` speed detection from "AT" and fixed `&B1`, RTS/CTS flow control (`&H &R`), DTR (`&D`), DCD (`&C`), RI, `+++` guard time and `ATO`. **Front-panel LED driver** per table 5.4 (RD/SD stretched) | 6.2, 5.4 | End-to-end tests drive the RS232 port through the Unitek adapter at 300–115200 baud |
| v0.6 | **Wi-Fi and clock**: `AT*WIFI` join/scan/status, optional static IP, mDNS name, NTP with Pacific/Auckland, ARQ LED, Wi-Fi password never shown. Boot banner shows Wi-Fi, IP and host-serial settings | 6.6, 6.7 | Joins Jon's Wi-Fi; `ping usr-modem.local` works; password absent from every output |
| v0.7 | **Device password and OTA**: random 12-character password at first boot, banner until first use, `AT*PASS`, ArduinoOTA with the hash, on-demand OTA mode, `AT*FACTORY` | 6.6 | OTA upload from VS Code works with the password; `AT*FACTORY` gives a new one |
| v0.8 | **Outgoing calls**: phonebook (entry 1 = Amiga Retro BBS), `ATD`/`ATDT`/`ATDP` with `lib/dialstring`, dial tone → DTMF/pulse → handshake WAV → `CONNECT`, Telnet (BINARY, ANSI, 80×24) and raw modes, `BUSY` / `NO ANSWER` / `NO DIALTONE` / `NO CARRIER`, `ATM`/`ATL` + pot volume, `&N` line speed. Source freely licensed handshake recordings (8 kHz mono WAV) and record them in `credits/LICENSES.md` | 6.3–6.6 | `ATDT1` reaches the Amiga Retro BBS from a PC terminal; local test server covers each error result |
| v0.9 | **Answer mode**: listen on port 23, RI + UK ring, `ATA`, `S0` auto-answer, AA LED, remote-caller isolation | 6.6 | A Telnet client on the PC can "call" the modem; it can't reach command mode |
| v0.10 | **Configuration screen**: menu data model in `lib/` with ANSI, CP437 and plain renderers; `ATDT123` / `AT*CONFIG`; status, phonebook, settings with help, command reference, About (from `credits/LICENSES.md`), sounds, debug log (PSRAM ring buffer, `AT*LOG`), call log, Wi-Fi tools, updates, config export, Hardware tests screen (all tests on the USB console; on an RS232 session the line, loopback and bridge tests are listed as unavailable, Q79); `AT*` parity unit test | 6.8, 6.14 | Every menu item has an `AT*` command (unit test) and works on the Amiga and a PC terminal |
| v0.11 | **Web interface firmware**: HTTP server, Digest login, Host check, CSRF, `/api/session` + WebSocket per `web/PROTOCOL.md`, config screen in the browser (including the Hardware tests screen: all tests, with line/loopback/bridge only while the modem is idle, bridge traffic shown in the browser terminal, on-screen DCD/RI/CTS toggles, Q79), live LEDs/status, browser firmware/LittleFS upload | 6.11, 6.14 | The v0.2 page works against the real modem with login |
| v0.12 | **Client Tests**: display tests, screen size, keyboard, terminal ID, echo/line endings, flow control, cable check, bell, X/Y/ZMODEM up/downloads (port from Zimodem), save results as client settings | 6.13 | Tested against a PC terminal (with `lrzsz`) and the Amiga |
| v0.13 | **Internet dial-up**: PPP on `ATDT666` and SLIP on `ATDT667` (port from Zimodem), NAT, clean hang-up | 6.6 | Windows 9x or Amiga TCP stack browses the web through the modem |
| v1.0 | **Production**: final build in the case (pin map already confirmed by Electronics, Q76, and applied in v0.6), all on-board and end-to-end tests pass, README and About complete | all | Jon's sign-off |

Implemented, available-bench acceptance passed: **v0.13 Internet dial-up**. See
`docs/v0.13-plan.md` for the source-built Arduino 2.0.17/IDF 4.4.7 NAPT stack,
PPP/SLIP and aliases, settings and clean hang-up. 376 native and three new on-board
checks pass; all 11 embedded programs compile. New Internet acceptance passes 11
cases (two opt-in UDP echo skips); full regression is 155 passed/nine skipped.
Independent pppd 2.4.9 verifies 64 KiB HTTP plus two concurrent 8 KiB flows, DNS,
actual Wi-Fi NAT and LCP hang-up; local authenticated HTTP remains available.
Normal v0.13 is installed, retaining LittleFS. Jon defers Windows 9x/Amiga browsing
acceptance because neither client is available (27 Oct); that criterion remains open.

Implemented, automated bench acceptance passed: **v0.12 Client Tests**, including the existing device-password variable
rename and Sportster terminal/browser UI in one release commit (Jon, 27 Oct).
See `docs/v0.12-plan.md`. Save one client profile per port (console and RS232).
Jon selected PuTTY and authorizes installing lrzsz for transfer tests. No Amiga is
available, so its acceptance remains outstanding. Console COM15, Unitek COM18;
all components are wired. 361 native, 22 browser-unit, nine script and five
design checks pass, along with three new on-board checks, nine client E2E cases,
ten wired transfers and 12 independent transfer cases against each of two lrzsz
versions. Final browser acceptance is 16 passed / one opt-in skip. PuTTY launch
was blocked by computer-use product policy; its visual check and Amiga remain
manual. Normal v0.12 and matching LittleFS are installed; Jon declined a backup.

Notes:

- The Sportster UI handoff is implemented in the working tree for the next bundled
  release: 80x25 first, four model themes, adaptive wide browser layout, Phonebook
  selection and safe keyboard/forms. Bundled and deployed as v0.12; see
  `docs/sportster-ui-implementation.md`. v0.12 enables Client Tests on serial; its browser listing remains serial-only.
- v0.10's configuration and v0.11's web interface must expose the shared
  `AT*ANSWERPORT` receiver setting alongside S0; the factory destination is RS232.
- **v0.8 model update (Jon, 25 Oct):** implemented `AT*MODEL=` for Sportster 14.4k, 28.8k,
  33.6k and 56k Series; factory default remains 14.4k. Selection affects ATI identity
  and default reported CONNECT speed only; speed options and features remain.
  Use `AT*MODEL=14400|28800|33600|56000`, saved immediately device-wide, retained
  by `AT&F`/`ATZ`, reset to 14.4k by `AT*FACTORY`. Explicit phonebook/`&N` speed
  wins over the model fallback; changes apply next call and leave current-call
  CONNECT/ATO reporting stable. The `AT*PASS=` echo fix is implemented: the separator
  stays visible, its value is masked.
  v0.10's config and v0.11's web interface must offer the model selector and show
  the selected model prominently on the first config page, optionally with ASCII
  art above the menu. Model selection and outgoing-call logic are implemented;
  software validation passes (263 native, 29 on-board, 78 serial checks). All
  outgoing-call results, including TCP refusal, are verified. External RS232 and
  listening await wiring. See `docs/v0.8-plan.md` and `test/README.md`.
- The RS232 section and all other hardware are now wired (Jon, 26 Oct); outstanding
  v0.5/v0.8/v0.9/v0.10 physical checks can be run through COM15 and COM18.
- The pin map is confirmed by Electronics (Q76, schematic rev A, 24 Oct 2026) and applied
  in v0.6. If the board ever changes, `lib/hw/src/pins.h` and the values locked in by
  `test/native/test_pins` change together.
- USB CDC serial on the native USB port is a v2 feature (Q25); keep GPIO19/20 free.
- The order can change; record any change in `docs/decisions-log.md`.
- `&B` has the Sportster meaning and `AT*BAUD` fixes the speed (decisions log, v0.5).
  `&B0` (serial speed follows the call) takes effect with real calls in v0.8.
- `AT*TESTCALL` (v0.5) gives a call without a network; later versions can use it to
  test their online features.
