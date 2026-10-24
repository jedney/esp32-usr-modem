# Firmware roadmap

The order the remaining firmware is built in, one version per branch. Each version
must leave the modem working, with its tests written and passing. Section numbers
refer to `docs/project-brief.md`.

Done: **v0.1** scaffold, versioning, CI · **v0.2** web page (`web/`) · **v0.3**
hardware tests (`AT*HW…`) · **v0.4** settings store and AT command parser.

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
| v0.12 | **Internet dial-up**: PPP on `ATDT666` and SLIP on `ATDT667` (port from Zimodem), NAT, clean hang-up | 6.6 | Windows 9x or Amiga TCP stack browses the web through the modem |
| v0.13 | **Client Tests**: display tests, screen size, keyboard, terminal ID, echo/line endings, flow control, cable check, bell, X/Y/ZMODEM up/downloads (port from Zimodem), save results as client settings | 6.13 | Tested against a PC terminal (with `lrzsz`) and the Amiga |
| v1.0 | **Production**: pin map confirmed by Electronics (Q76), final build in the case, all on-board and end-to-end tests pass, README and About complete | all | Jon's sign-off |

Notes:

- v0.5 needs the RS232 section wired on the bench (MAX3232s, DB9). If the final pin map
  (Q76) changes, only `lib/hw/src/pins.h` changes.
- USB CDC serial on the native USB port is a v2 feature (Q25); keep GPIO19/20 free.
- The order can change; record any change in `docs/decisions-log.md`.
- Before v0.5: Jon to decide the `&B` meaning (USR vs brief 6.2) (decisions log, v0.4).
