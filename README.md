# USR Modem Emulator

An ESP32-S3 Wi-Fi "modem" in a 3D-printed replica of a 1990s US Robotics Sportster
33.6 Faxmodem. It has a real DB25 RS232 port, speaks the Hayes AT command set, plays the
dial, DTMF and handshake sounds, and connects to Telnet BBSes over Wi-Fi.

Status: **v0.1, prototyping.** The full requirements are in the project brief
(`claude/project-brief.md` in the Claude project).

## Repository layout

| Path | Contents |
|---|---|
| `src/` | Firmware entry point |
| `lib/` | Firmware modules, kept free of hardware access where possible so they can be unit-tested on the PC |
| `test/native/` | Unit tests that run on the PC (no board) |
| `test/embedded/` | Tests that run on the ESP32-S3 |
| `test/e2e/` | End-to-end tests (Python) that drive the modem through its serial ports |
| `scripts/` | Build helpers: versioning, test gate, version bump |
| `3d-model/`, `images/` | Case model and label art |
| `sound/` | Audio files |
| `credits/` | Attribution and the licensing record (`LICENSES.md`) |
| `.github/workflows/` | GitHub Actions: CI and release tagging |

## Getting started in VS Code

1. Install VS Code and open this folder. Accept the prompt to install the recommended
   extensions (PlatformIO IDE is the important one; it installs the ESP32 toolchain on
   first build).
2. Run **Terminal → Run Task → Modem: run unit tests (PC)**. On Windows this needs a C++
   compiler on the PATH (MinGW-w64 or MSYS2 `gcc`); see "Windows compiler" below.
3. Connect the board's **USB-C port marked for the USB-serial chip** (UART0) and run
   **Modem: upload over USB (tests first)**.
4. Run **Modem: USB console (115200)**. You should see the boot banner and can type `AT`,
   `AT*VER` or `AT*DIALTEST 170064142064`.

Other tasks: build only, upload over Wi-Fi, upload sound files, on-board tests,
end-to-end tests, lint, and version bumps. All are under **Run Task**, and PlatformIO's
own sidebar has the same actions.

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

The `native` unit tests compile on your PC, so they need `gcc`/`g++`. The simplest
option is MSYS2 (<https://www.msys2.org>): install it, run
`pacman -S mingw-w64-ucrt-x86_64-gcc`, and add `C:\msys64\ucrt64\bin` to the PATH.
The ESP32 build itself does not need this.

## Versioning

- `VERSION` holds the current version (`0.1`). It is built into the firmware together
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
