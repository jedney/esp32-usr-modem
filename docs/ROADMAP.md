# Firmware roadmap

The order the firmware was built in, one version per branch. Each version left the
modem working, with its tests written and passing. Section numbers refer to
`docs/project-brief.md`. Detailed plans and acceptance notes for each version are in
`docs/history/`; decisions are in `docs/decisions-log.md`.

**Status: v1.0 released (27 Oct 2026).** All versions below are implemented and pass
their automated tests on the wired bench (console COM15, Unitek RS232 adapter COM18).

| Version | Feature | Brief | Done when | Status |
|---|---|---|---|---|
| v0.1 | Scaffold, versioning, CI | 6.9, 6.12 | CI builds and tags releases | Done |
| v0.2 | Web page (`web/`) | 6.11 | Page works in demo mode | Done |
| v0.3 | Hardware tests (`AT*HW...`) | 6.14 | Every part can be tested from the console | Done |
| v0.4 | **Settings store and AT command parser**: several commands per line, `A/`, S-registers, `E Q V X`, `&F &W &V &Z`, `Z`, `ATI0`-`ATI7`, `+FCLASS`, result codes, `AT*` table, `AT*HELP`. Stored in NVS, logic in `lib/` | 6.2, 6.5-6.7 | Parser and settings unit-tested; `AT&V` shows the saved profile after a restart | Done |
| v0.5 | **RS232 host port**: AT parser on UART1, `&B` speed detection and `AT*BAUD`, RTS/CTS (`&H &R`), DTR (`&D`), DCD (`&C`), RI, `+++` guard time, `ATO`. **Front-panel LEDs** per table 5.4 | 6.2, 5.4 | End-to-end tests drive the RS232 port at 300-115200 baud | Done |
| v0.6 | **Wi-Fi and clock**: `AT*WIFI` join/scan/status, static IP, mDNS name, NTP with Pacific/Auckland, ARQ LED, password never shown, boot banner | 6.6, 6.7 | Joins Wi-Fi; `ping usr-modem.local` works; password absent from every output | Done |
| v0.7 | **Device password and OTA**: random password at first boot, `AT*PASS`, authenticated OTA, on-demand OTA, `AT*FACTORY` | 6.6 | OTA upload works with the password; `AT*FACTORY` gives a new one | Done |
| v0.8 | **Outgoing calls**: phonebook (entry 1 = Amiga Retro BBS), `ATD`, dial tone → digits → handshake → `CONNECT`, Telnet and raw, call results, `ATM`/`ATL` and pot, `&N`, `AT*MODEL` | 6.3-6.6 | `ATDT1` reaches the Amiga Retro BBS; a local test server covers each error result | Done |
| v0.9 | **Answer mode**: port 23, RI and UK ring, `ATA`, `S0`, AA LED, `AT*ANSWERPORT`, remote-caller isolation | 6.6 | A PC Telnet client can call the modem and can't reach command mode | Done |
| v0.10 | **Configuration screen**: ANSI, CP437 and plain; status, phonebook, settings with help, reference, About, sounds, logs, call log, Wi-Fi tools, updates, export, hardware tests; `AT*` parity | 6.8, 6.14 | Every menu item has an `AT*` command and works on a PC terminal | Done; Amiga check open |
| v0.11 | **Web interface firmware**: Digest login, Host/Origin checks, CSRF, WebSocket, the configuration screen in the browser, live status, browser firmware/LittleFS upload | 6.11, 6.14 | The page works against the real modem with login | Done |
| v0.12 | **Client Tests**: display, size, keys, identity, echo, flow, cable, bell, X/Y/ZMODEM, per-port client settings | 6.13 | Tested against a PC terminal and lrzsz | Done; Amiga check open |
| v0.13 | **Internet dial-up**: PPP (`ATDT666`), SLIP (`ATDT667`), NAT, clean hang-up | 6.6 | A TCP/IP client browses through the modem | Done with pppd; Windows 9x/Amiga check deferred |
| v0.14-v0.18 | Review fixes, replacement sounds, menu refresh, dial timing, hostname dial digits | - | Tests pass | Done |
| v1.0 | **Production**: full review, all on-board and end-to-end tests pass, README, command reference and About complete | all | Jon's sign-off | Released |
| v1.1 | Rev B pin map, pins in `.env`, volume cap removed for the CLS0361 speaker | 5.2, 7 | Builds and tests pass on the rev B board | Released |
| v2.0 | **USB modem port**: TinyUSB serial port on the native USB (rear USB-C), its own session, menu and Client Tests record; `AT*ANSWERPORT` USB and AUTO; LEDs follow either host port | 6.2, Q25 | `test_usb.py` passes against the board through the rear socket | Done |

## Checks that need a person

Automated tests can't cover these. They are not blockers for v1.0 but should be done
once on the finished unit:

- Listen to the dial tone, DTMF, pulse, ringing, handshake sounds and the volume knob.
- Watch every front-panel LED during a call (ARQ shows Wi-Fi).
- `AT*HWLOOP` with a loopback plug.
- The configuration screen and Client Tests on a real Amiga terminal, and the PuTTY
  visual check.
- Web browsing from Windows 9x or an Amiga TCP stack over PPP/SLIP.
- The final build in the case with the DB25 connector (the bench uses a DB9).

## After v1.0

- The native USB port (GPIO19/20) is the USB modem port from v2.0 (Q25); it is no longer
  free for the USB-JTAG debugger while the firmware runs.
- If the board ever changes, `lib/hw/src/pins.h` and the values locked in by
  `test/native/test_pins` change together.
- New work follows the same rules: one version per branch, tests first, a
  `docs/decisions-log.md` entry for each decision.
