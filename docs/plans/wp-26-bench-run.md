# WP-26 (v3.0): Bench run with Jon

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / medium |
| Size | M |
| Depends on | WP-20 to WP-25 |

Read [wp-19-common.md](wp-19-common.md) first (decisions, versions, board testing, rules).

The full bench run. Earlier sessions will have tested parts of it on the board already;
this session runs all of it on the final code. Check the ports first ("Testing on the
board" in the common file); Jon is needed for listening, watching the LEDs and changing
cables.

1. **Full flash erase, then USB flash** of v3.0 (`pio run -e s3 -t erase`, then upload and
   uploadfs). Boot banner shows a new device password; factory settings.
2. All on-board suites (`pio test -e s3`; re-upload the firmware after).
3. End-to-end: full `test/e2e` with `--rs232-port`, plus `test_usb.py`,
   `test_internet.py` (PPP, SLIP, stack headroom), `test_web.py --browser-upload
   --upload-tests`, X/Y/ZMODEM with lrzsz.
4. ArduinoOTA v3.0 to v3.0 with the SHA-256 hash; web firmware upload.
5. USB: rear USB-C empty and with a power-only lead for 5 minutes (no reset). DCD and
   RI on the USB port in a terminal program that shows them (WP-23), during a test
   call and an incoming call.
6. Sound at full volume and at the pot's ends; `AT*BAUD=AUTO` at 300, 2400, 9600, 38400,
   115200 with a 128-character line; then a test build without `uartSync()`.
   **Call-tone level:** with the pot and `ATL` fixed, place a test call at 14400 and
   listen to the dial tone, DTMF, ringback and busy against the handshake. They should be
   about as loud; if not, change `volume::kCallToneDb` (the generated tones are cut by
   10.5 dB) and say what was chosen. `AT*HWTEST` and `AT*HWPOT=TONE` stay at full scale.
   Pulse clicks (`ATDP`) are at full scale: say if they are too loud or too soft.
7. Wi-Fi join (WPA2, WPA3 if available, open), browser 60 s rollback, scan,
   `AT*FACTORY`. These and the on-board `test_wifi` cut the connection, so use the console
   cable, not the network console.
8. Heap and stack low-water marks with a call, the web page and PPP together; 30-minute
   soak with the watchdog on.
9. Network console (WP-22): log-in and lock-out, the password never shown, an OTA
   upload followed by the boot log over the network, and the soak in step 8 with a
   network console client connected.

10. By eye and ear, with Jon: every front-panel LED during a test call and an incoming
    call (ARQ shows Wi-Fi), and `AT*HWLOOP` with a loopback plug.
11. Web `hello` message (no `size` flag): the page still sizes the terminal in a large and
    a small browser window, and `test_web.py` passes.

The roadmap's other person checks (Amiga terminal, Windows 9x or Amiga over PPP and
SLIP, the DB25 case, a real crash record) need kit that is not on the bench. They stay in
`docs/ROADMAP.md` unless Jon has it at hand.

When it passes and Jon says so: one push and one pull request for the whole of WP-19
("Rules for this version" in the common file), then the last step of WP-25: delete
`docs/plans/`.
