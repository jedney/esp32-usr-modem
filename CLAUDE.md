# USR Modem Emulator: notes for Claude

ESP32-S3 Wi-Fi "modem" in a 3D-printed US Robotics Sportster replica, with a DB25 RS232 port, a USB modem port, the Hayes AT command set, dial/DTMF/handshake sounds and Telnet to BBSes. Windows 11 PC, VS Code + PlatformIO, repo at `C:\repo\esp32-usr-modem`.

## Where the requirements are

- **`docs/project-brief.md`**: the firmware spec: what the firmware does and the hardware it drives (pin map, interfaces). Keep it current with the code: change it in the same branch as the behaviour.
- **`docs/ROADMAP.md`**: what is left to do. Work on one item per branch.
- **`docs/decisions-log.md`**: the current decisions, one row each. Append every new decision or spec change made while coding here (date, what, why); replace a row when a decision changes rather than keeping the old one.
- `docs/COMMANDS.md`: the complete command reference. Update it with every command or option change, together with `AT*HELP` and the README.
- `test/README.md`: feature-to-test matrix. `web/PROTOCOL.md`: web page ↔ firmware protocol. `ui/DESIGN_SPEC.md`: the configuration screen design.
- `credits/LICENSES.md`: licensing record. Update it whenever third-party code, a library or a sound file is added or removed (`python scripts/generate_credits.py` rebuilds the About screen's list).

## Rules

- **Versioning.** Every branch bumps the version once: `python scripts/bump_version.py minor "summary"` (+0.1; 0.9 → 0.10), or `major` (+1.0) only for a major feature or a hardware change. Then rewrite the generated `CHANGES.md` entry properly (Keep a Changelog: Added / Changed / Fixed). Commit messages start with the version: `v3.1: Add something`.
- **Keep only what is material.** Code, comments and docs describe the current design: no version tags, review-finding IDs, "from vX" notes, migration code or superseded decisions. Stored layouts are number 1; anything else loads factory settings.
- **Tests for every feature, before deployment.**
  - Logic lives in `lib/` with **no Arduino or hardware calls**, so it can be unit-tested on the PC in `test/native/` (Unity). Hardware access goes through `lib/hw`.
  - Add or extend on-board tests in `test/embedded/` and end-to-end tests in `test/e2e/` (pytest + pyserial, `--console-port COMx [--rs232-port COMy]`).
  - Update the matrix in `test/README.md`.
  - Uploads run the native tests first and stop if any fail. Don't bypass this (`USR_MODEM_SKIP_TESTS=1`) unless the user asks.
- **Pins only in `lib/hw/src/pins.h`** (numbers come from `.env.example`). `test_pins` locks the values in, so change both together and only when the board changes. A bench `.env` that changes pins must also set `USR_MODEM_PINS_OVERRIDDEN=1`, or the pin test fails and uploads stop. TTL side of the RS232 Pal modules is inverted: asserted = LOW. DSR is hard-wired on.
- **No volume cap**: the CLS0361 2 W speaker takes the amp's full output (MAX98357A GAIN pin unconnected = 9 dB). Every sound still goes through `volume::outputGain()` (`lib/sounds/src/volume.h`). A smaller speaker or a higher amp gain needs a cap there again.
- **Secrets.** The Wi-Fi password is never printed, logged, exported or sent to the web page. The device password is shown in the boot banner until first use, and once on the requesting local serial port or authenticated browser terminal when `AT*PASS` generates a new one. It is never written to debug logs or exports, included in status messages, broadcast to other terminals or shown on a remote (port 23) session. Authenticated recent console history and network-console replay may include device-password lines printed on the maintenance console. A remote caller (answer mode, port 23) can never reach command mode, `AT*` commands or the configuration screen. The network console (TCP 2001, behind the device password) counts as a local console and shows what the cable shows, password lines included. Never print or echo `.env`; scripts that touch it print key names only.
- **Commands.** Standard commands follow USR Sportster behaviour; project commands use `AT*`. Everything in the configuration screen also has an `AT*` command (parity rule, spec 3.8) and appears in `AT*HELP`.
- **Framework.** Use what Arduino 3.3 and ESP-IDF 6.1 recommend; no shims for old APIs and no deprecated APIs in project code (`-Werror=deprecated-declarations`; IDF's warnings-as-errors apply to project code too). Read the current docs online rather than working from memory. Pins and versions are pinned in `platformio.ini`; a patch release of the same line is fine, a new minor or major needs the user's say-so.
- **Zimodem code** (Apache-2.0): keep its licence headers and mark every ported or changed file as changed (spec 4).
- **Style.** clang-format (`.clang-format`, 100 columns) and cppcheck must pass (CI checks both). LF line endings. Plain, short English in docs and console messages. In Markdown, write each paragraph or list item as one line and let the editor wrap it: no hard line breaks mid-sentence.

## Build, test, upload

PlatformIO's `pio` is on the PATH inside VS Code tasks and the PlatformIO terminal. In a plain PowerShell terminal use `$env:USERPROFILE\.platformio\penv\Scripts\pio.exe`; in the Bash tool neither `pio`, Python nor Node is on the PATH (`PATH="$PATH:$USERPROFILE/.platformio/penv/Scripts"`, Python in `"$LOCALAPPDATA/Python/bin/python.exe"`). The PC unit tests need GCC: WinLibs (winget `BrechtSanders.WinLibs.POSIX.UCRT.LLVM`, which also has clang-format) is installed for the current user and on the user PATH. End-to-end tests use the Python in `%LOCALAPPDATA%\Python\bin` (pytest and pyserial installed); the web tests need Node 26.

| What | Command | VS Code task |
|---|---|---|
| PC unit tests | `pio test -e native` (one suite: `-f native/test_<suite>`) | Modem: run unit tests (PC) |
| Build | `pio run -e s3` (about 3 minutes clean) | Modem: build firmware |
| Upload over USB (tests first) | `pio run -e s3 -t upload` | Modem: upload over USB (tests first) |
| Upload over Wi-Fi (tests first) | `pio run -e s3_ota -t upload` | Modem: upload over Wi-Fi / OTA (tests first) |
| Upload LittleFS (sounds + web) | `pio run -e s3 -t uploadfs` | Modem: upload sounds and web files (LittleFS) |
| Console (115200) | `pio device monitor -e s3` | Modem: USB console (115200) |
| Console over Wi-Fi | `pio device monitor -e s3 --port socket://usr-modem.local:2001` | Modem: network console |
| On-board tests | `pio test -e s3` | Modem: on-board tests |
| End-to-end tests | `python -m pytest test/e2e --console-port COMx` (or `socket://usr-modem.local:2001`) | Modem: end-to-end tests |
| Web tests | `cd web; npm test` | Modem: web interface tests |
| Script and UI tests | `python -m pytest test/scripts`; `python -m unittest discover -s ui/tests` | |
| Lint | `pio check -e native --fail-on-defect=medium --fail-on-defect=high` | Modem: lint (cppcheck) |
| Format | `clang-format --dry-run --Werror <files>` | |
| Version | `python scripts/check_version.py` | |

- Only one program can open a COM port. **Close the serial monitor before uploading** or running the end-to-end tests.
- On-board tests replace the firmware with a test program. Re-upload the firmware after.
- If an upload stalls at "Connecting…": hold BOOT, tap RST, release BOOT, retry. On the S3 board use the **UART** USB-C port; the native USB port is the modem's USB port.
- The first flash after a full erase is over USB. Wi-Fi tests that cut the connection (`AT*WIFI=OFF`, a join) need the cable, not the network console.
- Ask the user for the COM port numbers (console = the S3's UART USB-C port; RS232 = the Unitek PL2303 adapter; the USB modem port is its own COM port) and for what is wired on the bench before testing hardware. Things a person must see or hear (sound, LEDs, cables) are for the user.
- Setup credentials are in `.env`: `USR_MODEM_WIFI_SSID`, `USR_MODEM_WIFI_PASS` and `USR_MODEM_PASSWORD`. Use them only through the project's scripts and test fixtures (`scripts/load_env.py`, `scripts/ota_upload.py`, the e2e `conftest.py`).

### Build tips

- See every build error at once: `SCONSFLAGS=-k pio run -e s3`. `pio run -e s3 -t compiledb` writes `compile_commands.json` (delete it afterwards: the repo root does not ignore it).
- PlatformIO does not re-run CMake when only a component's `idf_component.yml` changes: touch `CMakeLists.txt` after editing one.
- IDF logs are off at run time (`esp_log_level_set("*", ESP_LOG_NONE)`); to debug a framework component enable its tag at `ESP_LOG_WARN` in a throwaway build.
- Never run the pioarduino platform against the shared PlatformIO install (it replaced PlatformIO Core once); use an isolated `PLATFORMIO_CORE_DIR`.

## Working on a task

- Branch, version bump, then for each item write or change the test first, make the change, and run only that suite while iterating. Run the full suite, the build, lint and clang-format once at the end.
- Keep the context small: grep for a function before reading a file, and avoid reading whole large files (`src/main.cpp`, `lib/config/src/menu.cpp`, the big `test_main.cpp` files). Pipe long output (`2>&1 | tail -30`).
- Keep CI runs few: CI runs on every push, so check locally first and push once.

## Git workflow

- Branch from `main`: `firmware/vX.Y-short-name`.
- Commit when the native tests, build and lint pass. Push and open a pull request (`gh pr create`) unless the user says otherwise. CI runs on every push; merging to `main` tags `vX.Y` and publishes a GitHub release with the binaries.
- Never force-push `main`. Don't commit `.pio/`, `data/www/` or `web/preview.html`.

## Hardware summary

- ESP32-S3 N16R8 DevKitC-1 clone: 16 MB flash, 8 MB octal PSRAM, Arduino core 3.3.12 on ESP-IDF 6.1.0 (`espressif32@8.0.0`, framework packages pinned in `platformio.ini`). Partitions: 2 × 4 MB OTA app slots, ≈7.9 MB LittleFS.
- UART0 (GPIO43/44, USB-serial chip) = maintenance console, 115200. UART1 = host RS232 via 2× RS232 Pal (MAX3232) modules (DCE). Native USB (GPIO19/20), also wired to the rear USB-C socket = the USB modem port (TinyUSB CDC); the built-in USB-JTAG debugger is therefore not available.
- MAX98357A I2S amp (SD pin = shutdown), GAIN pin unconnected (9 dB), Same Sky CLS0361 8 Ω 2 W speaker, WH148 10 kΩ pot on ADC1, 7 front LEDs (AA CD RD SD TR CS ARQ; ARQ = Wi-Fi status). The S3 board's own RGB LED (GPIO47) is hidden in the case and not used; GPIO47 stays reserved.
- Bench prototype uses a DB9 wired as DCE; the final build uses a DB25.
