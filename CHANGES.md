# Changelog

All notable changes to the USR Modem Emulator are recorded here.
The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versioning rules (see `README.md`):

- **+0.1** for small enhancements and bug fixes (0.9 is followed by 0.10, not 1.0).
- **+1.0** for a major feature or a hardware change.

Every change that is pushed bumps `VERSION` and adds an entry here
(`python scripts/bump_version.py minor "what changed"`).

## [Unreleased]

## [3.0] - 2026-11-13

- Fresh start on a new platform: Arduino core 3.3.12 on ESP-IDF 6.1, a network console on
  TCP port 2001, DCD, DSR and RI on the USB modem port, and a SHA-256 OTA password hash.
  Nothing from v2 is migrated: flash over USB after a full erase.

## [2.x]

- The USB modem port on the rear USB-C socket, and the review fixes of v2.1 to v2.17
  (never-blocking serial output, bounded web and call handling, a loop watchdog, a
  repainting configuration screen that sends only changes, and many more tests).

## [1.x]

- v1.0, the first production release (Hayes commands, Telnet and Internet calls, answer
  mode, configuration screen, web interface, Client Tests); v1.1 moved to the rev B pin
  map and the CLS0361 speaker with no volume cap.
