# USR Modem Emulator: notes for Claude

ESP32-S3 Wi-Fi "modem" in a 3D-printed US Robotics Sportster 33.6 replica, with a DB25
RS232 port, the Hayes AT command set, dial/DTMF/handshake sounds and Telnet to BBSes.
Owner: Jon Edney. Windows 11 PC, VS Code + PlatformIO, repo at `C:\repo\esp32-usr-modem`.

## Where the requirements are

- **`docs/project-brief.md`**: snapshot of the full project brief (24 Oct 2026). The
  firmware spec is **section 6**; the pin map and electronics interface are in
  **section 7**; open questions are in **section 9**. Items marked *Proposed* are not
  decided: ask Jon before relying on them.
- **`docs/ROADMAP.md`**: the order of the remaining firmware versions, with what "done"
  means for each. Work on one roadmap version per branch.
- **`docs/decisions-log.md`**: append every new decision or spec change made while
  coding here (date, what, why). Don't edit `docs/project-brief.md`: its master copy is
  shared with other workstreams, and Jon merges the log back into it.
- `docs/COMMANDS.md`: the complete command reference. Update it with every command or
  option change, together with `AT*HELP` and the README.
- `test/README.md`: feature-to-test matrix. `web/PROTOCOL.md`: web page ↔ firmware protocol.
- `docs/history/`: plans and validation notes from earlier versions (read-only record).
- `credits/LICENSES.md`: licensing record. Update it whenever third-party code, a
  library or a sound file is added.

## Rules (decided by Jon)

- **Versioning.** Every branch bumps the version once:
  `python scripts/bump_version.py minor "summary"` (+0.1; 0.9 → 0.10), or `major` (+1.0)
  only for a major feature or a hardware change. Then rewrite the generated `CHANGES.md`
  entry properly (Keep a Changelog: Added / Changed / Fixed). Commit messages start with
  the version: `v0.4: Add AT command parser`. v1.0 is the first production release.
- **Tests for every feature, before deployment.**
  - Logic lives in `lib/` with **no Arduino or hardware calls**, so it can be
    unit-tested on the PC in `test/native/` (Unity). Hardware access goes through
    `lib/hw`.
  - Add or extend on-board tests in `test/embedded/` and end-to-end tests in `test/e2e/`
    (pytest + pyserial, `--console-port COMx [--rs232-port COMy]`).
  - Update the matrix in `test/README.md`.
  - Uploads run the native tests first and stop if any fail. Don't bypass this
    (`USR_MODEM_SKIP_TESTS=1`) unless Jon asks.
- **Pins only in `lib/hw/src/pins.h`.** The map is **confirmed** by Electronics (Q76,
  schematic rev A, 24 Oct 2026); `test_pins` locks the values in, so change both together
  and only when Electronics changes the board. A bench `.env` that changes pins must also
  set `USR_MODEM_PINS_OVERRIDDEN=1`, or the pin test fails and uploads stop. TTL side of the MAX3232 is inverted: asserted = LOW. DSR is hard-wired on.
- **No volume cap** (decided 7 Nov 2026): the CLS0361 2 W speaker takes the amp's full
  output (MAX98357A GAIN pin unconnected = 9 dB). Every sound still goes through
  `volume::outputGain()` (`lib/sounds/src/volume.h`). A smaller speaker or a higher amp
  gain needs a cap there again.
- **Secrets.** The Wi-Fi password is never printed, logged, exported or sent to the web
  page. The device password is shown in the boot banner until first use, and once on the
  requesting local serial port when `AT*PASS` generates a new one (brief 6.6; Jon, 11 Nov
  2026). It is never shown to the browser, never logged, exported or sent to the web page,
  and never shown on a remote (port 23) session. A remote caller (answer mode, port 23)
  can never reach command mode, `AT*` commands or the configuration screen (Q44). The
  network console (TCP 2001, behind the device password) counts as a local console and
  shows what the cable shows, password lines included (Jon, 13 Nov 2026).
- **Commands.** Standard commands follow USR Sportster behaviour; project commands use
  `AT*`. Everything in the configuration screen also has an `AT*` command (parity rule,
  6.8) and appears in `AT*HELP`.
- **Zimodem code** (Apache-2.0): keep its licence headers and mark every ported or
  changed file as changed (brief 8).
- **Style.** clang-format (`.clang-format`, 100 columns) and cppcheck must pass (CI
  checks both). LF line endings. Plain, short English in docs and console messages.

## Build, test, upload

PlatformIO's `pio` is on the PATH inside VS Code tasks and the PlatformIO terminal. In
a plain PowerShell terminal use `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`.
The PC unit tests need GCC: WinLibs (winget `BrechtSanders.WinLibs.POSIX.UCRT.LLVM`, which
also has clang-format) is installed for Jon's user and on the user PATH. End-to-end tests
use the Python in `%LOCALAPPDATA%\Pythonin` (pytest and pyserial installed).

| What | Command | VS Code task |
|---|---|---|
| PC unit tests | `pio test -e native` | Modem: run unit tests (PC) |
| Build | `pio run -e s3` | Modem: build firmware |
| Upload over USB (tests first) | `pio run -e s3 -t upload` | Modem: upload over USB (tests first) |
| Upload LittleFS (sounds + web) | `pio run -e s3 -t uploadfs` | Modem: upload sounds and web files (LittleFS) |
| Console (115200) | `pio device monitor -e s3` | Modem: USB console (115200) |
| Console over Wi-Fi | `pio device monitor -e s3 --port socket://usr-modem.local:2001` | Modem: network console |
| On-board tests | `pio test -e s3` | Modem: on-board tests |
| End-to-end tests | `python -m pytest test/e2e --console-port COMx` (or `socket://usr-modem.local:2001`) | Modem: end-to-end tests |
| Web tests | `cd web; npm test` | Modem: web interface tests |
| Lint | `pio check -e native` | Modem: lint (cppcheck) |

- Only one program can open a COM port. **Close the serial monitor before uploading**
  or running the end-to-end tests.
- On-board tests replace the firmware with a test program. Re-upload the firmware after.
- If an upload stalls at "Connecting…": hold BOOT, tap RST, release BOOT, retry.
- Ask Jon for the COM port numbers (console = the S3's UART USB-C port; RS232 = the
  Unitek PL2303 adapter) and for what is wired on the bench before testing hardware.

## Git workflow

- Branch from `main`: `firmware/vX.Y-short-name`.
- Commit when the native tests, build and lint pass. Push and open a pull request
  (`gh pr create`) unless Jon says otherwise. CI runs on every push; merging to `main`
  tags `vX.Y` and publishes a GitHub release with the binaries.
- Never force-push `main`. Don't commit `.pio/`, `data/www/` or `web/preview.html`.

## Hardware summary

- ESP32-S3 N16R8 DevKitC-1 clone: 16 MB flash, 8 MB octal PSRAM, Arduino core 2.0.17
  (ESP-IDF 4.4.7, `espressif32` pinned to `6.13.0` in `platformio.ini`). Partitions:
  2 × 4 MB OTA app slots, ≈7.9 MB LittleFS.
- UART0 (GPIO43/44, USB-serial chip) = maintenance console, 115200, `Serial`.
  UART1 = host RS232 via 2× MAX3232 (DCE). Native USB (GPIO19/20) kept for USB-JTAG.
- MAX98357A I2S amp (SD pin = shutdown), GAIN pin unconnected (9 dB), Same Sky CLS0361 8 Ω 2 W speaker, WH148 10 kΩ pot
  on ADC1, 7 front LEDs (AA CD RD SD TR CS ARQ; ARQ = Wi-Fi status). The S3 board's own
  RGB LED (GPIO47) is hidden in the case and not used from v3.0; GPIO47 stays reserved.
- Bench prototype uses a DB9 wired as DCE; the final build uses a DB25.
