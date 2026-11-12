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

## [2.16] - 2026-11-13

### Changed

- Configuration screen code tidied (MNT-04), with no change in behaviour: one key handler
  per screen mode (menu, form, confirmation, output, help, export, captured command)
  behind a short `Terminal::key()`; the frame built from header, body and footer steps;
  the main-menu layout, phonebook side panel and export step in their own functions;
  one function per command in `commands.cpp`, found through a table. No function in
  `menu.cpp` or `commands.cpp` is over 75 lines. The configuration screen tests pass
  unchanged.

## [2.15] - 2026-11-13

### Added

- **Ctrl-L** in the configuration screen draws the whole screen again.

### Changed

- The configuration screen sends only the cells that changed since the last frame
  (PERF-01). An arrow key at 80x25 was 3.0 KB (5.1 KB in RGB colour), 12-21 s at
  2400 bit/s; it is now 68 B (102 B), under half a second. A full frame is still sent
  first, after a resize, a style, colour or theme change, Ctrl-L, and after anything else
  wrote to the port (for example a hardware test run from the screen). Plain-text style
  still sends the whole screen on every key.
- "Input too long" shows as soon as a character is dropped, not only at Enter.
- Tests that started a fresh terminal view part-way through now keep one view from the
  start, as a real terminal does.

### Fixed

- Output and help pages on terminals taller or shorter than 25 rows repeated lines when
  paging (5.5-14).
- Alt+letter (Esc and a key together) cancelled a form or left the screen, then ran the
  letter as a hotkey. It is now ignored (5.5-14).
- A duplicate condition in the screen footer (cppcheck, MNT-05j).

### Bench checks for Jon (optional)

- PuTTY at 2400 bit/s on the RS232 port: `AT*CONFIG`, move around the menus, open a form,
  press Ctrl-L. Also run `AT*HWLED` from Hardware Tests and check the screen is redrawn
  in full when it ends.

## [2.14] - 2026-11-13

### Changed

- The web page's status messages are built only while a browser is connected; a browser
  that connects after none gets all of them at once. The clock's UTC offset is cached and
  worked out again once a minute or when the time zone changes (PERF-02).
- `src/main.cpp` tidied (MNT-04): one hardware-test terminal class for the RS232 and USB
  ports, one helper for each repeated block, the firmware's `AT*` commands in a table,
  and `pollBrowser`, the web status and `setup()` split into named steps. The Internet
  dialer is polled once per loop pass instead of twice. No other behaviour change.

### Fixed

- A dial chosen in the browser's configuration screen said "Dialling on the RS232 host
  port." and then did nothing when that port was busy. It now checks first and says the
  port is busy (MNT-05a).

### Bench checks for Jon (optional)

- `ATI`, a call, `AT*CONFIG`, the web page (status, `E`, a dial from the phonebook) and
  `AT*HWLED`.

## [2.13] - 2026-11-12

### Added

- `test/native/support/fakes.h`: one in-memory settings store, one text sink and one call
  sound for the PC unit tests, each able to fail on demand (TG-16).
- Tests the drifted copies were hiding: a sound that cannot start, or breaks while playing,
  ends a call with NO CARRIER at the dial tone, digits and handshake stages and frees the
  line (`test_calls`); a refused store write for the phonebook, the network name, the modem
  profile (`&W`) and the device password reports an error and changes nothing
  (`test_config_services`).

### Changed

- `test_answer`, `test_calls`, `test_client`, `test_config`, `test_config_services` and
  `test_internet` use the shared fakes instead of their own copies (tests only; no firmware
  change). Suites still on their own copies are listed in `test/README.md`.

## [2.12] - 2026-11-12

### Added

- Tests for each fix below (native: `test_netcfg`, `test_web`, `test_config_services`,
  `test_modemcfg`).

### Changed

- `AT*WIFISCAN`, `AT*WIFIJOIN` and `AT*WIFIIP` give `ERROR` with "A call is in progress;
  hang up first" while a call is up on any port, instead of dropping the call without a word
  (5.5-05).
- Wi-Fi passphrases may contain UTF-8 text (8-63 bytes; control bytes still refused)
  (5.5-15).
- Network names with accents or symbols reach the web page as the letters, not as mojibake;
  bytes that are not valid UTF-8 are still escaped (5.5-16).

### Fixed

- `AT*NTP` refused nothing but a leading or trailing dot; `a..b` is now refused (5.5-15).
- After `AT*WIFISCAN` with Wi-Fi off, the radio is switched off again when the scan ends,
  fails or is cancelled (5.5-01).
- `AT*PING`: the 10 s limit no longer includes the DNS lookup, and replies that arrived
  count as a success even if a probe was still out at the limit (5.5-07).
- Wi-Fi link state: state and reason change together under a lock, and a late "got IP"
  event after `leave()` or `off()` cannot bring the link back up (5.5-02).
- A second `AT*WIFISCAN` after a cancelled one joins the scan still running instead of
  stopping it, so a late "scan done" event cannot end the new scan with a short list (5.5-03).
- `AT*FACTORY` also clears the Wi-Fi driver's own saved network and password
  (`nvs.net80211`), which firmware before v0.6 left there (5.5-19).
- The call history file: after power loss between the two renames of a save, the newest
  complete blob (the temporary file) is recovered, not the older backup (5.5-06).

### Bench checks for Jon (hardware code that cannot run on the PC)

- Join a network, `AT*WIFISCAN` with Wi-Fi off: the RGB Wi-Fi LED stays off and the list appears.
- Start `AT*WIFISCAN`, press a key to cancel, run it again at once: the full list appears.
- `AT*PING` a host name that resolves slowly: three replies, `OK`.
- `AT*FACTORY` on a board that ran firmware before v0.6: no saved network afterwards.
- Pull power during a call-history save (hard to time): the history is the newest or the
  previous one, never empty.

## [2.11] - 2026-11-12

### Added

- `web/src/upload_flow.js`: the browser upload flow (preflight, confirmation, login refresh,
  sending, result) with every browser call passed in, so Node tests it (TG-14).
  `web/test/upload_flow.test.mjs` covers each step and each failure, including a real
  conflicting-identity image. The web file list now includes it in `build_web.py`,
  `web_service.cpp` and the preview (firmware build needed to serve it).
- `test/scripts/test_web_file_lists.py`: the build list, the served list, what the page
  loads, the preview and the end-to-end asset check must name the same files (MNT-05h).
- Opt-in end-to-end cases (collected only; not run here): `--flood-tests` (fourth browser
  refused, idle floods on ports 23 and 80), `--upload-tests` (a cut-off upload frees the
  modem), `--usb-unplug` (USB unplugged during a call) (TG-15).

### Changed

- The upload button is disabled from the first press until the result, so a second press
  during the image check no longer starts a second upload (5.5-17).
- After a successful update the page reloads once the modem answers again, so new web
  files are used (5.5-17).
- A browser that gets 503 (update in progress) from the login request asks again every
  30 seconds, not every few seconds (5.5-17).
- A connection dropped while the person reads the upload prompt now stops the upload with
  a message instead of sending an empty CSRF token.

### Fixed

- The demo page drew `?????` for frames and rules; it now draws the same double-line frame
  as the firmware (restored as Unicode escapes) (MNT-05b).

## [2.10] - 2026-11-12

### Added

- Tests: a table of malformed PPP frames (LCP, IPCP, PAP and IP lengths, options that run
  past the end, unknown codes, bad FCS, oversize frames) at each stage of the negotiation,
  each checked for the right drop, reject or silence and for a good negotiation after it
  (TG-03). A table of bad XMODEM/YMODEM blocks (repeat, skip, complement, CRC, block 0
  names and sizes, the 16-file batch limit, cancel sequences) and of bad ZMODEM headers
  and subpackets (CRC, format, type, cancel, wrong positions, ZFILE metadata). The X-1K
  "wrap" test now sends more than 256 blocks, so the block number really wraps.

### Changed

- ZMODEM: a receiver that gets ZFIN but no "OO" ends as a success after 2 s, not as a
  time-out after about 55 s (5.5-10).

### Fixed

- PPP: an IPCP Terminate-Request no longer starts a fresh 30 s negotiation time-out
  before the link has been up, so a peer cannot keep a half-open link for ever (5.5-09).
- X/Y/ZMODEM: stray bytes from the peer no longer postpone the retry and time-out
  counts; only bytes that belong to a block, header or expected answer do (5.5-10).
- Client Tests size probe: the reply is found even when the terminal echoes the query
  back first (5.5-11).
- `transfer.h` carries the Apache-2.0 notice and the "changed for USR Modem" mark like
  `transfer.cpp` (MNT-05e).

## [2.9] - 2026-11-12

### Added

- Test (no firmware change): a canary sweep stores a Wi-Fi password and a device password,
  then runs every `AT*` command (read forms), `AT&V`, `ATI0`-`ATI7`, `AT*EXPORT`,
  `AT*STATUS`, the logs and every configuration screen, and fails if either password,
  its OTA hash or its Digest hash appears in any output, in plain, lower or upper case.
  New commands are covered because the sweep walks the command table (TG-02).
- Tests for the web login guards: an unterminated quote, the 12-parameter and 900-byte
  limits, the 8-nonce table (the ninth evicts the oldest), a falling nonce count, a real
  MD5 (RFC 1321 and RFC 2617 vectors) with a Digest login end to end, and filesystem
  image checks (exact size, marker, version) (TG-05).
- Tests for the host port: the `+++` guard time, the DTR hold and the throttle credit
  across the 2^32 ms clock wrap, the throttle at 300, 2400 and 115200 bps, and S3/S5 at 0
  and 127 (TG-06).

## [2.8] - 2026-11-12

### Changed

- End-to-end tests (no firmware change): every serial port has a 5 s write time-out and
  every test a 300 s time-out (`pytest-timeout`, `test/e2e/pytest.ini`), so a silent
  modem fails a test instead of hanging the run.
- The default end-to-end run no longer dials a real BBS or needs the Internet: the
  "any key cancels a dial" test dials 192.0.2.1 (never answers); the DNS lookup in the
  NAT test needs the new `--live-internet`.
- `--device-tests` stops at once if no valid `USR_MODEM_PASSWORD` is in `.env` and
  `--save-password` is not given (the random password would be lost).
- The masked Wi-Fi join test is skipped when the modem has another network saved.
- The on-board `test_board` uses a scratch settings namespace, not the modem's own.

### Fixed

- Failed e2e asserts no longer print the Wi-Fi password echo.
- The bridge test always sends Ctrl-X; the bulk echo test restores the write time-out.
- `test_internet.py` looked for `NO DIALTONE`; the modem prints `NO DIAL TONE`.
- Two `test_hwtest` assertions could not fail; they now check for clipping and the
  unclamped gain.

## [2.7] - 2026-11-12

### Added

- A configuration screen with no key for 5 minutes closes itself and frees the line.
  A serial port prints "Screen closed after 5 minutes idle."; the browser returns to
  its read-only view.

### Changed

- `S3`, `S4` and `S5` accept 0-127, and `S3` can't be a printable character. A stored
  profile that breaks the rule falls back to factory settings.
- `AT*SETTING=` takes exactly one setting command; `AT*SETTING=E0DT555` now gives `ERROR`.
- A damaged phonebook is no longer replaced by the next edit: `AT*PHONEBOOK=` and
  `AT*PHONEDELETE=` give `ERROR` until `AT*BOOKCLEAR`.
- Table sizes are checked at build time (settings, setting help, client-test sizes).

### Fixed

- Esc in the configuration screen now cancels a scan, ping, DNS lookup or sound
  started from it.
- A failed read of the stored settings is treated as damaged settings.
- Stale comments in `connection.h` and `atcmd.cpp`.

## [2.6] - 2026-11-12

### Changed

- Every browser starts read-only (status view and lights). Opening the page no longer
  takes the configuration screen, so a forgotten tab can't turn callers away or make
  `ATD` give `BUSY`. Press `E` (or **Enter configuration**) to open it when the modem is
  free.
- A wrong web password no longer makes the web server sleep for a second. Instead, every
  login attempt in the next second gets `401` at once without checking the password.
  A request header must now arrive within 1 s (was 3 s).

### Fixed

- A browser upload answered by the modem just after the web server's 2-second wait no
  longer leaves the modem stuck in "uploading" (the main loop stopped for good). A late
  LittleFS unmount request after a failed upload no longer unmounts LittleFS again.
- `web/PROTOCOL.md`: `wifi` is sent every 10 s in every state; `npm test` runs five
  suites; HTTP statuses 404 and 503 are listed.

### Added

- `webinterface::UploadHandshake` and the Digest lock-out, with unit tests in `test_web`
  for late answers on both sides. End-to-end tests (not run yet) for the read-only start
  and the lock-out.

## [2.5] - 2026-11-12

### Fixed

- After a failed Wi-Fi join from the browser, later browser changes (host name, static IP)
  were answered `OK` and then undone a moment later. A finished join result nobody collected
  is now cleared when the browser goes and when the next browser change starts.
- `AT*FACTORY` can finish when LittleFS is not mounted: "not mounted" now counts as
  "nothing to erase". Before, the reset stopped part-way and repeated at every boot.
- `AT*WIFISCAN` shows control bytes in a network name (escape sequences, bell) as `?`, so
  a neighbour's name can't clear the screen or fake a prompt.
- `AT*WIFI=OFF` and `AT*WIFIFORGET` inside the browser's 60-second window no longer save
  the browser's unverified change: they start from the settings before it.

### Added

- Tests for all four fixes in `test_netcfg` and `test_devicecfg`.

## [2.4] - 2026-11-12

### Added

- An incoming call that nobody answers is dropped after 8 rings (about 24 s): the socket
  is closed, the line is freed, the ring sound and RI stop, and the call log records it.
  `S0` above 8 counts as 8, so it answers on ring 8. The limit is fixed (no `AT*` command).
- TCP keepalive on every call socket (idle 60 s, then 6 probes 10 s apart). A peer that
  vanishes without hanging up now ends the call with `NO CARRIER` after about 2 minutes.
- Tests: the remote `+++` with guard times on the console, RS232 and USB answer ports; a
  mixed Telnet `DO`/`WILL`/`DONT` storm and two Telnet ends settling; reading stops when
  the send queue is full; Telnet commands from an answer-mode caller before and after `ATA`;
  `S7` during the handshake sound.

### Changed

- The ring count no longer climbs to 255: it stops at 8, where the call is dropped. The
  help for `S0` (configuration screen) says so.

### Fixed

- `S7` no longer drops an outgoing call whose connection is already open while the
  handshake sound is still playing (a short `S7` gave `NO ANSWER` on a good connection).

## [2.3] - 2026-11-12

### Changed

- `AT*HWLED` with no argument, `=CHASE` and `=WALK` are refused during a call (`ERROR`).
  Single LEDs, `ALL`, `OFF`, `NONE` and `AUTO` still work. The rule now lives in
  `hwtest::allowedDuringCall()`, with native tests.
- `AT*BAUD=` from the console, USB or browser is refused while a call is up on the RS232
  port. From the RS232 port itself it works as before.

### Fixed

- A hardware test can no longer run for ever. Every test that waits for a key (LED
  patterns, tones, DTMF, pulse, pot, lines, bridge, the guided test prompts) now stops
  after 5 minutes with no key, and as soon as its terminal goes away: DTR drops on the
  RS232 or USB port, or the browser closes. Before, `AT*HWTONE=DIAL` on a closed USB
  terminal played until a key or a power cycle, and `AT*HWLED` on the console froze a call
  on RS232.
- When the browser leaves, it releases only the LED, amp and RS232-output holds it set
  itself. Before, it also cancelled holds made from the console, USB or RS232.
- Tidy-ups: the command-name check in `hwconsole.cpp` tests the length before reading the
  character (cppcheck note); the `hwconsole.h` header comment describes the current
  behaviour.

## [2.2] - 2026-11-12

### Fixed

- A serial port that nobody reads no longer freezes the modem. Before, every `RING` (and
  every other result) waited up to 2 s for room on an RS232 port with RTS off, or a USB
  port held open by a program that doesn't read. Now `RING` is dropped at once when the
  port has no room; a reply waits up to 250 ms, then the port is marked stalled and later
  output is dropped at once until the computer reads again (STA-01).
- Read buffers on the RS232 port and the maintenance console, and the RS232 speed-detection
  capture, are wiped after use, as they can hold a typed password (SEC-07e).
- If RS232 speed detection can't start, the port now keeps its current speed and the
  console says so, instead of ignoring everything typed; a failed buffer allocation is no
  longer used (5.5-13).

## [2.1] - 2026-11-12

### Added

- `USR_MODEM_PINS_OVERRIDDEN=1` setting: a `.env` that changes the pin map on purpose must
  set it, or the pin-lock test fails.
- Pin rules (`scripts/pin_rules.py`): GPIO 22-25, 26-37 and 19/20 are rejected, and the
  volume pot must be on ADC1 (GPIO 1-10).
- Script tests for pin rules, `.env` BOM/UTF-16, LF line endings from `bump_version.py`,
  hotkeys and the download check.

### Changed

- CI and release actions are pinned to commit SHAs; `platformio` and `pytest` are pinned.
- Release runs only from `main`, does not keep the checkout token and no longer restores
  CI caches.
- `uploadfsota` runs the unit tests first, like the other uploads.
- The pin-lock test fails (instead of being skipped) when `.env` overrides the pins.
- Secrets rule (CLAUDE.md): `AT*PASS` may show a generated device password once on the
  requesting local serial port.
- README and the volume setting help no longer mention a volume limit.
- `REVIEW.md` is now `docs/history/v2-code-review.md`; review decisions are in the
  decisions log; `/build/` is ignored.

### Fixed

- The CI version job now fails on a bad VERSION or change log.
- CI cppcheck now also fails on "high" defects.
- A bad download is no longer cached before its SHA-256 is checked, and a bad cached copy
  is deleted.
- A `.env` saved with a BOM works; a UTF-16 `.env` gives a clear error.
- `bump_version.py` writes LF line endings on Windows.
- The configuration-screen hotkey check compares whole keys.

## [2.0] - 2026-11-08

### Added

- USB modem port (brief Q25): the S3's native USB, carried by the rear USB-C socket, is a
  USB serial port that works like the RS232 port, with its own AT session, configuration
  screen (`ATDT123`, `AT*CONFIG`), Client Tests record, PPP/SLIP and hardware tests. One
  call at a time across all ports. DTR from the PC program follows `&D`; flow control is
  USB's own, so nothing is lost when the modem is busy.
- `AT*ANSWERPORT=USB` and `AT*ANSWERPORT=AUTO`. AUTO answers on the first port with DTR
  on, in the order RS232, USB, console.
- `AT*STATUS` and the boot banner show the USB port's state.
- End-to-end tests for the USB port (`test/e2e/test_usb.py`, `--usb-port`), an on-board
  driver test (`test/embedded/test_usb`) and unit tests for the answer-port choice, the
  three client records and the two-port panel.

### Changed

- Factory answer port is now AUTO (was RS232). A saved choice is kept.
- The CD, TR, RD and SD lights follow either the RS232 or the USB port. CS stays RS232 only.
- Client Tests keep three records (console, RS232, USB). Records saved before v2.0 go back
  to defaults once.
- The `AT*TEST CABLE` refusal and the RTS-pause notes name the port instead of assuming
  the console.
- TinyUSB (Arduino's `arduino_tinyusb` component, pinned and hash-checked) is downloaded
  with the LittleFS sources by `scripts/network_dependencies.py`.
- The built-in USB-JTAG debugger is no longer available while the firmware runs, because
  the native USB port is the modem port. Flash over the UART port or OTA.

## [1.1] - 2026-11-05

Final v1.x build. Fully built, tested and working.

### Added

- Final case print files: `case/parts/` STLs, the two Bambu Studio print projects
  (0.4 mm main print, 0.2 mm fine parts), build photos and the screw and insert list
  in `case/construction/`, and front labels in `case/labels/`.
- `electronics/`: datasheets, pinout and models of the bought-in parts.

### Changed

- GPIO pin map updated to the rev B layout (project brief Q89): RS232 on GPIO1, 2, 38-42
  (right header), front-panel LEDs on GPIO4-7, 15-17 (left header). Audio (11-14) and
  the volume pot (10) are unchanged.
- GPIO pin numbers now come from `USR_MODEM_PIN_*` settings: defaults in `.env.example`,
  overridden by `.env` or real environment variables (`scripts/pin_config.py` passes them
  to the compiler; duplicates and non-GPIO numbers stop the build). `pins.h` no longer
  holds numbers. The pin-map lock test runs only when no override is set.
- Speaker is now the Same Sky CLS0361 (8 ohm, 2 W) with the amplifier's GAIN pin
  unconnected (9 dB). The -7 dB firmware volume cap for the old 0.25 W speaker is
  removed: full volume is now full scale, about 13 dB louder than before at the same
  knob position. `AT*HWTEST` step 3 is now "Full volume".
- RS232 now goes through two RS232 Pal modules; `AT*HWPINS` shows DB9 pin numbers.
  The rev A boot-state pull-ups (Q77) are not part of this map: Electronics to confirm
  the idle state of RXD, CTS, DCD and RI during boot.
- The old case files (`USRRasp-v5-*.stl`, `USR-Modem.3mf`, logo and badge) are replaced
  by the final files in `case/`.

## [1.0] - 2026-10-27

First production release, after a full code, security and documentation review.

### Added

- `docs/COMMANDS.md`: every standard command, S-register, `AT*` command, hardware test
  and special dial number, with values and factory settings.
- Web login slows down password guessing: a wrong password is answered after one
  second.

### Changed

- README rewritten as a v1.0 guide; obsolete per-version notes removed. The end-to-end
  and UI READMEs, the web protocol and the test plan are brought up to date.
- Earlier version plans and the pre-v1 review moved to `docs/history/`.
- The volume knob is read at most every 50 ms during a sound, instead of 16 ADC reads
  on every loop pass, leaving more time for serial and network work.
- The browser call status is rebuilt at most ten times a second instead of on every
  loop pass.
- The 31 forwarding functions in the AT* registry are generated by one macro.

### Fixed

- Web login no longer asks for the password again after a minute: an expired login
  nonce is now answered with `stale=true`, so the browser retries by itself.
- No `[E] ... does not exist` errors on the console at the first boot (missing call
  history) or when `AT*PLAY` names a missing file.
- `AT*SOUNDS` credited the old recordings; it now names Gough Lui's collection.
- Configuration screen messages no longer name the bench PC's COM ports or an old
  version number.
- The factory-reset end-to-end test waits for the modem to restart after its final OTA
  upload before checking Wi-Fi.

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
