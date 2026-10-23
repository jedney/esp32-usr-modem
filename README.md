# USR Modem Emulator

An ESP32-S3 Wi-Fi "modem" in a 3D-printed replica of a 1990s US Robotics Sportster
33.6 Faxmodem. It has a real DB25 RS232 port, speaks the Hayes AT command set, plays the
dial, DTMF and handshake sounds, and connects to Telnet BBSes over Wi-Fi.

Status: **v0.3, prototyping.** The firmware has the USB maintenance console and the
hardware tests for the electronics build (`AT*HWTEST`); the modem itself comes next. The full requirements are in the project brief
(`claude/project-brief.md` in the Claude project).

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
   partition (only the web page for now).
5. **Run Task → Modem: USB console (115200).** Press **RST** on the board to see the
   banner with the version, flash and PSRAM size. Type `AT*VER`, `AT*HELP`, `AT*HWINFO`.
   Press **Ctrl+C** in the console terminal before the next upload (only one program can
   use the COM port).

The same actions are in PlatformIO's sidebar under **s3 → General / Platform**.

### Hardware tests

With the console open, `AT*HWTEST` walks through every part of the electronics in
turn (LEDs, speaker, volume cap, pot, RS232 lines, loopback) and prints a pass/fail
summary. `AT*HW?` lists the single tests. The GPIO pin map is in `lib/hw/src/pins.h`
(**proposed, not yet confirmed by Electronics**); `AT*HWPINS` prints it. Pins are
only driven when a test uses them.

Loopback plug for `AT*HWLOOP`: join RXD–TXD, CTS–RTS and DCD–DTR (DB25 pins 3–2, 5–4,
8–20; bench DB9 pins 2–3, 8–7, 1–4).

Other tasks: upload over Wi-Fi, on-board tests, end-to-end tests, web tests and preview,
lint, and version bumps. Running the on-board tests replaces the firmware on the board
with the test program, so upload the firmware again afterwards.

### Debugging

Connect the board's **native USB** port (GPIO19/20) and use **Run and Debug → PIO Debug
(s3)**. PlatformIO generates `.vscode/launch.json` itself, so it is not committed. The
S3's built-in USB-JTAG is used (`debug_tool = esp-builtin`).

### Over-the-air uploads

Set the OTA password once in your environment, then use the OTA task:

```powershell
setx USR_MODEM_OTA_PASSWORD "your-ota-password"   # Windows, then restart VS Code
```

The modem must have Wi-Fi configured and an OTA password set on the device.

### Windows compiler

The `native` unit tests compile on your PC, so they need `gcc`/`g++` (setup step 2). The
ESP32 build itself does not. For an emergency upload without the tests, set
`USR_MODEM_SKIP_TESTS=1` first: open PlatformIO's terminal (sidebar → **Quick Access →
Miscellaneous → PlatformIO Core CLI**), run `$env:USR_MODEM_SKIP_TESTS = "1"`, then
`pio run -e s3 -t upload` in the same terminal.

## Web interface

The modem serves a page with an 80 × 25 terminal (the configuration screen) and a status
bar with the live front-panel lights, call status, remote host, connection time and modem
clock. Source is in `web/`; see `web/PROTOCOL.md`.

- **Preview without hardware:** run **Modem: web interface preview (demo)** (or
  `python scripts/make_preview.py`) and open `web/preview.html`. Press **D** to fake a
  call, **H** to hang up. Every CI run also attaches the preview as a downloadable artifact.
- **On the modem:** the page files are gzipped into `data/www/` and uploaded with the
  LittleFS image (**Modem: upload sounds and web files (LittleFS)**). The firmware side
  of the web server comes in a later version.
- **Tests:** **Modem: web interface tests** (`cd web && npm test`) needs Node.js 20 or later.

## Versioning

- `VERSION` holds the current version (`0.3`). It is built into the firmware together
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
