# USR Modem Emulator
<img width="480" alt="20261108_212045" src="https://github.com/user-attachments/assets/d87178f2-25f3-45a6-91df-568b987f7651" />

An ESP32-S3 Wi-Fi "modem" in a 3D-printed replica of a 1990s US Robotics Sportster
Faxmodem. It has a real RS232 port (DB25, wired as a modem), speaks the Hayes AT command
set of the Sportster, plays dial, DTMF, ringing and handshake sounds, and connects your
retro computer to Telnet BBSes and the Internet over Wi-Fi.

**Version 2.0**: adds the USB modem port. What it does:

- Outgoing Telnet and raw TCP calls, from a 64-entry phonebook or by host name or IP.
- Incoming calls on Telnet port 23, with UK ringing, RI, `ATA` and auto-answer.
- A USB modem port on the rear USB-C socket, so a modern PC needs no USB-serial adapter.
- Internet dial-up: PPP (`ATDT666`) and SLIP (`ATDT667`) with NAT.
- A configuration screen on either serial port or in a web browser.
- Client Tests and X/Y/ZMODEM transfers to check a terminal and its cable.
- Front-panel LEDs, a volume knob and a 2 W speaker.
- Choice of Sportster model: 14.4k, 28.8k, 33.6k or 56k.

Every command is listed in **[docs/COMMANDS.md](docs/COMMANDS.md)**.

## Ports

| Port | Use |
|---|---|
| RS232 (DB25) | The computer. Speed is detected from each "AT" (300-115200), or fixed with `AT*BAUD`. RTS/CTS, DTR, DCD and RI work as on a real modem; DSR is always on |
| Rear USB-C (the S3's native USB) | **USB modem port** (v2.0): a PC sees a USB serial port that works like the RS232 port, with its own AT session. No line speed, USB flow control, DTR from the PC program; no DCD or RI (calls show by result codes). The same socket powers the modem |
| USB-C "UART" on the S3 board | Maintenance console, 115200 8N1. Same commands as the RS232 port, plus the boot messages. Inside the case only |
| Wi-Fi | Calls, the web interface (`http://usr-modem.local`), the network console (TCP port 2001) and OTA updates |

## First start

1. Connect the USB console (115200) and power on. The banner shows the version, Wi-Fi
   state and the **device password**. The password is made at first boot, is the login
   for the web interface, the network console and OTA (user `admin`), and is shown at
   each boot until it is first used or changed (`AT*PASS`).
2. Join Wi-Fi:

   ```text
   AT*WIFISCAN            list the networks in range
   AT*WIFIJOIN=1          join number 1 (or AT*WIFIJOIN="My Network")
   Password (Enter for none): ************
   Connected, IP 192.168.1.42
   ```

   The password is typed at a masked prompt and never shown again. The network is saved
   only once it connects. The modem is then `usr-modem.local` (`AT*HOSTNAME` changes it)
   and sets its clock by NTP (time zone Pacific/Auckland; `AT*TZ=?` lists others).
3. Dial the preloaded Amiga Retro BBS from your terminal: `ATDT1`.

## Making calls

| Example | What it does |
|---|---|
| `ATDT1` | Dial phonebook entry 1 |
| `ATDTbbs.example.com:2323` | Dial a host and port (port 23 if left out) |
| `ATDP1` | The same with pulse clicks instead of DTMF |
| `+++` then `ATO` / `ATH` | Escape to commands, then go back online / hang up |
| `ATDL`, `A/` | Redial / repeat the last command |
| `AT*PHONEBOOK=2,bbs.example.com,2323,TELNET` | Save entry 2 (`RAW` for plain TCP; an optional fifth field fixes the CONNECT speed) |
| `AT*PHONEBOOK?`, `AT*PHONEDELETE=2` | List / delete entries |

With the speaker on, a call plays the dial tone, the digits, ringing, the handshake for
the line speed, a short pause, and then `CONNECT`. Dialling a host name plays the digits
of its IP address. `ATM0` connects at once in silence. `AT*RINGDELAY` and
`AT*CONNECTDELAY` set the ringing and the pause. Results follow the Sportster: refused
gives `BUSY`, no connection within `S7` seconds gives `NO ANSWER`, no Wi-Fi or an unknown
host gives `NO DIAL TONE`, and a dropped call gives `NO CARRIER`.

Telnet calls negotiate binary mode, ANSI terminal type and an 80x24 window. The reported
`CONNECT` speed comes from the phonebook entry, else `AT&N`, else the selected model
(`AT*MODEL`, factory 14400). With `&B0` the RS232 speed follows the call speed. The
data itself runs at full speed unless `AT*THROTTLE=ON`.

## Incoming calls

The modem listens on Telnet port 23 while on Wi-Fi. A caller hears no password prompt.
`RING` appears on every port, RI follows the UK double ring and the AA light flashes.
`ATA` answers on the port you type it on. `ATS0=n` answers automatically after `n`
rings (at most 8) on the port chosen with `AT*ANSWERPORT`. The default, `AUTO`, picks the first port
with DTR on (a program has it open), in the order RS232, USB, console. Only one call at a
time: further callers are turned away. A call that nobody answers is dropped after 8
rings (about 24 s), and a caller who vanishes without hanging up is dropped after about
2 minutes (TCP keepalive).

A caller only ever sends data. They can't reach command mode, `AT*` commands, the
configuration screen or the hardware tests, even with `+++` or Telnet commands.

## Internet dial-up

Dial `ATDT666` for PPP or `ATDT667` for SLIP (Zimodem's `5517546` and `5517545` also
work). The computer needs a TCP/IP stack. PPP accepts any login and gives the computer
its address and DNS; SLIP needs them set by hand: computer `10.64.64.2`, gateway
`10.64.64.1` (change with `AT*INTERNET=IP,...`). Traffic goes out through the modem's
Wi-Fi with NAT.

Use a fixed speed and hardware flow control, for example `AT&B1&H1&R2&I0&D2&C1M0`. Hang up
with the client's PPP disconnect, by dropping DTR (`&D2`), or with `AT*INTERNET=HANGUP`
from the other port. Limits: IPv4 only, 1500-byte MTU, no fragmented packets, no header
compression, and `AT*PING` is unavailable during the call.

## Configuration screen

`AT*CONFIG` opens it at once; `ATDT123` plays dial sounds first. It works on the USB
console, the RS232 port and in the browser, but only one at a time. While it is open,
incoming calls are turned away. A screen with no key for 5 minutes closes itself, so a
forgotten one does not hold the line.

- Arrow keys and Enter, or press the hotkey shown. **F1** is help, **Esc/Q** goes back,
  **Q/F10** leaves from the main menu, **Ctrl-C** leaves a form, **Ctrl-L** draws the
  whole screen again.
- After the first frame a key sends only what changed on the screen: an arrow key is about
  70 bytes (100 in RGB colour) at 80x25, so the screen is usable at 2400 bit/s. The first
  frame is 3-5 KB (12-22 s at 2400). `AT*STYLE=PLAIN` has no cursor control, so it sends the
  whole screen (about 2 KB) on every key.
- Screens: status, phonebook, all modem settings with help, Wi-Fi and clock, Internet,
  sounds, updates, logs, A About, B Hardware Tests, C Client Tests, D Command Reference.
- 80x25 is the standard size; larger terminals (up to 132x50) get a wider layout. The
  modem asks the terminal for its size; `AT*SIZE` sets it and `AT*STYLE` picks ANSI,
  CP437 or plain text, for this boot.
- Everything on the screen also has an `AT*` command ([docs/COMMANDS.md](docs/COMMANDS.md)).

## Web interface

Open `http://usr-modem.local` (or the modem's IP) and log in as **admin** with the device
password. The page shows the front-panel lights, call state and clock, and a terminal.
Every browser starts read-only, so an open tab never blocks a call; press **E** (or
**Enter configuration**) to open the same configuration screen as `AT*CONFIG` when no
call, test or other menu is using the modem. `AT*WEB=OFF` turns the web
interface off.

The page can install a `firmware.bin` or `littlefs.bin` from a release. The modem checks
the image belongs to this project and matches its SHA-256, and asks before installing
an older version. A LittleFS image replaces the sounds, web files and **call history**.
Keep a USB cable handy: if power fails during a LittleFS install, restore it over USB
(`pio run -e s3 -t uploadfs`). Browser network changes are undone after 60 seconds
unless you confirm them. The web protocol and security rules are in
[web/PROTOCOL.md](web/PROTOCOL.md).

## Sounds, volume and lights

The knob sets the overall volume; `ATL` scales within it and `ATM` chooses when the
speaker is on. Full volume is the amplifier's full output, which the 2 W speaker can take,
so there is no extra limit. Handshake recordings come from Gough Lui's modem sound collection (see
`AT*ABOUT`). `AT*PLAY` previews any sound.

| Light | Meaning |
|---|---|
| AA | Auto-answer on (`S0` > 0); flashes while ringing |
| CD | Call connected (follows `&C`) |
| RD / SD | Data to / from the computer |
| TR | DTR from the computer (always on with `&D0`) |
| CS | CTS to the computer |
| ARQ | Wi-Fi: off = not set up or off, blinking = connecting, on = connected |

The RGB LED on the S3 board itself is not used (it can't be seen in the case).

## Client Tests and transfers

On either serial port (not the browser), menu **C** or `AT*TEST ...` checks the
terminal: character sets, screen size, keys, terminal identity, echo, bell, throughput
and the RS232 cable lines. Results can be saved as this port's client settings
(charset, size and Backspace code). `AT*SX`/`AT*SY`/`AT*SZ` send test data, the debug log
or the handshake sounds by X/Y/ZMODEM; `AT*RX`/`AT*RY`/`AT*RZ` receive and check a file
without storing it.

## Settings, backup and reset

- Modem settings follow the Sportster: change them with `AT` commands and save with
  `AT&W`. Project settings (`AT*...`) save at once.
- `AT*EXPORT` prints commands that restore the configuration, without passwords.
- `AT&F` loads modem defaults and keeps Wi-Fi, passwords and the phonebook.
- `AT*FACTORY` erases everything and restarts with a new device password (asks Y/N).
- `AT*LOG`, `AT*CALLLOG`, `AT*STATUS` and `AT*RESETINFO` help with problems. The debug log
  holds events only, never typed text, passwords or call data.
- A watchdog restarts the modem if its main loop stops for 15 s; `AT*RESETINFO` then says
  "task watchdog" and shows the crash record.

## Hardware tests

`AT*HWTEST` walks through the electronics (LEDs, speaker, volume, knob, RS232
lines, loopback) and prints a summary; `AT*HW?` lists the single tests. The GPIO map is
in `lib/hw/src/pins.h` (confirmed by Electronics, rev B) and `AT*HWPINS`
prints it. Loopback plug for `AT*HWLOOP`: join RXD-TXD, CTS-RTS and DCD-DTR (DB25 pins
3-2, 5-4, 8-20; bench DB9 pins 2-3, 8-7, 1-4).

## Building the firmware (Windows, VS Code)

### One-time setup

1. Install **Git for Windows** and **VS Code**.
2. Install GCC for the PC unit tests, which run before every upload. Either WinLibs
   (`winget install BrechtSanders.WinLibs.POSIX.UCRT.LLVM`, which also has clang-format)
   or MSYS2 (`pacman -S mingw-w64-ucrt-x86_64-gcc`, then add `C:\msys64\ucrt64\bin` to
   your user Path). Check that `gcc --version` works in a new terminal.
3. Optional: Node.js 20+ for the web tests, Python 3.12 for the end-to-end tests.
4. Clone: `git clone https://github.com/jedney/esp32-usr-modem.git C:\repo\esp32-usr-modem`
5. In VS Code open the folder, choose **Trust** and install the recommended extensions
   (PlatformIO IDE). Restart when asked.

### Build and upload over USB

Use **Terminal → Run Task…**:

1. **Modem: build firmware.** The first build downloads the toolchain and builds the
   Arduino core and ESP-IDF together (this enables NAT), so it takes a while.
2. Plug a USB-C cable into the S3 board's **UART** port (not **USB**). It appears as a
   CH34x COM port.
3. **Modem: upload over USB (tests first).** The PC unit tests run first and a failure
   stops the upload. If it stops at "Connecting…", hold **BOOT**, tap **RST**, release
   **BOOT** and retry.
4. **Modem: upload sounds and web files (LittleFS).** Needed once, and whenever sounds or
   the web page change.
5. **Modem: USB console (115200).** Close it before the next upload: only one program can
   use a COM port.

| What | Command | VS Code task |
|---|---|---|
| PC unit tests | `pio test -e native` | Modem: run unit tests (PC) |
| Build | `pio run -e s3` | Modem: build firmware |
| Upload over USB | `pio run -e s3 -t upload` | Modem: upload over USB (tests first) |
| Upload LittleFS | `pio run -e s3 -t uploadfs` | Modem: upload sounds and web files (LittleFS) |
| Upload over Wi-Fi | `pio run -e s3_ota -t upload` | Modem: upload over Wi-Fi / OTA (tests first) |
| Console | `pio device monitor -e s3` | Modem: USB console (115200) |
| Console over Wi-Fi | `pio device monitor -e s3 --port socket://usr-modem.local:2001` | Modem: network console |
| On-board tests | `pio test -e s3` | Modem: on-board tests |
| End-to-end tests | `python -m pytest test/e2e --console-port COMx [--rs232-port COMy]` | Modem: end-to-end tests |
| Web tests | `cd web; npm test` | Modem: web interface tests |
| Web preview (no hardware) | `python scripts/make_preview.py`, open `web/preview.html` | Modem: web interface preview (demo) |
| Lint | `pio check -e native` | Modem: lint (cppcheck) |

For an emergency upload without the unit tests, set `USR_MODEM_SKIP_TESTS=1` in that
terminal first. On-board tests replace the firmware with a test program: upload the
firmware again afterwards.

### Local secrets (`.env`)

Copy `.env.example` to `.env` (git-ignored) and fill in `USR_MODEM_WIFI_SSID` and
`USR_MODEM_WIFI_PASS` (end-to-end Wi-Fi tests) and `USR_MODEM_PASSWORD` (device password
for OTA and the web tests). Environment variables of the same names win over the file.

The same file holds the GPIO pin map as `USR_MODEM_PIN_*` lines. `.env.example` has the
rev B values and is always read first; a line in `.env` or a real environment variable
overrides one pin. The build stops on a duplicate pin or a number that is not an
ESP32-S3 GPIO. Change the pins only when the board changes. `AT*HWPINS` shows the map in
use.

### Over-the-air updates

With `USR_MODEM_PASSWORD` set, run **Modem: upload over Wi-Fi / OTA (tests first)**. The
modem must be on Wi-Fi and idle. The password is passed in memory, never on the command
line. If the host name has changed, add `--upload-port <name>.local`.

OTA always listens by default. `AT*OTA=DEMAND` keeps it closed until `AT*OTA=ON` opens it
for 10 minutes (`AT*OTA=OFF` closes it early, `AT*OTA=ALWAYS` goes back).

OTA checks the password over UDP 3232, then the modem connects back to the PC on TCP
**32320**. If the password is accepted but the transfer says "No response from device",
run `scripts/allow_ota.ps1` once from an Administrator PowerShell. It allows only
PlatformIO's Python on that port, from the local subnet, on private networks. If the PC
and modem are on different subnets, the router must also allow modem → PC TCP 32320.

### Network console

The maintenance console also works over Wi-Fi, on TCP port 2001 (raw bytes, like
ser2net), so the modem can be used, debugged and, with OTA, flashed while the USB cable is
on the USB modem port or not plugged in at all. Run **Modem: network console** (or any raw
TCP client, or `socket://usr-modem.local:2001` in pyserial) and type the device password
at `Password:` (if no prompt shows, press Enter). It then shows the console's recent output,
the boot messages included while they fit in its 16 KB, and works as the console itself:
the same session as the cable, with the same commands and rules. One logged-in client at
a time: logging in from a new connection closes the earlier one, so a hung connection never
keeps you out.
`AT*NETCONSOLE=OFF` turns it off (saved). The end-to-end tests take
`--console-port socket://usr-modem.local:2001` and log in with `USR_MODEM_PASSWORD`.

It is plain TCP, like Telnet: the password crosses the local network unencrypted. Still
needed the cable: the first flash after a full erase, on-board tests, crash output as it
happens (`AT*RESETINFO` shows the record afterwards) and a modem that doesn't boot or join
Wi-Fi.

### Debugging

From v2.0 the native USB port (GPIO19/20) is the USB modem port, so the built-in USB-JTAG
debugger is not available while the firmware runs. Flash over the UART USB-C port or OTA.
To reach the ROM bootloader on the native port, hold BOOT and tap RST.

Never connect a computer to both the S3's own native USB-C and the rear USB-C socket at
the same time: they share the same data lines.

## Versioning

- `VERSION` holds the version; the boot banner, `AT*VER` and About show it with the git
  commit.
- Every change bumps it once: `python scripts/bump_version.py minor "summary"` (+0.1) or
  `major` (+1.0, for a major feature or hardware change), then the `CHANGES.md` entry is
  written up (Added / Changed / Fixed). Commit messages start with the version.
- Merging to `main` tags `vX.Y` and publishes a GitHub release with the firmware and
  LittleFS images.

## Tests

Logic lives in `lib/` with no hardware calls, so it is unit-tested on the PC; the
hardware layer is `lib/hw`. GitHub Actions runs the unit tests, lint, web tests and a
full build on every push. On-board and end-to-end tests run on the bench before each
release. [test/README.md](test/README.md) maps every feature to its tests.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | Firmware entry point, RS232 port, hardware-test console |
| `lib/` | Firmware modules; `lib/hw` is the only one that touches hardware (pin map in `lib/hw/src/pins.h`) |
| `test/` | Unit (`native/`), on-board (`embedded/`), end-to-end (`e2e/`), interoperability (`interop/`) and script tests |
| `web/` | Web page (`src/`), third-party files (`vendor/`), tests and `PROTOCOL.md` |
| `ui/` | Configuration screen design data shared by firmware and browser |
| `sound/` | Handshake recordings |
| `scripts/` | Build helpers: versioning, test gate, web bundle, OTA |
| `case/`, `images/` | Case parts (STL), print projects, build photos, screw list and label art |
| `electronics/` | Datasheets, pinout and models of the bought-in parts |
| `credits/` | Attribution and the licensing record (`LICENSES.md`) |
| `docs/` | `COMMANDS.md`, project brief, roadmap, decisions log; `history/` holds earlier version plans |
| `.github/workflows/` | CI and release |

## Credits

See `credits/LICENSES.md` (also `AT*ABOUT`). In short: case model by luizpacheco
(CC BY-NC), firmware modules ported from Zimodem by Bo Zimmerman (Apache-2.0), concept
from RetroExternalModem by caiot5, and modem recordings from
[Gough Lui's modem sound collection](https://goughlui.com/2016/05/03/project-the-definitive-collection-of-v-90v-92-modem-sounds/).
