# Changelog

All notable changes to the USR Modem Emulator are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versioning rules (see `README.md`):

- **+0.1** for small enhancements and bug fixes (0.9 is followed by 0.10, not 1.0).
- **+1.0** for a major feature or a hardware change.
- **v1.0** is the first production version. Everything before it is prototyping.

Every change that is pushed bumps `VERSION` and adds an entry here
(`python scripts/bump_version.py minor "what changed"`).

## [Unreleased]

## [0.5] - 2026-10-24

### Added

- RS232 host port on UART1 (brief 6.2): the same AT parser and settings as the USB
  console. Each port has its own session with command, online and online-command modes.
- Speed detection from "AT" / "at" at 300-115200 baud, as on the Sportster. The RMT
  receiver records the TXD-in pin, and in command mode the typing is decoded from that
  recording, so the first "AT" at a new speed is already understood (`lib/hostport`
  autobaud). `AT*BAUD=<300-115200>` fixes the speed instead; `AT*BAUD=AUTO` (default)
  goes back to detection. Saved at once.
- Hardware flow control: `&H1`/`&H3` drop CTS when the receive buffer is full, `&R2`
  pauses sending while RTS is off (UART1's own RTS/CTS). Software flow control: `&I1`/
  `&I2` obey XON/XOFF from the computer, `&H2`/`&H3` send XOFF/XON.
- DTR (`&D0` ignored, `&D2` hang up with NO CARRIER, `&D3` hang up and reset, debounced
  by S25), DCD (`&C0` always on, `&C1` follows the call), RI wired and held off until
  answer mode (v0.9).
- `+++` escape with guard time (S2, S12; S12=0 no guard, S2>127 off), `ATO`/`ATO1` back
  online, `ATH0` and `ATZ` hang up, `F0` online local echo, `ATI4` shows OFF HOOK during
  a call.
- `AT*TESTCALL[=<seconds>]`: a test call to a built-in echo, so all of the above can be
  tried without a network; with `<seconds>` the far end hangs up (NO CARRIER).
- Front-panel LED driver (brief 5.4): AA, CD, RD and SD (each byte lit for 50 ms), TR,
  CS from the real CTS pin, ARQ (Wi-Fi, from v0.6). `AT*HWLED=AUTO` gives the LEDs back
  after a hardware test.
- `AT*HW...` tests also run on the RS232 port. As decided in Q79, the line, loopback and
  bridge tests say why they are unavailable there, and on the console they run only with
  no call up, pausing the RS232 session. `AT*HWSIG=AUTO` ends a held `AT*HWSIG`.
- Tests: 40 unit tests for speed detection (synthetic waveforms at every rate, clock
  error, jitter), escape timing, DTR, flow control, sessions and LEDs; an on-board test
  (`test_rs232`) that loops UART2 through the chip's own pins (no wires) to check
  detection at every rate, a 140-character line, CTS flow control with nothing lost, RTS
  and DTR; end-to-end tests for the console sessions (`test_session.py`) and for the
  RS232 port through the Unitek adapter (`test_rs232.py`).

### Changed

- `&B` now has the Sportster meaning (`&B1` = the serial speed stays at the speed of
  "AT", factory default; `&B0` = follows the call speed, from v0.8). The speed is always
  detected unless `AT*BAUD` fixes it.
- `&W` stores the port's current speed in the template (ATI5 `BAUD=`), as the Sportster
  does.
- Saved settings layout 2 (adds the `AT*BAUD` setting). Settings saved by v0.4 are
  converted, not reset: `&B` becomes 1, everything else is kept.
- The boot banner's host-serial line is now `RS232: speed ..., &B, flow control, &C &D`.
- ESP-IDF driver messages are no longer printed on the console.

### Fixed

- Changing UART1's speed or flow control at run time now takes effect on the S3:
  Arduino core 2.0.17 leaves the new settings unsynchronised (a change to 2400 baud ran
  at 1980).

## [0.4] - 2026-10-24

### Added

- AT command parser on the USB console (`lib/atcmd`), following the USR Sportster 33.6
  (brief 6.2, 6.5-6.7): several commands per line, `A/` repeat, `AT`/`at` only, S3/S5
  line end and backspace, `E F L M Q V X B`, `P`/`T`, `H`, `Sr=n`, `Sr?`, `Sr.b=n`,
  `AT=n`/`AT?`, `Y`, `Z0`-`Z5`, `&F0`-`&F2`, `&W0`/`&W1`, `&Z0`-`&Z3` stored numbers,
  `&A &B &C &D &G &H &I &K &M &N &P &R &S &T &U &Y`, and `+FCLASS` (data only; fax
  classes give ERROR). The first bad command stops the line with ERROR.
- Result codes with Sportster numbers and words, X0-X4 sets, verbal/numeric (`V`) and
  quiet (`Q`) modes, ready for the call results in v0.8.
- `ATI0`-`ATI7` and `ATI9` with the text of a real Sportster 33600/Fax V2.31; `ATI4`
  current settings, `ATI5` stored templates and numbers, `AT&V` both together.
- Settings store (`lib/modemcfg`): two stored templates (Y0, Y1), factory templates
  &F0-&F2, four stored numbers, kept in NVS with a version number and CRC; damaged or
  old saved settings fall back to factory settings, and the boot banner says which.
- Boot banner shows where the settings came from and the host-serial, echo, quiet,
  verbal and result-code settings (brief 6.7).
- Tests: 66 unit tests for the parser and settings; an on-board NVS test; end-to-end
  tests for echo, numeric and quiet modes, `A/`, `ATI`, `+FCLASS` and settings kept
  across a restart.
- `CLAUDE.md` (project rules for Claude Code in VS Code), `docs/ROADMAP.md` (remaining
  firmware versions v0.4-v1.0), `docs/decisions-log.md`, and a snapshot of the project
  brief in `docs/project-brief.md`.

### Changed

- `AT*` commands come from one table, and `AT*HELP` lists it. `AT*VER` and
  `AT*DIALTEST` output now follows the `V` and `Q` settings.
- Unknown-command end-to-end test uses `ATJ`: `ATXYZZY` is now valid (`X`, `Y`, `Z`).
- PC unit tests are linked statically on Windows, so they run during an upload, when
  PlatformIO's ESP32 compiler folder (with an older `libstdc++-6.dll`) is first on
  PATH.

## [0.3] - 2026-10-24

### Added

- Hardware tests for the electronics build, on the USB console (brief 6.14). `AT*HWTEST`
  is a guided walk-through (LEDs, speaker, volume cap, pot, RS232 lines, loopback) with a
  pass/fail summary. Single tests: `AT*HWLED`, `AT*HWAMP`, `AT*HWVOL`, `AT*HWTONE`,
  `AT*HWDTMF`, `AT*HWPULSE`, `AT*HWPOT`, `AT*HWLINES`, `AT*HWSIG`, `AT*HWLOOP`,
  `AT*HWBRIDGE` (console <-> RS232 pass-through), `AT*HWPINS`, `AT*HWINFO`, `AT*HWFILES`.
  `AT*HW?` lists them.
- GPIO pin map in `lib/hw/src/pins.h`. **Proposed, not yet confirmed by Electronics**; the
  only file to change if the wiring differs.
- Hardware layer `lib/hw` (LEDs, I2S audio for the MAX98357A, amp shutdown, pot ADC,
  RS232 handshake lines, UART1). Pins are only driven when a test uses them.
- Tone generator `lib/sounds`: UK dial, busy and ring tones, DTMF, UK pulse clicks, test
  tones and sweeps; volume curve with the -7 dB speaker cap applied last.
- `AT*HELP` and `AT*?` list the console commands; the boot banner names the reset reason.
- Tests: 24 unit tests for tones, volume and argument parsing; a pin-map check against
  the S3's reserved pins (runs before every upload); on-board driver tests (`test_hw`);
  end-to-end tests for the `AT*HW` commands, including a console <-> RS232 bridge test.
- CI also compiles the on-board tests.

### Changed

- VS Code tasks put PlatformIO's own `pio` on the PATH, so they work without a separate
  PlatformIO Core install.
- README: full first-time setup, build and upload steps for Windows.

## [0.2] - 2026-10-23

### Added

- Web interface page (`web/`): a full-window 80 x 25 terminal (xterm.js 6.0.0, IBM Plex Mono,
  VGA colour palette) for the configuration screen, with a status bar showing the live
  front-panel lights, connection status, remote host and IP, connection time, Wi-Fi, modem
  clock and the page-to-modem link. The WebSocket protocol the firmware will implement is
  in `web/PROTOCOL.md`.
- Demo mode (`index.html?demo`) and a single-file preview (`python scripts/make_preview.py`)
  that simulate the modem, so the page can be checked without hardware.
- `scripts/build_web.py` gzips the page into `data/www/` when the LittleFS image is built.
- Web unit tests (`cd web && npm test`), run by CI; CI also uploads the preview as an artifact.
- VS Code tasks for the web tests and preview.

## [0.1] - 2026-10-22

### Added

- Project scaffold: PlatformIO project for the ESP32-S3 N16R8 (Arduino core 2.0.x),
  16 MB partition table with two 4 MB OTA app slots and ~7.9 MB LittleFS for sounds.
- Build environments: `s3` (USB upload), `s3_ota` (ArduinoOTA upload) and `native`
  (unit tests on the PC).
- Automatic versioning: `VERSION` file embedded in the firmware with the git commit
  hash (`-dirty` for uncommitted builds); `scripts/bump_version.py` bumps the version
  and adds a change-log entry.
- Test gate: every upload (USB or OTA) runs the PC unit tests first and stops if any fail.
- Dial-string classifier (`lib/dialstring`) implementing the brief's dialling rules
  (zero-padded IP + port, hostnames, phonebook numbers, reserved numbers 123/666/667 and
  Zimodem's 5517546/5517545), with unit tests.
- USB maintenance console stub: boot banner with version, flash and PSRAM size;
  `AT`, `AT*VER` and `AT*DIALTEST <dial string>`.
- VS Code workspace: recommended extensions, settings, tasks; clang-format, EditorConfig,
  cppcheck via `pio check`.
- GitHub Actions: build, unit tests and lint on every push; tag `vX.Y` and a GitHub
  release with the firmware image on each merge to `main`.
- Test plan (`test/README.md`), on-board board check, end-to-end test skeleton (pytest).
- Licensing record (`credits/LICENSES.md`).
