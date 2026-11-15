# USR Modem Emulator: firmware specification

What the firmware does and the hardware it drives, as the code behaves now. Keep it current with the code: change this file in the same branch as the behaviour.

- Every command and option is in [COMMANDS.md](COMMANDS.md); this file says what the firmware is for and why, and leaves out what the command reference already lists.
- Decisions and the reasons behind them are in [decisions-log.md](decisions-log.md).
- The order the firmware was built in and what is left is in [ROADMAP.md](ROADMAP.md).
- The page protocol is in [../web/PROTOCOL.md](../web/PROTOCOL.md); the test matrix is in [../test/README.md](../test/README.md).
- The case and the electronics keep their own files in `case/` and `electronics/`.

Repository: `C:\repo\esp32-usr-modem` (private; `origin` is `https://github.com/jedney/esp32-usr-modem`).

## 1. Goal

An ESP32-S3 Wi-Fi "modem" in a 3D-printed replica of a 1990s US Robotics Sportster Faxmodem. It has a working DB25 RS232 port and is driven by the Hayes AT command set as the Sportster speaks it. It must work with a modern PC (a terminal program on the USB modem port or a USB-to-serial adapter) and with genuine historic machines that have a real serial port (for example an Amiga).

Behaviour on dial: `ATDT<number>` gives a dial tone, the DTMF of the digits dialled, ringing, the handshake sound for the emulated speed, then `CONNECT <speed>` and Telnet or raw passthrough to the remote host (usually a BBS). On failure it plays a busy tone where the real modem would and returns a result code.

Design priority: stay future-proof (current framework, spare memory and GPIO).

## 2. Hardware the firmware drives

### 2.1 Board

- **ESP32-S3 N16R8 DevKitC-1 clone** (ePartners ST8002): dual-core 240 MHz, 16 MB flash, 8 MB octal PSRAM, 57 x 28 mm, no mounting holes, two USB-C ports.
- **UART0** (GPIO43/44) goes to the board's USB-serial chip: the maintenance console (3.7). **UART1** is the host RS232 port. The **native USB** (GPIO19/20) is the USB modem port (3.2); it is also wired to the rear USB-C socket's D-/D+. The board's own USB-C port, the one on the UART side (console and flashing), is the right-hand USB port seen from the back of the modem, behind the blanking plate.
- The on-board RGB LED (GPIO47) is not used and the pin stays reserved.
- Flash holds two 4 MB OTA app slots, about 7.9 MB of LittleFS (sounds, web files, call history) and a 64 KB core dump (`partitions_16mb.csv`). NVS holds the settings.
- Reserved GPIOs: strapping 0, 3, 45, 46; octal PSRAM 35-37 (and flash 26-32); native USB 19/20; UART0 43/44; the RGB LED 47.

### 2.2 RS232 (DCE, DB25 female in the final build; DB9 on the bench)

- Modem outputs: **RXD (DB25 3), CTS (5), DSR (6), DCD (8), RI (22)**. Modem inputs: **TXD (2), RTS (4), DTR (20)**. GND is pin 7. Connect only pins 2-8, 20 and 22; leave the others open (Amiga ports carry supply voltages on some).
- The bench connector is a DB9 wired as DCE: 1 DCD, 2 RXD, 3 TXD, 4 DTR, 5 GND, 6 DSR, 7 RTS, 8 CTS, 9 RI.
- Two RS232 Pal (MAX3232) modules at 3.3 V give four drivers (RXD, CTS, DCD, RI) and four receivers (TXD, RTS, DTR and one spare). **DSR is hard-wired asserted**, as on the Sportster's factory default, so firmware cannot control it and `&S` is fixed at 0.
- The TTL side is inverted: asserted = LOW. That matches the UART's active-low RTS/CTS, so UART1's hardware flow control is used directly. DTR, DCD and RI are plain GPIO.

### 2.3 Audio

- MAX98357A I2S amplifier on 5 V, mono from a stereo stream. The SD pin is driven by a GPIO: low = shutdown, high = on (left channel). It is low whenever no sound plays.
- GAIN pin unconnected (9 dB). Speaker: Same Sky CLS0361-L152, 8 Ω, 2 W. **There is no volume cap**: full scale is the amp's full output, which the speaker can take. Every sound still goes through `volume::outputGain()`; a smaller speaker or a higher gain needs a cap there again. The generated call tones (dial, ring, busy, carrier, DTMF) are cut by `volume::kCallToneDb` to the handshake recordings' level; pulse clicks and the hardware test tones stay at full scale.
- The volume pot (WH148, 10 kΩ linear, 100 nF at the wiper) is read on **ADC1** (ADC2 is unusable with Wi-Fi). The pot sets the overall level and `ATL` scales within it.

### 2.4 LEDs

Seven front-panel LEDs, each on its own GPIO (GPIO to resistor to LED to GND, HIGH = lit).

| LED | Meaning here |
|---|---|
| AA | Auto-answer on (`S0` above 0); flashes while an incoming call rings |
| CD | Carrier: a call is connected (mirrors DCD, always on with `&C0`) |
| RD / SD | Data to / from the computer, each held lit for 50 ms after a byte |
| TR | DTR from the computer (always on with `&D0`) |
| CS | The CTS output read back (off only when the modem pauses the computer) |
| ARQ/FAX | Wi-Fi: off = not set up or switched off, blinking = connecting, on = connected |

CD, TR, RD and SD follow either host port (RS232 or USB); CS follows RS232 only.

### 2.5 Pin map

The pin numbers live in **`lib/hw/src/pins.h`** (which reads `USR_MODEM_PIN_*` values from `.env.example`, then `.env`, then the environment) and are locked by `test/native/test_pins`. `AT*HWPINS` prints the map in use.

| Function | GPIO |
|---|---|
| RXD out (UART1 TX), TXD in (UART1 RX) | 1, 2 |
| CTS out (UART1 RTS), RTS in (UART1 CTS) | 42, 41 |
| DTR in, DCD out, RI out | 40, 39, 38 |
| LEDs AA, CD, RD, SD, TR, CS, ARQ | 4, 5, 6, 7, 15, 16, 17 |
| I2S LRC, BCLK, DIN; amp SD | 11, 12, 13; 14 |
| Volume pot (ADC1) | 10 |
| Spare | 8, 9, 18, 21, 48 |

GPIO18 and 8 are also the nominal pins of the UART2 used by `test/embedded/test_rs232` to play the computer (nothing is wired to them).

### 2.6 Boot-state resistors

From reset until the firmware sets the pins (and throughout flashing) the S3's GPIOs float. The board has 10 kΩ pull-ups to 3.3 V on the four RS232 driver inputs (RXD, CTS, DCD, RI) so the computer sees idle. The amp SD pin has no resistor: it floats until the firmware drives it low.

## 3. Firmware

### 3.1 Approach

- A purpose-built code base that implements the USR Sportster command behaviour. Parts of **Zimodem** (Bo Zimmerman, Apache-2.0) are ported in: Telnet, PPP/SLIP and the X/Y/ZMODEM transfer code. Those files keep their licence headers and say they were changed.
- **Platform:** Arduino core 3.3.12 as a component of ESP-IDF 6.1.0, built with the official PlatformIO platform `espressif32@8.0.0` (versions pinned in `platformio.ini`). Project code builds with `-Wall -Wextra` and deprecated APIs are errors. See the decisions log for the reasons and the fallback.
- **Layout:** logic in `lib/` with no hardware calls (unit-tested on the PC); `lib/hw` is the only module that touches hardware; `src/` holds the entry point and the parts that tie modules to the board (RS232 and USB ports, the hardware-test console, the console output path, the web service).

### 3.2 Serial

- **Ports.** Three local ports run the same command set, each with its own session, echo, configuration screen and Client Tests record: the maintenance console (UART0), the RS232 host port (UART1) and the USB modem port (TinyUSB CDC on the native USB). The browser terminal is a fourth modem session, opened by an automatic terminal request on page load, subject to existing ownership checks (3.11). They share **one call slot**: only one call, menu or test owns the modem at a time.
- **Host speed and line speed are separate.** The RS232 speed is detected from each "AT" (300-115200 baud, 8N1) by decoding a recording of the TXD-in pin, or fixed with `AT*BAUD`. `&B` has the Sportster meanings. The emulated line speed comes from `&N`, the phonebook entry or the selected model, and chooses the handshake sound and the `CONNECT` text. Data runs at full serial speed unless `AT*THROTTLE=ON`.
- **Lines and flow control.** `&H`, `&R` and `&I` choose RTS/CTS or XON/XOFF; `&D2` makes a DTR drop hang up; `&C1` makes DCD follow the call; `+++` with a 1 s guard time escapes and `ATO` returns. Commands are accepted with DTR off (3-wire cables). Factory profile `&F1`: `&H1 &R2 &D2 &C1`, `CONNECT <speed>` plain, tone dialling.
- **USB modem port.** A PC sees a USB serial port named for the selected model. It has no line speed (the speed the PC sets is shown but not used) and USB does the flow control. DCD, DSR and RI reach the PC as CDC serial-state notifications by the same rules as on RS232. The port keeps one COM port across firmware versions because the Wi-Fi MAC is its serial number.
- **Output never blocks the loop.** A reply to a port nobody reads is dropped after 250 ms and the port marked stalled; unsolicited output (`RING`) is dropped at once if there is no room.
- **Auto-answer port.** `AT*ANSWERPORT` chooses where `S0` answers: RS232, USB, console or AUTO (the first port with DTR on, in that order).

### 3.3 Dialling

- Numeric dial strings, so old terminal address books that reject letters still work: 12 digits = zero-padded IPv4 on port 23, 13-17 digits = IP plus port (`ATDT001002003004789` is 1.2.3.4 port 789), fewer than 12 digits = phonebook entry or special number. Invalid strings give `ERROR` before any sound. Spaces, dashes, brackets and dial modifiers are stripped.
- A quoted string, or one containing `.` or `:`, is a host name or dotted IP: `ATDT"bbs.example.com:2323"`, `ATDTwww.amigaretro.com`. Dialling a host name plays the digits of its resolved IPv4 address.
- `ATDP` plays UK pulse clicks instead of DTMF.
- **Phonebook:** 64 entries, numbers of 1-11 digits, each with host, port, Telnet or raw mode and an optional CONNECT speed. Entry 1 is the Amiga Retro BBS (`www.amigaretro.com:23`), the test BBS.
- **Special numbers:** `ATDT123` the configuration screen, `ATDT666` PPP, `ATDT667` SLIP (Zimodem's `5517546` and `5517545` also work). They cannot be phonebook entries.
- Wi-Fi is set up with AT commands, the configuration screen or the console, and once the modem is on the network by the web page. There is no captive portal and no Ethernet.

### 3.4 Sounds

- Order: dial tone, DTMF of the digits (generated live), ringback, the handshake for the emulated speed, a short pause, `CONNECT`. `ATM0` skips the audio and connects at once; `ATM1`/`ATM2`/`ATL0-3` follow the Sportster. `AT*RINGDELAY` and `AT*CONNECTDELAY` set the ringback length and the pause.
- UK tones, generated in firmware: dial 350+450 Hz, busy 400 Hz 0.375 s on and off, ring 400+450 Hz 0.4 s on, 0.2 off, 0.4 on, 2.0 off; pulse dialling at 10 pulses per second (digit *n* = *n* pulses, 0 = 10). DTMF is the same worldwide.
- Handshakes are 8 kHz mono 16-bit PCM WAVs in LittleFS for 1200, 2400, 9600, 14400, 28800, 33600 and 56000 (`sound/`, credits in `credits/LICENSES.md`), played over I2S. An intermediate speed uses the nearest lower file, 300 uses 1200.
- `AT*PLAY` previews any sound or tone; the configuration screen lists them.

### 3.5 Result codes

| Condition | Sound | Result |
|---|---|---|
| Connection refused or reset while connecting | Busy tone | `BUSY` |
| No connection within `S7` seconds | Silence | `NO ANSWER` |
| DNS failure or no Wi-Fi | None | `NO DIAL TONE` |
| Remote drops an established call | None | `NO CARRIER` |

The factory result set is `X4` (all codes). Numeric and verbal results follow the Sportster.

### 3.6 Other features

- **Telnet and raw TCP:** Telnet by default, overridable per phonebook entry. It negotiates BINARY both ways, reports terminal type `ANSI` and the session window size (serial defaults to 80x24; browser reports live dimensions), and escapes 0xFF. Raw passes bytes unchanged.
- **Answer mode:** listen on port 23, no password. `RING` on every port, RI and the AA LED follow the UK ring cadence, `ATA` and `S0` answer, and an unanswered call is dropped after 8 rings. A dead caller is dropped after about 2 minutes (TCP keepalive).
- **Remote caller isolation:** a remote caller can never reach command mode, the configuration screen or any `AT*` command. Only bytes from a local port are parsed.
- **Internet dial-up:** PPP and SLIP for Windows 9x and Amiga TCP stacks, on a private /30 (default 10.64.64.1 modem, 10.64.64.2 client, `AT*INTERNET` changes it) with NAT to Wi-Fi. Any PPP login is accepted. A DTR drop or LCP terminate ends the session and drops DCD. The engine is the project's own on raw lwIP.
- **Identity (`ATI`):** `ATI0`-`ATI7` and `ATI9` mimic the selected Sportster series so old operating systems detect it (text from a capture of a real unit where one exists). The firmware version is in the boot banner, `AT*VER` and the About screen. `AT*MODEL=14400|28800|33600|56000` selects the series.
- **Fax:** `AT+FCLASS=0` gives `OK`; fax classes give `ERROR`.
- **Command conventions:** standard commands follow the Sportster; project commands are `AT*...` (3.8).
- **Settings:** the Sportster's two stored templates (`&W0`, `&W1`, `Y`, `Z`), project settings that save at once, and the phonebook, Wi-Fi, device and client records, all in NVS with a CRC (any other layout loads factory settings).
- **Wi-Fi:** one saved network, DHCP or static IP, mDNS name `usr-modem` (`usr-modem.local`, configurable). The password is typed at a masked prompt and saved only when the join works. A lost network during a call gives `NO CARRIER`. Joining, scanning and changing the IP are refused while a call is up. **The Wi-Fi password is never displayed, logged, exported or sent to the web page**: screens show "set" or "not set".
- **Clock:** NTP (`pool.ntp.org`), UTC internally, local time from the project's own POSIX-rule code with 16 named zones; default Pacific/Auckland.
- **Device password:** one login for the web page, the network console and OTA. User `admin`, password 12 random characters generated at first boot from the hardware noise source.
  - It is shown in the boot banner at every boot until it is first used (a web, console or OTA login) or changed. After that only hashes are kept (SHA-256 for OTA, MD5 for the web's Digest, as browsers require).
  - `AT*PASS` generates a new one and shows it once on the requesting local port or authenticated browser terminal; `AT*PASS=<text>` sets 8-32 printable characters. It never asks for the old password.
  - It is never logged, exported, included in status messages or another browser's output, or shown on a remote (port 23) session. The network console counts as a local console.
- **OTA updates:** ArduinoOTA with the device password, on the local network only, no update checks over the Internet. The listener is on by default; `AT*OTA=DEMAND` keeps it closed except for a 10-minute window opened by `AT*OTA=ON`. `scripts/ota_upload.py` is the PC side. The image is not encrypted in transit.
- **Call history and logs:** the last 50 calls, kept across restarts. A PSRAM ring buffer of debug events (no typed text, passwords or call data), the last reset reason and any crash record.
- **Watchdog:** a task that stops for 15 s restarts the modem; `AT*RESETINFO` says so.

### 3.7 Maintenance console and factory reset

- **Console:** UART0 through the board's USB-serial chip (the USB port behind the blanking plate, on the right seen from the back), fixed 115200 8N1, independent of every host-serial setting. The boot banner shows the version, reset reason, Wi-Fi state and IP, OTA state, the host-serial settings and, until first use, the device password (never on the RS232 or USB port). It accepts the full AT and `AT*` set, on the same stored configuration, and the configuration screen. There is no console password: access needs the case open.
- **Network console:** the same console over Wi-Fi on TCP port 2001 (raw bytes, plain TCP), factory ON, `AT*NETCONSOLE=ON|OFF`. It asks for the device password first; then it replays the newest 16 KB of all console output (1 KB fallback if PSRAM is short; the banner may have been overwritten by later output) and is the same session as the cable, a local console that shows what the cable shows. One logged-in client at a time: a new log-in closes the earlier client. It never holds up the modem.
- **Factory reset:** `AT&F` loads modem settings (Wi-Fi, phonebook and device password are kept); `AT*FACTORY` erases every store after a Y/N confirmation and restarts with a new device password. Both are accepted on the local ports.
- **Debug:** `AT*LOG`, `AT*STATUS`, `AT*RESETINFO` and the Debug screen.

### 3.8 Configuration screen and `AT*` commands

- **Entry:** `AT*CONFIG` (instant) or `ATDT123` (dial tone, handshake and `CONNECT` first), on any local port, and from the web page. Never reachable by a remote caller.
- **Style:** one compact wrapping list in plain ASCII text or ANSI colour. It works at any terminal width, with no cursor positioning, arrow navigation or box drawing. Letters open items, Enter confirms text and enum choices, X/Esc goes back, P/R changes pages and Q exits at the root. Friendly names, current values and units come from the shared metadata. Lists and enum choices omit explanatory help; the web menu retains it as hover tooltips. Input prompts retain units and limits.
- **Content:** status (LEDs, Wi-Fi, call, host-serial settings), phonebook, every setting, Wi-Fi and clock tools, Internet, sounds, updates and password, logs and call log, About (version, build, acknowledgement of every project and file used), Hardware Tests (3.13), Client Tests (3.12), the command reference, configuration export and restart/factory reset with confirmation.
- **Parity rule:** everything on the screen also has an `AT*` command, and `AT*HELP` and `AT*?` list them. The screen, the commands and the browser come from one data model (`lib/config`, `ui/`), so parity is unit-tested.
- **Typed menu metadata:** `lib/config/ui-bindings.json` supplies friendly names, help, value types, ranges, units, choices and command templates for all 13 top-level entries and 78 nested items. `scripts/generate_ui.py` emits C++ metadata for the text menu and `web/src/ui-commands.js` for browser consumers; native tests compare it with the firmware tables. The schema, composite forms and display-unit conversion are defined in `ui/DESIGN_SPEC.md`.
- A list prints on entry or explicit refresh and echoes only new text while editing. Idle lists produce no output. Commands and exports retain paged output, confirmations and masked secret entry. The screen closes after 5 minutes with no key and holds the call slot while open (incoming callers are turned away).
- Results of Client Tests can be saved as the port's client settings (character set, size, Backspace code). Terminal dimensions remain available through `AT*SIZE` and browser size reports, independently of the wrapping configuration list.

### 3.9 Versioning and change log

- `VERSION` in the repository root is the source of truth; `scripts/version.py` embeds it with the short commit hash (`-dirty` for uncommitted changes).
- Every branch bumps it once with `scripts/bump_version.py minor|major "summary"`: +0.1 (0.9 is followed by 0.10) for enhancements and fixes, +1.0 for a major feature or hardware change. `CHANGES.md` follows Keep a Changelog. Commit messages start with the version.
- CI (`.github/workflows/ci.yml`) checks every push; `release.yml` runs on `main`, and when `vX.Y` is not yet tagged it re-runs the tests, builds, tags and publishes a release with the firmware and LittleFS images.

### 3.10 Development environment and testing

- **Tools:** VS Code with PlatformIO. `platformio.ini` environments: `s3` (USB upload), `s3_ota` (OTA to `usr-modem.local`, password from `USR_MODEM_PASSWORD`) and `native` (tests on the PC). `scripts/build_web.py` bundles the web page into the LittleFS image. Tasks, commands and setup steps are in the README.
- **Every feature has tests, run before deployment:**
  - unit tests on the PC (`test/native/`, Unity), Node tests for the page (`web/test/`) and script tests (`test/scripts/`);
  - on-board tests (`test/embedded/`), which replace the firmware while they run;
  - end-to-end tests (`test/e2e/`, pytest and pyserial) against the console, the RS232 port and the USB port; `lrzsz` and `pppd` are the interoperability peers (`test/interop/`).
- **Gate:** every upload first runs the PC unit tests and stops if any fail (`scripts/pre_upload_tests.py`; `USR_MODEM_SKIP_TESTS=1` is for emergencies only).
- Lint is `pio check` (cppcheck) and clang-format (100 columns); CI checks both.

### 3.11 Web interface

- On by default (`AT*WEB=OFF` turns it off). HTTP only, on port 80, separate from answer mode on port 23. Login `admin` and the device password with **Digest** authentication for every page; **CSRF tokens** on every change; a **Host-header check** (the modem's name and IP, which also blocks DNS rebinding); a Content-Security-Policy that allows only the modem's own scripts. The WebSocket takes a single-use token fetched after the Digest login. Accepted risk: the session is not encrypted on the LAN.
- **The page:** the masthead shows the full currently selected model name and updates when any local port changes it. A modem terminal (xterm.js, IBM Plex Mono, VGA colours), minimum 80x25, with status lights, call state, remote host and IP, connection time, Wi-Fi, clock and link state. The page automatically requests access on first open; the firmware grants only one browser its own AT parser and Telnet/raw TCP data line; `ATD` and `AT*DIAL` dial on that session, `+++` escapes, `ATO` resumes and `ATH` hangs up. `AT*CONFIG` opens the shared configuration list. Closing the owner socket cancels pending input and hangs up. Size reports feed Telnet NAWS, including live resize, up to 65535 columns and rows. CP437 BBS output is translated to UTF-8; `AT*STYLE=UTF8` selects a UTF-8 BBS. File transfers, Client Tests and PPP/SLIP remain serial only.
- **Page controls and idle guard:** The page automatically requests one terminal session on first opening. It never takes over another browser or a modem reserved by a serial call, incoming call, menu, test or update. An idle browser terminal does not reserve the call slot, so RS232 and USB can still dial or answer. After loss, reboot, refused access or idle expiry, the centred overlay says "Session Disconnected" and "Click the Terminal area, or select a menu item to reconnect". Transport status reconnects automatically; terminal access retries only on user action. There are no open/close buttons; close the tab to release a terminal. A menu selection waits for granted access and firmware readiness before continuing. Keys retain xterm's CR, BS/DEL and ANSI sequences. Paste is queued up to 1 MiB, chunked at 512 bytes and held by WebSocket/TCP backpressure. Only a connected browser-owned call is guarded: four minutes without user activity shows a one-minute countdown, any input cancels it, and expiry sends an owner-only hang-up control and closes the terminal, leaving the reconnect overlay. Modem data does not reset activity; wall-clock deadlines handle delayed background callbacks. Short stacked status toasts report call identity/disconnect reason, terminal access, link loss/recovery, refusal and uploaded updates without focus changes or motion.
- **Recent console history:** Diagnostics → Recent console history reads authenticated, read-only `GET /api/boot` and writes the reply into the terminal. It returns the newest 16 KB of all maintenance-console output since boot, with a 1 KB fallback if PSRAM allocation fails. A long `AT*ABOUT` reply (about 9.4 KB) can push the banner out. Device-password lines may be included for the authenticated browser; the Wi-Fi password is never included. Each chunk is copied under the console ring lock, then sent after releasing it, stopping at the captured end of the log.
- **Call buttons:** An inert disabled "No call" button appears when there is no call. During dialling, ringing or a connected call it is replaced by a dark red "Hang up" button with a handset icon. Hang up is enabled only for this browser's call, uses the owner-only control to cancel dialling or hang up directly in command or online mode, and retains terminal ownership.
- **Updates:** the page uploads `firmware.bin` and `littlefs.bin`, streamed into the inactive app slot. Only images built for this project are accepted, checked by SHA-256; downgrades need confirmation; refused during a call. A LittleFS image replaces the sounds, web files and call history.
- **Friendly menu:** one compact row follows `AT*CONFIG`, with overflow under More and serial-only Client Tests omitted. Generated metadata supplies icons' labels, help, units, ranges, choices and templates for validated dialogs; profile/register and command variants select one operation at a time. No-input commands run immediately as terminal input. Phonebook opens as a live table with add/edit/delete/dial actions. Wi-Fi passwords, confirmations and interactive test keys remain in the terminal. A firmware readiness acknowledgement includes consumed input bytes, preventing commands from being appended to partial input or prompts. Upload is under Updates and Reset; DCD/RI/CTS toggles and Stop test are under Hardware Tests. Link state appears once in the top badge, call state and Hang up share the terminal header, and LEDs plus guidance, Wi-Fi strength (dBm on hover) and time occupy the bottom strip. Light/dark themes follow the browser preference; overflow and secondary details adapt to narrow windows.
- **Wi-Fi changes** from the page revert after 60 s if the new network does not connect.
- The protocol is in `web/PROTOCOL.md`. `?demo` and `web/preview.html` simulate a modem.

### 3.12 Client Tests

A Client Tests section of the configuration screen (and `AT*TEST`, `AT*SX/SY/SZ`, `AT*RX/RY/RZ`) checks what the connected computer and terminal program can do. They run on serial sessions only (RS232, USB, console).

- **Display:** ANSI (16 colours, bold, underline, reverse, blink, cursor movement), the PC CP437 table, plain 7-bit ASCII and an ISO-8859-1 table (the Amiga's set).
- **Screen size:** ask the terminal where the cursor is after moving to row and column 999 (`ESC[6n`); with no reply, show a ruler and ask.
- **Keyboard:** the code each key sends (Backspace 08 or 7F, arrows, function keys, Enter).
- **Identity, echo, bell:** answerback, Device Attributes and Device Status replies; stair-stepping and local echo; BEL.
- **Throughput and flow control:** send and receive flat out for a set time and report the rate and how often RTS paused the modem.
- **Cable check:** live DTR and RTS, and DCD, RI and CTS toggled in turn so the terminal's indicators can be watched. DSR shows always on.
- **Serial format:** the detected speed and format.
- **Transfers:** XMODEM (checksum, CRC, 1K), YMODEM batch and ZMODEM downloads of the log, the sound files (DOS 8.3 names such as `HS33600.WAV`) or a generated file of a chosen size, and uploads that are checked, reported and discarded. Reports bytes, time, characters per second and retries.
- **Summary:** a results page with an offer to save the findings as the port's client settings.

### 3.13 Hardware tests

`AT*HW...` commands and a Hardware Tests screen check each part of the electronics.

- **Guided test `AT*HWTEST`**, six steps (Enter starts, S skips, Q quits) ending in a summary: LEDs one by one; the speaker (1 kHz, sweep, UK dial tone, DTMF, pulse clicks, busy); full volume; the pot with a following tone; RS232 lines with DCD, RI and CTS toggled; loopback (plug joining RXD-TXD, CTS-RTS, DCD-DTR: DB25 3-2, 5-4, 8-20; bench DB9 2-3, 8-7, 1-4) at 300-230,400 baud.
- **Single tests** are listed by `AT*HW?` and in COMMANDS.md: pins, chip info, files, LEDs, amp, volume, tones, DTMF, pulse, pot, lines, signals, loopback and a console to RS232 bridge.
- **Safe on a bare board:** no pin is driven until a test uses it.
- **Where they run:** the console, USB port and browser run all of them, with the RS232 line, loopback and bridge tests only while the modem is idle. An RS232 session runs everything except those three, which show as unavailable with the reason. A remote caller runs none.
- **They cannot freeze the modem:** each interactive test ends when its terminal goes away or after 5 minutes with no key, LED patterns are refused during a call, and the browser releases only what it set.

## 4. Attribution

- Case model: **luizpacheco** (Luiz Pacheco), Thingiverse thing:7063155, **CC BY-NC**. Modified versions keep the attribution and stay non-commercial. The front label art is inspired by two of luizpacheco's designs, with higher resolution, more model designs and hole alignment improvements added by ChatGPT.
- Firmware base: **Zimodem** by Bo Zimmerman, **Apache-2.0**. Keep its LICENSE and NOTICE and mark every ported or changed file as changed. Its ZMODEM code derives from the Synchronet/SyncTERM code (licence not stated in the file).
- Concept: **RetroExternalModem** by caiot5 (GPL-3.0), which builds on WiFi2DialUp (Luis Miguel Silva), PodSix pimodem (Matt Whitted) and tcpser (Jim Brain / FozzTexx).
- Handshake recordings: Gough Lui's 2016 modem sound collection.
- The repository stays private. The licensing record is **`credits/LICENSES.md`**: every third-party component, file and library with its source, licence and obligations. The same list feeds the About screen (`scripts/generate_credits.py`).
