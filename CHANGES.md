# Changelog

All notable changes to the USR Modem Emulator are recorded here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

Versioning rules (see `README.md`):

- **+0.1** for small enhancements and bug fixes (0.9 is followed by 0.10, not 1.0).
- **+1.0** for a major feature or a hardware change.

Every change that is pushed bumps `VERSION` and adds an entry here (`python scripts/bump_version.py minor "what changed"`).

## [Unreleased]

## [3.0] - 2026-11-13

A fresh start on a new platform. Nothing from v2 is migrated: flash over USB after a full erase.

### Added

- Basic SSH destinations for BBSes that only offer SSH (hobby use only: the host key is not checked, password login only): phonebook type `SSH`, and a direct dial to port 22 tries SSH first.
- A browser modem terminal (xterm.js) with its own AT session, Telnet window size that follows the browser, a menu bar and settings dialogs driven by shared typed menu metadata, an idle guard for browser calls, status toasts and recent console history.
- A login page with server-side sessions and Help > Log out, in place of HTTP Digest.
- A network console on TCP port 2001, behind the device password.
- DCD, DSR and RI on the USB modem port; the USB device is named for the selected model and keeps one COM port (its serial number is the Wi-Fi MAC).
- Firmware-update progress on the front LEDs, and one `Login:` console line for each web, network console and OTA login.
- Copyright, project link and US Robotics acknowledgement in About, the boot text and the login page; `AT*GITHUB`.

### Changed

- Platform: Arduino core 3.3.12 as a component of ESP-IDF 6.1.0, TinyUSB 0.21.0 from the component registry, PSA Crypto random numbers and hashes.
- The configuration screen is a wrapping letter list in plain text or ANSI colour, the same menus as the browser.
- OTA uses a SHA-256 password hash; the device record keeps only that hash.
- The generated call tones (dial, ring, busy, carrier, DTMF) play at the handshake recordings' level.
- `FAX` is a documented name for the ARQ LED in the hardware tests.
- The project is licensed under Apache-2.0; third-party credits are in `CREDITS.md`.

### Removed

- The on-board RGB LED, the always-true `size` flag in the web `hello` message, the roadmap and the work-package plans.

### Fixed

- A browser loading the page opened many connections at once and the web server reset some of them; the server now has 12 connection slots and 40 active TCP connections and no longer queues its own close after each response.

## [2.x]

- The USB modem port on the rear USB-C socket, and the review fixes of v2.1 to v2.17 (never-blocking serial output, bounded web and call handling, a loop watchdog, a repainting configuration screen that sends only changes, and many more tests).

## [1.x]

- v1.0, the first production release (Hayes commands, Telnet and Internet calls, answer mode, configuration screen, web interface, Client Tests); v1.1 changed the pin map and the CLS0361 speaker with no volume cap.
