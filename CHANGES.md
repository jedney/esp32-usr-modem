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

## [0.18] - 2026-10-27

### Added

- When dialling a hostname, look it up during the dial tone and play the digits of
  its IP address, then connect to that address. A failed lookup gives NO DIAL TONE.

### Fixed

- Pause for the connect delay after the handshake sound and before sending
  CONNECT, instead of holding data after CONNECT. The Sounds menu item is now
  "Delay before CONNECT".

## [0.17] - 2026-10-27

### Added

- Play 1500 ms of generated ringing after dialled digits and before starting TCP.
- Hold payload in both directions for 250 ms after handshake playback completes
  and CONNECT is sent, while continuing network and modem services.
- Add saved `AT*RINGDELAY` and `AT*CONNECTDELAY` settings (0-60000 ms), Sounds menu
  controls and configuration export. Migrate existing settings with the new defaults.
  Zero disables each delay; ATM0 retains immediate connection without audio delays.

### Changed

- Order the main menu shortcuts as A About, B Hardware Tests, C Client Tests,
  and D Command Reference across serial, browser and preview screens.

## [0.16] - 2026-10-27

### Fixed

- Stop repainting idle configuration menus every two seconds, avoiding flicker
  and repeated scrolling on consoles. Keep explicit live Status refresh, input,
  resize and model-change updates.

## [0.15] - 2026-10-27

### Changed

- Replace all packaged handshake recordings with Jon's supplied WAVs from Gough
  Lui's collection and update README, About and licensing attribution.
- Add a dedicated 9600 playback/download asset; ALL transfers now include seven
  handshake files. Package the supplied PCM WAVs unchanged with
  `scripts/build_handshakes.py`, replacing its removed MP3/OGG source dependencies.

## [0.14] - 2026-10-27

### Fixed

- Retain serial input when PPP reply pressure or a local-service transition stops
  a chunk partway through; retry its remaining bytes without loss.
- Keep the full browser network rollback window when the loop supplies an older
  timestamp. Reserve Wi-Fi joins throughout password entry on every local port.
- Give established PPP sessions a fresh deadline and retry budget when LCP/IPCP
  renegotiates.
- Reject failed settings saves without changing live configuration or the radio;
  distinguish missing NVS records from oversized or unreadable records.
- Share the playback/download sound catalogue, include HS56000.WAV in ALL batches,
  and preserve HS9600.WAV as an alias of its existing HS2400.WAV playback fallback.

### Added

- Native failure-path regressions, real scratch-NVS and packaged-sound checks,
  wired PPP pressure/renegotiation and cross-port Wi-Fi prompt checks, and complete
  Y/ZMODEM sound-batch verification against lrzsz.

## [0.13] - 2026-10-27

### Added

- PPP (`666`, `5517546`) and SLIP (`667`, `5517545`) Internet dial-up with a
  private /30 network and IPv4 NAPT through Wi-Fi.
- Saved Internet addressing/DNS, AT*INTERNET status and local management hang-up,
  shared menu actions, PPP/SLIP browser status and call-history kinds.
- Portable protocol, settings and lifecycle tests, on-board adapter checks and
  binary COM15/COM18 network acceptance.

### Changed

- Build Arduino 2.0.17 with pinned ESP-IDF 4.4.7 from source, enabling forwarding
  and NAPT while retaining 16 MB flash, octal PSRAM, 240 MHz and the OTA partitions.
- Fetch matching LittleFS sources at immutable revisions with SHA-256 verification.

### Fixed

- Binary calls suppress F0 online echo as well as software flow and Hayes escapes.
- Internet hang-up restores command mode and releases network resources without reset.

## [0.12] - 2026-10-27

### Added

- Serial Client Tests for display/charset, screen size, raw keys, terminal replies,
  echo/line endings, bell, configured format, timed throughput and RS232 signals.
- Incremental XMODEM checksum/CRC/1K, YMODEM batch and ZMODEM downloads/uploads,
  generated throughput files, immutable log snapshots and DOS-safe sound names.
  Uploads are validated and discarded; transfer statistics include retries and CRC.
- Confirmed client settings saved independently for console and RS232, with
  reboot retention, corruption defaults and save rollback.
- Sportster UI with 13 main options and 72 nested actions, 80x25 compact and wide
  layouts, four model themes, Phonebook selection, editable forms and safe keys.
- Adaptive browser geometry and a labelled demo using the shared catalogue;
  browser Client Tests are listed for reference and execute only on serial.
- Native, on-board, wired and independent lrzsz transfer checks.

### Changed

- Rename USR_MODEM_OTA_PASSWORD to USR_MODEM_PASSWORD for the shared OTA/web
  device credential. Update local .env/environment keys; the password is unchanged.
- Consequential menu changes require confirmation; secret buffers clear on
  cancellation, timeout, exit and disconnect.

### Fixed

- Binary diagnostics bypass menu key decoding, software flow bytes, guarded
  escape and unsolicited console notices; every temporary resource is restored.
- A diagnostic started after the loop timestamp no longer immediately times out.
- Host output uses nonblocking FIFO writes, keeping the modem responsive and
  counting RTS pauses while the computer stops receiving.
- The WebSocket acceptance peer retains partial frames across read timeouts;
  the missing-CSRF probe uses the current version rather than requesting a downgrade.

## [0.11] - 2026-10-26

### Added

- Authenticated HTTP and WebSocket configuration using the existing xterm page and
  shared terminal, with read-only browser status while a call or another menu owns
  the modem. Live panel LEDs, Wi-Fi, clock and call status follow the real hardware.
- Digest login using the existing device hash, single-use 30-second socket tickets,
  Host/Origin checks, CSRF-protected uploads and bounded browser queues. `AT*WEB`
  controls the saved listener setting; browser passwords remain write-only.
- Browser hardware tests, live line controls and RS232 bridge, with disconnect
  cleanup. Firmware and LittleFS image uploads verify project/version identity and
  SHA-256; filesystem images are staged before replacing the single partition.
- Image identity/manifest build checks and native, on-board, script and web tests.

### Changed

- Browser network changes commit after connection and revert on failure within
  60 seconds, independently of the browser socket. Configuration buffers use PSRAM
  where available. Serial command and menu behavior remains covered by regression.
- The Client Tests placeholder follows the reordered v0.12 roadmap milestone.

### Fixed

- Detect silent browser sockets after restart and reconnect after image uploads;
  restore the real panel LEDs when leaving browser hardware tests.
- Wired 300-baud acceptance now budgets for the full ATI4 profile response instead
  of timing out based only on the short command length.

## [0.10] - 2026-10-26

### Added

- Local BBS-style configuration through `AT*CONFIG` and reserved dial `ATDT123`,
  with ANSI/ASCII, CP437 and plain screens, paging, setting help and AT* parity.
- Status, phonebook, profiles, Wi-Fi/clock, update, sound and hardware-test screens;
  generated About credits and a command reference. Both local menus reserve the
  call slot; remote callers remain data-only.
- A 256 KiB PSRAM structured debug ring with bounded reads, persistent last-50-call
  history, reset/crash summary, cancellable DNS/ping and capped sound previews.
- Password-free configuration export with replay support for profiles, phonebook,
  network/device settings and final serial/response formatting.
- Native configuration/service tests, scratch on-board checks and serial acceptance
  tests. External RS232, real PC/Amiga observation and listening acceptance remain open.

### Changed

- Move the production AT* registry into shared, host-testable configuration data.
- Keep direct local menus separate from network carrier state and use UART input
  while a local menu owns the RS232 port. Retain existing settings layouts 1-4.
- Keep call history in CRC-checked LittleFS files with a previous valid backup,
  avoiding competition with a full phonebook in the small NVS partition.

### Fixed

- Prevent `F0` data echo from revealing password input in dialed configuration.

## [0.9] - 2026-10-25

### Added

- Incoming Telnet calls on port 23, manual ATA, S0 auto-answer, UK double ringing,
  RI and AA indication, with one shared incoming/outgoing call slot.
- Saved AT*ANSWERPORT=RS232|CONSOLE selection (default RS232), query/list and help.
- Incoming-call native, on-board and PC Telnet acceptance tests, including remote
  command/escape isolation, binary transfer, buffering and listener recovery.

### Changed

- Modem NVS layout 4 preserves layouts 1-3 and defaults their answer port to RS232.
  Live S1 ring counts are cleared when saving/restoring profiles.

### Fixed

- ATH and ATZ can reject a pending incoming call before carrier is established.
- Q2 suppresses incoming-call CONNECT/carrier-loss results while outgoing results
  keep their existing behavior.
- Simultaneous socket and Wi-Fi loss emits one NO CARRIER result; answered-call
  hang-up restores the host command speed once.

## [0.8] - 2026-10-25

### Added

- Outgoing ATD/ATDT/ATDP calls, ATDS0-3 and ATDL through nonblocking DNS/TCP,
  with a shared call slot, cancellation, S7 deadlines and modem error results.
- Telnet BINARY, ANSI and 80x24 negotiation, incremental IAC parsing, raw TCP,
  bounded queues/backpressure, guarded escape/resume and clean disconnects.
- A persistent 64-entry phonebook, entry 1 preloaded with Amiga Retro's hostname,
  AT*PHONEBOOK and AT*PHONEDELETE; included in recoverable factory reset.
- AT*MODEL=14400/28800/33600/56000, query/list commands and four Sportster series
  identities. Device-wide selection saves immediately and affects ATI/default
  CONNECT reporting only; active-call reporting stays stable.
- USR V.90 &N/&U indexes through 38 (56000), matching numeric/verbose CONNECT
  results, explicit speed precedence, &U floor and optional &A result details.
- Saved AT*THROTTLE policy, bidirectional rate limiting, and outgoing &B0/&B2
  serial speed following independently of model reporting.
- Streaming tone/digit/WAV call audio, strict 8 kHz mono PCM WAV validation,
  live pot/ATL/ATM control, amp shutdown and the existing -7 dB final volume cap.
- Licensed 1200-baud recording and the approved public-domain generic fallback
  for five other speed files, with original sources and reproducible conversion.
- Native call/model/storage/Telnet/audio tests, on-board integration checks and
  a local-server serial acceptance harness. Future config/web model presentation
  requirements remain documented for v0.10/v0.11.

### Changed

- Factory identity is Sportster 14.4k Series and operational speed remains 14400.
  All speeds/features through 56k remain selectable; host serial reaches 115200.
- Modem settings layout 3 stores explicit speed flags, model and throttle metadata;
  layouts 1/2 migrate without discarding saved settings.
- Firmware and filesystem uploads both run the native-test gate.

### Fixed

- TCP resets during connection setup report BUSY, including lwIP's ECONNRESET
  form of active refusal; resets after connection still report NO CARRIER.
- AT*PASS= leaves the equals sign visible and masks only the following value,
  retaining secret buffer wiping, validation and the A/ repeat prohibition.

## [0.7] - 2026-10-25

### Added

- Device credentials: random 12-character first-boot password, USB-console boot display
  until first use or change, and OTA/HTTP Digest hashes in their own NVS record.
  `AT*PASS` generates a replacement shown once; `AT*PASS=<text>` accepts 8–32 printable
  characters. Secret input is masked, wiped and excluded from `A/` repeat.
- Authenticated ArduinoOTA with `AT*OTA?`, saved `ALWAYS`/`DEMAND` policy, and `ON`/`OFF`
  for a 10-minute demand window. Listener follows Wi-Fi, credentials and idle state;
  first authenticated use removes the temporary plaintext password.
- `AT*FACTORY`: local Y/N confirmation, 30-second timeout, project-settings wipe and
  reboot with a fresh password. A durable marker recovers interrupted resets.
- 26 native lifecycle/command/reset tests, five on-board checks (including real MD5
  vectors and offline password generation after pot ADC initialization), opt-in
  serial/OTA acceptance tests, and three host uploader tests run in CI.

### Fixed

- The device password (OTA and web) stays out of process arguments and PlatformIO/espota
  debug output. The upload hook reads `.env` or the environment and invokes the installed
  uploader in memory, preserving native tests before every upload.
- End-to-end timeout failures omit raw boot output and command text that can contain
  credentials.

## [0.6] - 2026-10-24

### Added

- Wi-Fi (brief 6.6): one saved network, joined at boot and rejoined when it drops
  (retries after 5, 10, 20, 30 s, then every minute). `AT*WIFISCAN` lists the networks in
  range; `AT*WIFIJOIN=<n|"name">` joins one, asking for the password at a masked prompt
  (each character shows as `*`; it is never on the command line, so `A/` doesn't repeat
  it). The network is saved only once it connects; otherwise the old one is rejoined.
  `AT*WIFI?` shows the state, network, signal, address, MAC and name, and the password
  only as "set" / "not set". `AT*WIFI=ON|OFF`, `AT*WIFIFORGET`.
- Static IP: `AT*WIFIIP=<ip>,<mask>,<gateway>[,<dns>]`, `AT*WIFIIP=DHCP`.
- mDNS name, `usr-modem.local` by default: `AT*HOSTNAME=<name>`.
- Clock from NTP, shown in New Zealand time by default: `AT*TIME`, `AT*TZ=<zone|POSIX
  rule>` (`AT*TZ=?` lists the zones), `AT*NTP=<server>` (default `pool.ntp.org`).
- Wi-Fi lights: the front ARQ LED is off with no network set up, blinks while connecting
  and is on when connected; the on-board RGB LED is red, flashing green and green.
  Both are off when Wi-Fi is switched off.
- The boot banner shows Wi-Fi, IP, network name and the clock, waiting up to 8 s for the
  network. Later changes (connected, lost, clock set) are printed on the USB console
  between commands, never on the RS232 port.
- Wi-Fi settings are stored on their own (NVS namespace `net`), so `AT&F`, `ATZ` and
  `&W` don't touch them. The ESP-IDF's own copy of the network is switched off.
- `.env` (git-ignored, see `.env.example`) holds the Wi-Fi details for the end-to-end
  tests and the device password (OTA and web) for `s3_ota` uploads.
- Tests: 42 unit tests (`test_netcfg`: settings, checks, time zones and NZ daylight
  saving, retry rules, lights, join and scan, every command, and a check that the
  password never appears in any output), 6 for the masked prompt and late results
  (`test_atcmd`); on-board `test_wifi`; end-to-end `test_wifi.py` (join, `ping
  usr-modem.local`, clock against the PC, password never shown).

### Changed

- Pin map confirmed by Electronics (Q76, schematic rev A): I2S LRC GPIO11 and BCLK
  GPIO12, pot GPIO10, LEDs AA CD RD SD TR CS ARQ on GPIO1, 2, 42, 41, 40, 39, 38.
  `AT*HWPINS` says "confirmed"; a unit test locks the values in.
- AT commands can now ask for a secret at a masked prompt and send their result later,
  so a Wi-Fi join or scan doesn't hold up the other port (any key cancels).

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
