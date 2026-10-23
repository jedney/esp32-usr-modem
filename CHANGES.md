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
