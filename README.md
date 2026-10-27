# USR Modem Emulator

An ESP32-S3 Wi-Fi "modem" in a 3D-printed replica of a 1990s US Robotics Sportster
Faxmodem. It has a real DB25 RS232 port, speaks the Hayes AT command set, plays the
dial, DTMF and handshake sounds, and connects to Telnet BBSes over Wi-Fi.

Status: **v0.13 PPP/SLIP Internet dial-up implemented and running on the wired bench;
Windows 9x/Amiga browsing acceptance is deferred because neither client is available.**
Authenticated browser configuration, local configuration, logs/history, export,
incoming/outgoing TCP/Telnet calls, a 64-entry phonebook, selectable Sportster identity,
Client Tests/transfers, Internet NAPT, optional throttling and call audio are implemented.
See `test/README.md` for validation and pending
bench checks. All hardware is now wired: console **COM15**, Unitek **COM18**.
The wired RS232 baseline passes all 21 cases; physical listening and real Amiga
observations remain separate acceptance items.

## Web configuration (v0.11)

Open `http://usr-modem.local` (or the modem's current IP). Log in as **admin** with
the same device password used for OTA. Set a replacement locally with `AT*PASS`
if needed. `AT*WEB=OFF` disables the saved listener; `AT*WEB=ON` re-enables it from
serial. Every asset and status connection requires login.

One configuration menu across USB console, RS232 and browser reserves the call
slot. Other browsers show read-only live status; press **E** or **Enter
configuration** when free. Quit returns to read-only status. The selected model,
settings, help, phonebook, export, logs and hardware tests use the same menu as
serial. Browser phonebook dialing sends CONNECT and call data to the RS232 host.

The toolbar uploads a raw project `firmware.bin` or `littlefs.bin`, checking its
embedded identity and SHA-256. Confirm older versions explicitly. A LittleFS upload
replaces **sounds, web files and stored call history**; keep USB available because
the final filesystem copy cannot roll back after power loss. Invalid/staging-full
images are rejected before the live filesystem is replaced. Recovery uses
`pio run -e s3 -t uploadfs --upload-port COM15`. Firmware-only updates preserve history.
Use tested release images; browser uploads cannot run the PC test gate themselves.

Browser network changes have a 60-second rollback guard that survives socket loss.
Passwords are write-only in the browser. HTTP is the agreed LAN transport; see
`web/PROTOCOL.md` for authentication, trust boundaries and update details.

Factory model and operational call speed are **14.4k**. All emulated line-speed
options through **56000** remain available (`AT&N38`, or `AT&N0` for highest);
host serial remains 300-115200 and the USB maintenance console stays at 115200.
The full requirements are in `docs/project-brief.md`, with later decisions in
`docs/decisions-log.md` and implementation/acceptance notes in `docs/v0.8-plan.md`
and `docs/v0.9-plan.md`.

## Incoming calls (v0.9)

The modem listens on TCP port **23** while Wi-Fi is connected. A PC Telnet client
can call `<modem-address>:23`; the caller needs no password. Only one incoming or
outgoing call is allowed, including a call still ringing. Additional callers close
without taking over the first call.

| Command | Behaviour |
|---|---|
| `ATA` | Answer the pending caller on the local port issuing the command |
| `ATS0=0` | Manual answer only (default) |
| `ATS0=3` | Auto-answer on the third logical ring |
| `ATS1?` | Live ring count when S0 is enabled; cleared after the call |
| `AT*ANSWERPORT=RS232` | Auto-answer to the host RS232 port (factory default) |
| `AT*ANSWERPORT=CONSOLE` | Auto-answer to the maintenance console; useful on the bench |
| `AT*ANSWERPORT?` / `AT*ANSWERPORT=?` | Current destination / available choices |
| `ATH` / `ATZ` | Reject a ringing call or hang up a connected local call |

`AT*ANSWERPORT` saves immediately, survives restart, `AT&F` and `ATZ`, and returns
to RS232 with `AT*FACTORY`. It cannot change during a call. S0 remains a normal
profile setting (`AT&W` to save); S1 is runtime state and is not stored. Auto-answer
requires the selected terminal to be ready under `&D`; it never falls back to the
other port or interrupts a partial command/password prompt.

One logical ring is the UK double ring: 400+450 Hz, 400 ms on, 200 ms off, 400 ms on,
2000 ms off. It produces one RING result; RI follows the two on portions even with
ATM0. AA flashes while ringing and otherwise follows S0. DCD follows actual carrier
under `&C`. ATM0 mutes the sounds and skips handshake delay after answering. Pot,
ATL and the final speaker cap also apply to ring audio.

The remote side is always Telnet data. Remote `+++`, AT commands, configuration
numbers and Telnet control messages cannot enter the modem's local command mode.
When the local terminal escapes, incoming data waits in bounded queues until ATO
or hang-up. Incoming calls use the existing model, speed, flow control and throttle
settings; Q2 suppresses answer-call results. Wi-Fi loss closes the call and the
listener reopens after reconnect.

On the wired bench use **COM15 at 115200** and **COM18** through the Unitek adapter.
Select the intended auto-answer destination explicitly. Physical control-signal,
full-panel and speaker observations are recorded separately from automated checks.

## Outgoing calls and model selection (v0.8)

| Command | Behaviour |
|---|---|
| `AT*MODEL=14400` | Sportster 14.4k Series; factory model; default reported CONNECT 14400 |
| `AT*MODEL=28800` | Sportster 28.8k Series; default reported CONNECT 28800 |
| `AT*MODEL=33600` | Sportster 33.6k Series; default reported CONNECT 33600 |
| `AT*MODEL=56000` | Sportster 56k Series; default reported CONNECT 56000 |
| `AT*MODEL?` / `AT*MODEL=?` | Current model / list of values |
| `ATDT1` | Dial the preloaded Amiga Retro BBS entry, stored by hostname |
| `ATDTbbs.example.com:2323` | Dial a host/port, Telnet by default |
| `ATDP1` | Same destination, with pulse clicks instead of DTMF |
| `AT&Z0=1`, then `ATDS0` | Save/dial one of four traditional stored numbers, separate from the phonebook |
| `ATDL` / `A/` | Redial last destination / repeat last command |
| `+++`, `ATO`, `ATH` | Guarded escape, resume, hang up |
| `AT*PHONEBOOK?` | List the 64-entry phonebook |
| `AT*PHONEBOOK=2,bbs.example.com,2323,RAW,2400` | Save exact numeric key 2, raw TCP, optional speed override 2400 |
| `AT*PHONEDELETE=2` | Delete entry 2 |
| `AT*THROTTLE=ON` / `OFF` / `?` | Save/query bidirectional line-speed throttling; factory OFF |

`AT*MODEL` is a project command (the star is required). Model changes save
immediately device-wide, survive restart and `AT&F`/`ATZ`, and reset to 14.4k with
`AT*FACTORY`. They affect ATI identification and the fallback CONNECT speed only.
They do not change operational speed, host baud, handshake selection, flow control,
compression support, throttling or available commands. A current call, including
later ATO responses, keeps its reported speed when the model changes.

An explicit phonebook speed overrides `&N`; an explicitly issued `&N` overrides the
model reporting fallback. New factory profiles use operational `&N8` (14400) with
no explicit reporting override. `AT&F1` restores that factory profile and clears an
explicit override; `AT&W` saves the profile. `AT*BAUD` fixes host speed independently.
With automatic host baud and `&B0`/outgoing `&B2`, the RS232 data rate follows the
operational call speed, returning to the command rate on escape or hang-up.
Draining the host output is bounded at two seconds; a host holding RTS off cannot
freeze the console during a speed transition.

Upgrading converts modem NVS layouts 1-3 to layout 4. Existing saved speeds in layouts 1/2 are
preserved as explicit overrides because their prior intent is unknown. Model and
throttle settings are outside the templates; `AT*FACTORY` also erases the phonebook.
Phonebook keys are exact (leading zeroes matter), 1-11 digits; reserved service
numbers cannot be saved. Host names are up to 63 characters; ports are 1-65535.
Entry speed 0 or an omitted speed means no entry override. Wi-Fi and credentials
are unaffected by factory profile commands. Reserved configuration/PPP/SLIP service
numbers remain unavailable until their roadmap versions.

Telnet negotiates BINARY both ways, ANSI terminal type and 80x24 window size, and
escapes 0xFF. RAW entries pass bytes unchanged. Queues are bounded and use TCP/serial
backpressure, including while escaped. S7 bounds DNS/connection setup. Refusal gives
BUSY, timeout NO ANSWER, DNS/no Wi-Fi NO DIAL TONE, and a dropped connected call
NO CARRIER, subject to the existing V/Q/X result settings. Network bytes cannot
execute local AT commands. `&U` rejects operational speeds below its configured floor.
The default CONNECT text is plain; `&A1` adds ARQ for the reliable channel, `&A2`
adds modulation, and `&A3` adds NONE for unimplemented modem compression. Numeric,
quiet and X0 results retain their existing formats.

`ATM0` skips dial/handshake audio delays; `ATM1` plays dial tone, digits and the
selected handshake until CONNECT. `ATM2` keeps a simulated carrier tone after
connection. `ATL0`/`ATL1` select low, `ATL2` medium and `ATL3` high within the pot's
overall level. Every generated/WAV path applies the -7 dB speaker cap last. Call
audio streams without blocking the maintenance loop; missing/invalid required WAVs
abort audible calls with NO CARRIER. Upload LittleFS as well as firmware for audio.
The 1200 recording is CC0; the five other speed files use an approved public-domain
generic handshake fallback. They are not claimed as authentic recordings of those
speeds. Originals, attribution and reproducible conversion are in the repository.

Identity presets are modeled rather than complete verified ROM captures. ATI0
product codes and ATI1 checksum are documented placeholders; ATI3/ATI9 show the
selected series, and ATI7 marks physical ROM/DSP details as emulated/unavailable.
See `credits/LICENSES.md` for sources and sound provenance.

The future AT*CONFIG (v0.10) and web (v0.11) menus will include model selection and
show the selected model prominently on the first page, optionally with ASCII modem
art above the menu. Those future interfaces are not part of v0.8.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | Firmware entry point and console commands |
| `lib/` | Firmware modules. `lib/hw` is the hardware layer and holds the GPIO pin map (`lib/hw/src/pins.h`); the others have no hardware access so they can be unit-tested on the PC |
| `test/native/` | Unit tests that run on the PC (no board) |
| `test/embedded/` | Tests that run on the ESP32-S3 |
| `test/e2e/` | End-to-end tests (Python) that drive the modem through its serial ports |
| `scripts/` | Build helpers: versioning, test gate, version bump |
| `3d-model/`, `images/` | Case model and label art |
| `sound/` | Audio files |
| `credits/` | Attribution and the licensing record (`LICENSES.md`) |
| `web/` | Web interface page (`src/`), third-party files (`vendor/`), tests and `PROTOCOL.md` |
| `docs/` | Project brief snapshot, firmware roadmap, decisions log |
| `.github/workflows/` | GitHub Actions: CI and release tagging |
| `CLAUDE.md` | Rules and commands for Claude Code working in this repo |

## Getting started in VS Code (Windows)

### One-time setup

1. Install **Git for Windows** (<https://git-scm.com>) and **VS Code**
   (<https://code.visualstudio.com>).
2. Install a C++ compiler for the PC unit tests, which run before every upload:
   1. Install MSYS2 from <https://www.msys2.org>.
   2. In the **MSYS2 UCRT64** window, run `pacman -S --needed mingw-w64-ucrt-x86_64-gcc`.
   3. Add `C:\msys64\ucrt64\bin` to your user `Path` (Start → "Edit environment variables
      for your account" → Path → Edit → New).
   4. Open a new PowerShell window and check that `gcc --version` works.
3. Optional: Node.js 20 or later (<https://nodejs.org>) for the web page tests, and Python
   3.12 (<https://www.python.org>) for the end-to-end tests.
4. Clone the repo (skip if you already have it):
   `git clone https://github.com/jedney/esp32-usr-modem.git C:\repo\esp32-usr-modem`
5. In VS Code: **File → Open Folder… → `C:\repo\esp32-usr-modem`**, choose **Trust**, and
   accept **Install** on the "recommended extensions" prompt. PlatformIO IDE is the one that
   matters. Restart VS Code when it asks. The PlatformIO (alien head) icon then appears in
   the left-hand bar.

### Build and upload

1. **Terminal → Run Task… → Modem: build firmware.** The first build downloads the
   ESP32-S3 toolchain (several hundred MB) and takes a few minutes. It ends with
   `SUCCESS`.
2. Plug a USB-C cable into the S3 board's **UART / COM** port (the one wired to the
   USB-serial chip, not the one marked **USB**). In Device Manager → Ports it shows as a
   `USB-SERIAL CH34x` (or similar) COM port. If no COM port appears, install the WCH CH34x
   driver. Unplug other USB-serial adapters for the first upload, or choose the port in
   PlatformIO's status-bar port selector.
3. **Run Task → Modem: upload over USB (tests first).** The PC unit tests run first; then
   the firmware is written (`SUCCESS`). If it stops at "Connecting…", hold **BOOT**, press
   and release **RST**, release **BOOT**, and run the task again.
4. **Run Task → Modem: upload sounds and web files (LittleFS).** This writes the files
   partition (web files and handshake sounds).
5. **Run Task → Modem: USB console (115200).** Press **RST** on the board to see the
   banner with the version, flash and PSRAM size. Type `AT*VER`, `AT*HELP`, `AT*HWINFO`.
   Press **Ctrl+C** in the console terminal before the next upload (only one program can
   use the COM port).

The same actions are in PlatformIO's sidebar under **s3 → General / Platform**.

### Hardware tests

With the console open, `AT*HWTEST` walks through every part of the electronics in
turn (LEDs, speaker, volume cap, pot, RS232 lines, loopback) and prints a pass/fail
summary. `AT*HW?` lists the single tests. The GPIO pin map is in `lib/hw/src/pins.h`
(**confirmed by Electronics, schematic rev A, Q76**); `AT*HWPINS` prints it, and the
`test_pins` unit test stops a pin changing by accident. Pins are only driven when a
test uses them.

Loopback plug for `AT*HWLOOP`: join RXD–TXD, CTS–RTS and DCD–DTR (DB25 pins 3–2, 5–4,
8–20; bench DB9 pins 2–3, 8–7, 1–4).

Other tasks: upload over Wi-Fi, on-board tests, end-to-end tests, web tests and preview,
lint, and version bumps. Running the on-board tests replaces the firmware on the board
with the test program, so upload the firmware again afterwards.

### Wi-Fi and clock

On the console (or the RS232 port):

```text
AT*WIFISCAN            list the networks in range
AT*WIFIJOIN=1          join number 1 (or AT*WIFIJOIN="My Network")
Password (Enter for none): ************
Connected, IP 192.168.1.42
```

The password is typed at a masked prompt and never shown again: `AT*WIFI?` says only
"set" or "not set". The network is saved only once it connects. The modem then answers
as `usr-modem.local` (`ping usr-modem.local`; change it with `AT*HOSTNAME=<name>`) and
sets its clock from NTP (`AT*TIME`). The time zone is Pacific/Auckland; `AT*TZ=?` lists
others. `AT*WIFIIP=<ip>,<mask>,<gateway>[,<dns>]` sets a static address,
`AT*WIFIIP=DHCP` goes back. `AT*WIFI=OFF` switches Wi-Fi off; `AT*WIFIFORGET` forgets the
network.

The front ARQ LED and the on-board RGB LED show the Wi-Fi state: no network set up = ARQ
off, RGB red; connecting = ARQ blinking, RGB flashing green; connected = ARQ on, RGB
green; switched off = both off.

### Local secrets (`.env`)

Copy `.env.example` to `.env` at the repository root and fill it in. `.env` is
git-ignored. It holds the Wi-Fi name and password the end-to-end Wi-Fi tests use
(`USR_MODEM_WIFI_SSID`, `USR_MODEM_WIFI_PASS`) and the device password (OTA and web)
(`USR_MODEM_PASSWORD`). Environment variables of the same names win over the file.

### Debugging

Connect the board's **native USB** port (GPIO19/20) and use **Run and Debug → PIO Debug
(s3)**. PlatformIO generates `.vscode/launch.json` itself, so it is not committed. The
S3's built-in USB-JTAG is used (`debug_tool = esp-builtin`).

### Over-the-air uploads

The first boot generates a random 12-character **device password**, shared by OTA and
the web login (`admin`). It is printed only on the USB console at each boot
until the first authenticated upload or password change. After that only hashes are
kept. `AT*PASS` replaces it with a random password shown once on the requesting local
port; `AT*PASS=<text>` sets 8–32 printable characters, including spaces. Arguments are
masked and cannot be repeated with `A/`.

Put the device password in `.env` (`USR_MODEM_PASSWORD`, see above), then use
**Modem: upload over Wi-Fi / OTA (tests first)**, or:

```powershell
pio run -e s3_ota -t upload
```

The modem must be on Wi-Fi and idle. The upload hook passes the password in memory,
so even verbose PlatformIO output has no password argument. Environment variables
override `.env`. If the hostname has changed, add `--upload-port <name>.local`.

OTA listens by default. `AT*OTA=DEMAND` saves demand mode, normally closed;
`AT*OTA=ON` opens it for 10 minutes and `AT*OTA=OFF` closes it early. `AT*OTA?` shows
policy, state and remaining time. A reboot closes a demand window; reconnecting Wi-Fi
does not extend it. `AT*OTA=ALWAYS` restores the default policy. An upload already
accepted can finish after the window expires.

On Windows, OTA authenticates over UDP 3232, then the modem connects back to the PC
on TCP **32320**. If authentication says OK but transfer says "No response from device",
run `scripts/allow_ota.ps1` once from an **Administrator PowerShell**. It permits only
PlatformIO's Python on that callback port, from the local subnet, on Domain/Private
networks. Existing TCP blocks for that interpreter are restricted to Public networks;
public-network access stays blocked. It does not disable the firewall.

If the PC and modem are on different subnets/VLANs, the network firewall must also
permit **modem → PC TCP 32320**. Allowing it in Windows alone does not open that routed
path. Use a rule limited to those two devices, or put both on the same network.
Successful UDP authentication does not prove the return TCP path is reachable.

`AT*FACTORY` asks Y/N, followed by Enter, with a 30-second timeout. Y erases all stored
modem settings, Wi-Fi and the device password, then reboots with a new password.
An interrupted reset resumes on the next boot. `AT&F1` loads modem defaults only;
Wi-Fi and device credentials are kept.

### Windows compiler

The `native` unit tests compile on your PC, so they need `gcc`/`g++` (setup step 2). The
ESP32 build itself does not. For an emergency upload without the tests, set
`USR_MODEM_SKIP_TESTS=1` first: open PlatformIO's terminal (sidebar → **Quick Access →
Miscellaneous → PlatformIO Core CLI**), run `$env:USR_MODEM_SKIP_TESTS = "1"`, then
`pio run -e s3 -t upload` in the same terminal.

## Configuration terminal

Use `AT*CONFIG` for an immediate local menu, or `ATDT123` for dial sounds and
CONNECT followed by the menu. Either USB console or RS232 menu reserves the modem:
incoming callers are rejected until exit. A menu cannot interrupt an existing call.
Remote callers never receive access to configuration or hardware commands.

Use arrows and Enter, or press a displayed hotkey immediately. Main keys are
1-9/A-D in the supplied design order; D opens Client Tests. The browser lists
Client Tests but execution requires the console or RS232 port.
Esc/Q goes Back in child screens; Q/F10 exits from Main. Ctrl-C exits ordinary
menus/forms; raw client captures and transfers use the controls documented below.
F1 opens contextual help. Main F2/F3/F4 opens Status/Phonebook/Settings; child F2
returns to Main. Forms own their printable input, including Q and menu keys;
arrows/Home/End, Backspace/Delete, Tab between comma-separated fields and Ctrl-U
support editing. Enter reviews a change; Y confirms and N/Esc cancels. Existing
service-owned prompts still require their normal Enter. Trailing Enter after a
hotkey does not submit a form or dismiss a result.

80x25 is canonical: every main option is visible in two columns, and Settings,
Wi-Fi and Hardware lists fit without paging. At >=120x45, Main has description
cards and Phonebook has a list, action cards and selected-entry details. Other
sizes use compact or smaller paged views, bounded to 132x50. ANSI/CP437 terminals
are queried for their cell dimensions with `CSI 18 t`; unsupported terminals keep
80x25. Outside the menu, `AT*SIZE=80,25` overrides this port's dimensions for the
current boot (`AT*SIZE?` reads them; supported range 1-132 by 1-50).

`AT*STYLE=ANSI|CP437|PLAIN` selects this port's style for the current boot.
ANSI uses safe ASCII art and approximate ANSI colours; CP437 uses PC line/block
drawing. The browser converts CP437 to Unicode and uses exact RGB accents.
Plain emits no cursor/colour escapes and remains a transcript fallback. Status
refreshes automatically in ANSI/CP437, or with R in plain mode. Export uses a
separate paged transcript so restore command lines remain complete and copyable.


Phonebook shows saved hosts as NAME, keeps number as the selection key, and pages
nine entries at a time in compact mode. I/Enter opens full details. Keys 1-4 are
List, Add/edit, Delete and Dial; delete/dial need a selected entry and confirmation.
Speed is a configured override, not a measured connection rate. Existing contacts
and storage format are retained; no mock-up contacts are installed. Model titles
read the live state behind `AT*MODEL=14400|28800|33600|56000`; an unrecognized
model explicitly displays Unknown. Footer fields come from device state.

Screens cover status, phonebook, all current modem settings with help, Wi-Fi/clock,
updates, About/credits, command reference, sounds, logs and hardware tests. Client
Tests run on serial and are listed read-only in the browser. Settings say whether they save immediately
or require a profile save (`&W`). Model, S0 and the auto-answer receiver are exposed.
The line, loopback, bridge and held-output tests are unavailable on RS232 because
they would take over that session's port. USB hardware tests still require idle RS232.
Interactive hardware tests use their existing local prompts and return to the menu.

Every feature action has an `AT*` equivalent, listed by `AT*HELP`/`AT*REFERENCE`.
Additional commands include:

| Command | Purpose |
|---|---|
| `AT*STATUS` | Live panel, call, Wi-Fi and host-serial snapshot |
| `AT*SETTING?`, `AT*SETTING=<modem command>` | View profiles or edit through existing validation, e.g. `AT*SETTING=S0=2` |
| `AT*DIAL=<number or host:port>` | Dial through the existing call engine |
| `AT*ABOUT` | Build and licensing acknowledgements |
| `AT*SOUNDS`, `AT*PLAY=<name or speed>` | List/preview generated tones and LittleFS WAVs; any key cancels |
| `AT*LOG`, `AT*LOG=0|1|2`, `AT*LOG=CLEAR` | View, set verbosity or clear the RAM debug ring |
| `AT*LOG=<offset>,<count>` | Read a stable slice, at most 8192 bytes; default view is the last 8192 |
| `AT*CALLLOG` | Last 50 calls with timestamps, results, durations and payload counts |
| `AT*DNS=<host>`, `AT*PING=<host>` | Cancellable DNS/ICMP checks with a ten-second overall bound |
| `AT*RESETINFO` | Reset reasons and available numeric crash summary |
| `AT*EXPORT` | Print restore commands, excluding both passwords; join Wi-Fi separately |
| `AT*RESTART` | Restart after Y/N confirmation; factory reset remains `AT*FACTORY` |

Export replay clears the destination phonebook before restoring the exported one.
`AT*PROFILE`, `AT*PROFILECTL`, `AT*EXPLICIT` and `AT*RESTOREEND` preserve stored
profile speeds and response formatting. Replay from ordinary command mode, with no
call or menu open. The final command acknowledges at the old serial/framing settings
before applying the saved ones. Supply network/device credentials separately.

Debug logs contain structured events, not AT input, passwords or caller payload.
The ring uses 256 KiB of PSRAM, with an explicit reduced-capacity fallback. Verbosity
0 disables events, 1 records lifecycle changes, and 2 also records diagnostic actions.
Logs do not continuously write flash. Call history uses checksummed LittleFS files
and a previous valid backup; it survives restarts and firmware-only OTA. Factory reset
or uploading a replacement filesystem image removes history. Reset diagnostics never
print raw core-dump memory.

Console is COM15 at 115200; Unitek is COM18, with all components wired.
Current v0.13 validation is recorded in `docs/v0.13-plan.md`; the older
`docs/v0.10-plan.md` records its original bench scope. Real Amiga and physical
listening/panel observations remain separate acceptance checks.

### Client Tests (v0.12)

Open D on console COM15 (115200) or host RS232 COM18, or use `AT*TEST <name>`.
Tests reserve the modem. The browser lists them for reference and cannot execute
them; remote callers cannot reach them. PuTTY is the selected PC terminal;
independent transfers use lrzsz. Real-Amiga acceptance remains outstanding.

| Test | Full command |
|---|---|
| Display | `AT*TEST ANSI`, `AT*TEST PC`, `AT*TEST PLAIN`, `AT*TEST LATIN1` |
| Size | `AT*TEST SIZE` (CPR; ruler/manual fallback after one second) |
| Keys, identity, echo | `AT*TEST KEYS`, `AT*TEST ID`, `AT*TEST ECHO` |
| Bell, configured UART format | `AT*TEST BELL`, `AT*TEST FORMAT` |
| Throughput | `AT*TEST TX=5`, `AT*TEST RX=5` (1-30 seconds) |
| Cable signals | `AT*TEST CABLE` (RS232; automatic restore after six seconds) |
| Findings | `AT*TEST SUMMARY` |
| Stage settings | `AT*TEST SET=ANSI,80,25,8` |
| Confirm/save or defaults | `AT*TEST APPLY`, `AT*TEST RESET` (Y confirms) |

Raw key/ID captures show hexadecimal bytes; Ctrl-] ends them. Display and bell
checks require observation. Character-table control positions are represented
safely rather than executed. Console handshake fields are unavailable because
its lines serve ESP reset/boot. Host DSR is hard-wired asserted.

One saved record belongs to each physical port. Charset, dimensions and Backspace
(8 or 127) survive reboot; no attached-client identity is inferred. `AT*STYLE`
and `AT*SIZE` remain local overrides. Measured dimensions can exceed the bounded
132x50 menu viewport. Latin-1 uses ASCII-safe menu frames. Export annotates both
records as manual restore commands, each applied/confirmed on its own port.
Factory reset removes both records.

`AT*SX=TEST,CRC,65536` sends generated 64 KiB data by XMODEM; SUM and 1K select
the other variants. `AT*SY=ALL` batches the frozen log and six handshake sounds;
`AT*SZ=LOG` sends `USRMODEM.LOG` by ZMODEM. Other sources are `HS1200.WAV`,
`HS2400.WAV`, `HS9600.WAV`, `HS14400.WAV`, `HS28800.WAV`, `HS33600.WAV` and
generated `TEST` (1 byte to 16 MiB). `AT*SY=TEST,1048576` or
`AT*SZ=TEST,1048576` generates `TEST1M.BIN` without allocating a megabyte buffer.
ALL requires YMODEM or ZMODEM.

`AT*RX=SUM`, `AT*RX=CRC`, `AT*RX=1K`, `AT*RY` and `AT*RZ` validate uploads and
discard their contents. Use protocol cancellation: ordinary Ctrl-C/ESC/`+++`
are payload during transfers. Deadlines restore the modem after stalls. Reports
include bytes, time/rate, retries/errors and CRC32. XMODEM upload counts include
final-block padding because it carries no original length. See the
[independent/wired test instructions](test/interop/README.md). Close PuTTY before
another program opens COM18.

## Internet dial-up (v0.13)

Dial `ATDT666` for PPP or `ATDT667` for SLIP; Zimodem aliases `5517546` and
`5517545` also work. The attached computer needs a TCP/IP stack. PPP accepts any
login and negotiates IPv4 addressing/DNS; SLIP needs static client configuration.
The default private network is `10.64.64.0/30`: modem/peer `10.64.64.1`, computer
`10.64.64.2`. Set the computer's default route through the modem. Traffic uses
Wi-Fi NAPT; the computer does not share the modem's Wi-Fi address.

Use fixed serial speed (`&B1`), 8N1 and hardware RTS/CTS (`&H1&R2`). A typical
initialization is `AT&B1&H1&R2&I0&D2&C1F1M0`. `ATM0` connects immediately;
other speaker settings retain the dial/handshake sequence and volume cap.
Configure unauthenticated PPP or PAP; compression/encryption is not required.

`AT*INTERNET?` reports setup/state, effective DNS, packet counters and last reason.
`AT*INTERNET=IP,10.64.64.1,10.64.64.2` saves a private /30 address pair, and
`AT*INTERNET=DNS,AUTO` uses Wi-Fi DNS (`DNS,1.1.1.1` selects a fixed server).
Changes require an idle call and cannot overlap Wi-Fi. Export/factory reset include
these settings; modem `&F`/`Z` retain them. Wi-Fi and Clock menu D/E/F provides
Internet status, settings and dialing; browser dialing targets the RS232 port.

PPP LCP termination or RS232 DTR drop with `&D2` hangs up without restarting.
`AT*INTERNET=HANGUP` on the other local serial port ends PPP or SLIP. Binary calls
do not interpret `+++`, software flow characters or command-looking payloads.
Console PPP/SLIP is supported; its USB control lines remain ESP reset/boot lines,
so use COM18 for local management when COM15 carries packets. Wi-Fi loss/address
change ends the call; redial after reconnection.

The bounded serial interface supports unfragmented IPv4 through a 1500-byte MTU;
IPv6, CSLIP, packet compression and fragmented datagrams are unsupported. Use a
client TCP MTU/MSS suitable for that link. Translated TCP/UDP ports occupy
32768-49151, separate from the modem's own ephemeral socket ports. Firmware
`AT*PING` is unavailable during Internet calls to avoid shared ICMP-identifier
ambiguity; client ping and local HTTP/DNS/NTP remain available.
Internet call byte totals include serial framing; packet counters count IP packets
submitted or returned through the network adapter.

The firmware now builds Arduino 2.0.17 and pinned IDF 4.4.7 together from source
to enable NAT; the first build downloads additional tools/sources. SDK defaults
and dependency checksums are tracked; no installed SDK files are patched. See
`docs/v0.13-plan.md` and `test/interop/README.md` for verification. Real Windows
9x/Amiga browsing acceptance is deferred because neither client is available.

## Web interface

The modem serves a page with an adaptive terminal (starting at 80 × 25) and a status
bar with the live front-panel lights, call status, remote host, connection time and modem
clock. Source is in `web/`; see `web/PROTOCOL.md`.

- **Preview without hardware:** run **Modem: web interface preview (demo)** (or
  `python scripts/make_preview.py`) and open `web/preview.html`. Phonebook **4**
  simulates a call after confirmation; **H** ends it. D remains unavailable.
  Every CI run also attaches the preview as a downloadable artifact. All preview
  values are explicitly synthetic; production output comes from the shared C++ UI.
- **On the modem:** the page files are gzipped into `data/www/` and uploaded with the
  LittleFS image (**Modem: upload sounds and web files (LittleFS)**). The modem serves
  the authenticated page and shared terminal directly.
- **Tests:** **Modem: web interface tests** (`cd web && npm test`) needs Node.js 20 or later.

## Versioning

- `VERSION` holds the current version (`0.13`). It is built into the firmware together
  with the git commit, and shown in the boot banner and by `AT*VER`.
- **+0.1** for small enhancements and bug fixes. After 0.9 comes 0.10.
- **+1.0** for a major feature or a hardware change. **v1.0** is the first production
  release.
- Every change bumps the version and adds a `CHANGES.md` entry:
  `python scripts/bump_version.py minor "what changed"` (or the VS Code task).
  Commit messages start with the version, e.g. `v0.2: Add AT command parser`.
- Merging to `main` automatically tags `vX.Y` and creates a GitHub release with the
  firmware image.

## Tests

- **Unit tests (PC):** `pio test -e native`. These run automatically before every upload
  (USB or OTA); a failure cancels the upload. Emergency override:
  `USR_MODEM_SKIP_TESTS=1`.
- **Host uploader tests:** `python -m pytest test/scripts` (also run by CI).
- **On-board tests:** `pio test -e s3` with the bench prototype connected.
- **End-to-end tests:** `python -m pytest test/e2e --console-port COM5` (see
  `test/e2e/README.md`). The live dial test uses the Amiga Retro BBS
  (`www.amigaretro.com:23`).
- On-board and end-to-end tests run before every tagged release.
- `test/README.md` maps every modem feature to its tests.

GitHub Actions runs the unit tests, lint and a full firmware build on every push.

## Credits

See `credits/LICENSES.md`. In short: case model by luizpacheco (CC BY-NC), firmware
modules ported from Zimodem by Bo Zimmerman (Apache-2.0), concept and reference sounds
from RetroExternalModem by caiot5 (GPL-3.0).
