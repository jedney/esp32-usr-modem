# Web interface: files and protocol

The page is an adaptive **modem terminal**, minimum 80 × 25 and up to the firmware limit of 65535 × 65535 cells (xterm.js), with a **status bar** along the bottom. It fills the available window at a readable font size and fits the font on smaller windows. The page automatically requests one terminal session on first opening. It never takes over another browser or a modem reserved by a serial call, incoming call, menu, test or update. An idle browser terminal does not reserve the call slot, so RS232 and USB can still dial or answer. After loss, reboot, refused access or idle expiry, the centred overlay says "Session Disconnected" and "Click the Terminal area, or select a menu item to reconnect". Transport status reconnects automatically; terminal access retries only on user action. There are no open/close buttons; close the tab to release a terminal. A menu selection waits for granted access and firmware readiness before continuing.

| Item | Source |
|---|---|
| Front-panel lights AA, CD, RD, SD, TR, CS, ARQ/FAX (live) | `led` messages |
| Connection status (Idle, Dialling, Ringing, Connected · Telnet · 33600, Internet (PPP)…) | `call` messages |
| Remote host and IP (`www.amigaretro.com (170.64.142.64:23)`) | `call` messages |
| Connection time | `call.since` and the modem clock |
| Modem Wi-Fi network and signal | `wifi` messages |
| Modem time (the modem's time zone) | `clock` messages, ticked by the page |
| Page ↔ modem link (Online / Offline / Connecting) | WebSocket state |

The compact menu stays on one row, moving overflow entries into More. The terminal header holds the call indicator, remote destination, duration and call-only Hang up button; the top badge shows the page-to-modem link once. The bottom status bar holds the LEDs, one guidance message, a Wi-Fi icon and the clock. Wi-Fi arcs increase with signal strength; hovering shows the network, dBm and address. The clock shows time with the full date on hover. Narrow windows hide secondary call details and shorten guidance, retaining the Wi-Fi icon and time. Light and dark themes follow the browser preference.

The shared configuration terminal prints compact wrapping lists in plain text or ANSI colour, with letter selection, friendly names and current values. Menu entries and enum choices omit explanatory help; browser menu entries retain it as hover tooltips rather than a second line. Input prompts retain units and limits. No cell grid, cursor positioning or box drawing is emitted. Settings selects the live Sportster model through `AT*MODEL=14400|28800|33600|56000`; the factory model remains 14.4k. Model selection changes identity and default CONNECT reporting, while explicit phonebook/`&N` speeds and a current call's reporting remain stable. `?demo` previews the same inventory using synthetic data and never invokes device services.

## Files

`web/src/` holds the page and the cropped, transparent dark red (#8B0000) `usr-logo.png` masthead artwork (29 px high, 24 px on narrow screens, preserving its aspect ratio); `web/vendor/` holds third-party files (xterm.js 6.0.0, MIT; IBM Plex Mono, OFL-1.1). `idle.js` holds the timestamp-based call idle policy and `toast.js` the reusable passive notification stack. `menubar.js` builds and validates commands, `menu-view.js` renders disclosures and dialogs, `menubar.css` defines the visual system and `command-channel.js` tracks consumed input and bounded command replies. `scripts/build_web.py` gzips all 22 assets into `data/www/` when the LittleFS image is built (`pio run -e s3 -t buildfs` or `-t uploadfs`). The firmware serves `data/www/<file>.gz` with `Content-Encoding: gzip`.

Assets are compressed into the existing LittleFS image, including the generated `ui-data.js` catalogue for the explicitly synthetic browser demo and `ui-commands.js` for typed command metadata. Both JavaScript catalogues are static assets behind the same login as the page. `ui-commands.js` exports the 91 menu bindings with friendly text, types, ranges, choices and raw command templates generated from `lib/config/ui-bindings.json`; it contains no live setting values or credentials. `ui/DESIGN_SPEC.md` defines the schema and unit conversion. These assets add no WebSocket messages or ownership changes.

## HTTP endpoints

| Method and path | Auth | Purpose |
|---|---|---|
| `GET /`, `GET /<file>` | Digest (`admin`, realm `usr-modem`) | Page files from LittleFS |
| `GET /api/boot` | Digest | Recent console history as `text/plain; charset=utf-8`, no-store, without claiming a terminal |
| `GET /api/session` | Digest | `{"token": "<random>"}`: a single-use token for the WebSocket, valid 30 s |
| `GET /ws?token=<token>` | Token | WebSocket upgrade |
| `POST /api/update` | Digest + CSRF token | Firmware or LittleFS image upload (spec 3.11) |

Diagnostics → Recent console history reads authenticated, read-only `GET /api/boot` and writes the reply into the terminal. It returns the newest 16 KB of all maintenance-console output since boot, with a 1 KB fallback if PSRAM allocation fails. A long `AT*ABOUT` reply (about 9.4 KB) can push the banner out. Device-password lines may be included for the authenticated browser; the Wi-Fi password is never included. Each chunk is copied under the console ring lock, then sent after releasing it, stopping at the captured end of the log.

An inert disabled "No call" button appears when there is no call. During dialling, ringing or a connected call it is replaced by a dark red "Hang up" button with a handset icon. Hang up is enabled only for this browser's call, uses the owner-only control to cancel dialling or hang up directly in command or online mode, and retains terminal ownership.

Every request: check the `Host` header is the modem's mDNS name or IP (blocks DNS rebinding). Changes need a CSRF token (sent to the page on the WebSocket).

The listener defaults on; `AT*WEB[?]` and `AT*WEB=ON|OFF` query/change its saved setting. OFF closes sockets and tickets. Serial access can re-enable it. All files require Digest (`qop=auth`, MD5 HA1 from the device store); no Basic fallback. Nonces expire after 60 seconds and nonce counts must increase. A correct response with an expired, replaced or replayed nonce gets a new challenge with `stale=true`, so the browser retries without asking for the password again. A wrong response starts a one-second lock-out: every Digest attempt in that second is answered `401` at once without checking the password (even a correct one), so guessing is limited to one try a second and the server task never sleeps. Refused attempts don't extend the lock-out. A request header must arrive within 1 s of the previous part. Successful authentication removes the initial boot password; a storage failure refuses access.

HTTP statuses: 200 OK; 400 bad request or rejected image; 401 login needed (or lock-out); 403 wrong host, origin or CSRF token; 404 unknown path or a file that is not a web asset; 409 modem busy or confirmation missing (uploads); 503 an update is in progress, the web files are missing (restore `littlefs.bin` over USB), or the first use could not be recorded. Password changes invalidate nonces, socket tickets and terminal ownership immediately. Existing connections accept no more input and close after their queued terminal output has been sent, so the requesting terminal receives a generated replacement password.

Host accepts the current `<name>.local` or IPv4 address, optionally `:80`. Duplicate security headers, Transfer-Encoding, oversized headers and pipelined requests are refused before the HTTP library's header lookup. Ordinary HTTP connections close after one request. Static serving uses a fixed gzip asset allowlist; other LittleFS files are not reachable. Responses set CSP, no-sniff, no-referrer and frame denial.

## WebSocket `/ws`

- **Binary frames** are terminal bytes, both directions.
  - Modem → page: the session's screen output, UTF-8 with ANSI escape codes. A web session always renders in UTF-8, so the PC line-drawing style uses Unicode box characters (xterm.js draws them itself).
  - Page → modem: keystrokes as UTF-8, only after `access` grants terminal mode. xterm sends Backspace as DEL (0x7F), Ctrl-H as BS (0x08), Enter as CR and arrows/function keys as ANSI sequences. Paste uses xterm's newline-to-CR normalization and preserves byte order with subsequent keys.
- **Text frames** carry typed JSON status, heartbeat and geometry messages. The page ignores unknown server types and fields, so new firmware can add them.

| `t` | Fields | When |
|---|---|---|
| `hello` | `name` (mDNS name), `ver` (firmware version), `csrf` (token for POSTs) | On connect |
| `led` | `m`: bit mask, bit 0 AA, 1 CD, 2 RD, 3 SD, 4 TR, 5 CS, 6 ARQ/FAX | On connect, then whenever the physical LEDs change, at most 20 per second. RD/SD flashes are the same stretched flashes as the front panel |
| `clock` | `epoch` (UTC seconds), `tzo` (UTC offset in minutes, e.g. 780 for NZDT) | On connect, every 60 s, and when NTP or the time zone changes. `epoch` 0 = clock not set |
| `call` | `state`: `idle`, `dialling`, `ringing`, `connected`, `ppp`, `slip`, `config`, `test`; `mode`: `telnet`, `raw`, `ppp` or `slip`; `dir`: `out` or `in`; `number` (as dialled); `host`; `ip`; `port`; `speed` (emulated line speed); `since` (UTC seconds the call connected). Internet calls additionally carry `ready` (IP forwarding enabled), `local` and `peer` (private IPv4 addresses); `ip` is empty and `port` is omitted | On connect and on every change |
| `wifi` | `state`: `up`, `connecting`, `down`; `ssid`; `rssi` (dBm); `ip` | On connect, on change, and every 10 s in every state |
| `access` | `edit`: boolean; `state`: `terminal` or `read-only` | Explicit terminal ownership changes; only the terminal owner may send commands or upload |
| `hardware` | `active`: boolean; `mode`: `lines` when applicable | Show/disable line controls and test-stop button |
| `ready` | `idle`: boolean; `rx`: unsigned 32-bit consumed browser input byte count, reset on terminal acquisition and the hang-up control; `errors`: unsigned 32-bit command-error counter for this parser | Owner only, on change, after its queued terminal output. Partial input, configuration lists, prompts, tests, calls and network operations make `idle` false. The error counter increments even when Q1 suppresses result text |
| `pong` | None | Reply to the browser's `{"t":"ping"}` text heartbeat |

Serial Client Tests publish call state `test`. Their browser menu is a reference listing: execution and transfer handlers reject browser-originated requests. Serial tests retain the shared reservation, keeping other browsers read-only. Serial protocol bytes never enter WebSocket output.

The browser sends `{"t":"ping"}` every five seconds and reconnects after 15 seconds without a frame, covering half-open TCP connections after a power cycle. The watchdog pauses during its own upload; success explicitly starts a new connection and, once the modem answers again, reloads the page (new firmware may bring new web files). While the modem answers the login request with 503 (an update is being written) a browser asks again only every 30 seconds instead of every few seconds. The upload button stays disabled from the first press until the result, and the upload code lives in `upload_flow.js` (browser calls passed in, tested in Node). Old socket callbacks and ticket fetches cannot replace the new session. Heartbeats are handled in the transport and never enter terminal input or light RD/SD.

The upgrade also requires a same-modem HTTP Origin. Invalid Origin does not consume a ticket. Binary terminal input is authorized by the single-use upgrade capability and Origin check; it does not carry a JSON/CSRF envelope. Unknown client text frames cannot invoke commands. There are at most three connected browsers and one terminal owner. Input and output queues are bounded at 2 KiB and 16 KiB per client. Binary frames up to 1 MiB are read in 512-byte pieces; fragmented binary messages are supported, and the sender is held off when the input queue fills. Larger pastes must use multiple frames. While input waits for space, the server continues sending queued output. A full output queue retains data at the session/TCP source instead of closing the connection. No input progress for 30 seconds, no progress on a synchronous command reply for 2 seconds, or a socket I/O failure closes an unresponsive connection. TCP close hangs up and wipes pending terminal input.

The owning page sends the canonical text envelope `{"t":"size","cols":120,"rows":45}`. Integer bounds are 80-65535 columns and 25-65535 rows; the supplied page keeps at least 80x25 and fits its font on smaller windows. The exact key order/spelling shown is required; extra fields, fractions, signs, leading zeroes and trailing bytes are refused. Geometry is never terminal input. The server task coalesces dimensions per client under its queue mutex; only the modem task applies them, and only to that client's owned terminal. An observer cannot resize another owner's UI. The page debounces resize and resends dimensions on ownership/reconnect. Dimensions belong to the terminal session, feed initial and live Telnet NAWS (with IAC escaping), and never change or repaint the wrapping configuration list. Output is pulled in bounded chunks, without cursor positioning.

Every browser starts as a read-only status observer; opening the socket claims nothing. The canonical text frame `{"t":"open"}` explicitly requests terminal ownership, granted only when no browser owns it and no call, test or menu holds the modem. Wait for `access.state=terminal` before sending binary input; input from observers is discarded. The page sends this request once on initial access. A refused request returns `access.state=read-only` and waits for a terminal click or menu selection before retrying. This control frame never enters the AT parser. `E` is an ordinary terminal byte. The owner sends AT command lines directly; `AT*CONFIG` opens the shared menu, and leaving it returns to command mode while retaining ownership. `ATD` and `AT*DIAL` dial on the browser session; CONNECT, BBS bytes, `+++`, `ATO` and `ATH` use that same session. Closing its socket cancels pending dials/prompts and hangs up. Other browsers observe live status and cannot run commands, resize the owner's terminal or upload.

BBS output defaults to CP437 translated by `terminalUtf8`, including the low control-byte glyphs; BEL, BS, TAB, LF, CR and ESC retain terminal semantics so ANSI screens work. `AT*STYLE=UTF8` passes UTF-8 BBS bytes unchanged; `AT*STYLE=CP437` restores translation. These choices are local to this browser connection and independent of PLAIN/ANSI configuration colour. UTF-8 sequences can span binary frames; decode them as a stream. File transfers, Client Tests and PPP/SLIP are refused in the browser. Bare `AT*PASS` generates and shows a replacement device password once on the requesting authenticated terminal, then closes authenticated connections; Wi-Fi passwords are never sent to the page.

Browser hardware tests use the shared AT* handlers. UART-taking tests require idle exclusive ownership, with the browser menu's own reservation permitted. Binary bridge data appears in xterm; Ctrl-X exits. `hardware.mode=lines` enables D/R/C buttons, which send ordinary terminal keys. Disconnect stops an interactive test and restores modem control. The BBS byte-to-UTF-8 converter remains at the browser boundary; configuration lists use ASCII and ANSI colour.

## Idle guard and notifications

Only the terminal owner arms `idle.js`, and only while `call.state=connected` and `call.browser=true`. The `browser` boolean is included in every call snapshot and is true for the browser session's current call, including its outgoing dial before CONNECT. Other tabs and serial calls never arm the timer. Four minutes without typing, clicking, pointer movement or scrolling shows “Are you still there?” with a one-minute countdown and an “I'm here” button. Any user activity cancels the warning. Modem/BBS output does not count. Deadlines use wall-clock timestamps; polling and visibility changes check elapsed time, so delayed background callbacks do not extend the countdown.

At expiry the owner sends the canonical control `{"t":"hangup"}`. The HTTP task accepts it only from the terminal owner, and the modem task hangs up only that browser session's existing call and clears pending terminal input. The idle guard then closes the owner socket and reconnects as a status observer, showing the disconnected overlay. An observer's hang-up request is ignored. The page reports that the session disconnected because the user was away. Manual Hang up retains the terminal. Socket closure also cancels pending dials, prompts and tests through the normal disconnect cleanup.

`link.js` queues at most 1 MiB of unsent input, sends frames of at most 512 bytes at ten-millisecond intervals and waits while WebSocket `bufferedAmount` is at least 2 KiB. TCP backpressure from the firmware's bounded input queue therefore reaches the page. An oversized paste is refused with a toast; uploads, access revocation, hang-up and link loss discard unsent input. Input is never replayed after reconnect. Size frames are debounced, sent on terminal acquisition and on geometry changes in terminal mode, independently of configuration menus.

`toast.js` stacks at most five safe-text `role="status"` messages, dismissing each after five seconds without focus changes or animation, including with reduced motion. Notifications cover terminal opened/closed, call connected (host/name/number), disconnected (the reason, defaulting to “No carrier”), idle hang-up, link lost/restored, refused ownership, command `ERROR` results outside an active call and successful image upload. They never interpret BBS data as a command refusal during a connected call.

## Menu commands and dialogs

Top-level controls follow the generated `AT*CONFIG` catalogue, with serial-only Client Tests omitted. No-input actions immediately send one raw `AT*...` line with CR as binary terminal input. Forms use the same friendly labels, help, enums, units and constraints as the text menu, including seconds-to-milliseconds conversion, field variants and individual profile/register selection. Large choice lists can be filtered and time zones are grouped by region. Optional trailing fields are omitted. The advanced disclosure previews the complete command, masking device-password values. Secrets are cleared on submit, cancel, access loss and a two-minute form timeout. The Wi-Fi password is only entered at the terminal's masked prompt.

Menus and form submission require terminal ownership, a live link, no upload or call, and `ready.idle` with `ready.rx` equal to the number of bytes accepted by the browser input queue modulo 2^32. Sending any input immediately disables command actions until the firmware acknowledges it. This prevents stale idle snapshots from appending a menu command to a partially typed line or a pending password/Y/N prompt, and works with quiet or numeric modem results. The readiness message has a separate coalesced server slot and follows the drained output queue, so a bounded phonebook reply is complete before its table is populated. Table rows offer dial, edit and delete; empty and failed loads are explained. Output is always displayed in the terminal, including table reads. The browser never turns terminal text into HTML.

Interactive follow-ups stay in the terminal: Wi-Fi passwords, restart/reset/Wi-Fi-off confirmations, scan acknowledgements and sound/LED test keys. A scan started from the menu supplies friendly network choices for the scanned-network join variant. Hardware tests keeps Stop test (Ctrl-X), and the DCD/RI/CTS toggles during a line test, usable while other commands are disabled. Ctrl-X ends a bridge. Refused textual `ERROR` and numeric `4` results raise passive toasts outside connected calls; `ready.errors` covers quiet command failures without duplicating those toasts. Local validation and failed sends also explain the problem. The explicit owner-only Hang up control retains terminal ownership.

`?demo` and `scripts/make_preview.py` run the same page with synthetic command results, editable in-memory phonebook rows, scan choices and terminal follow-ups. They never change device settings, join a network, make a real call or operate hardware.

## Image upload

Upload image under Updates and Reset selects a raw project `.bin`. The page reads its embedded `USRMODEM/1/FIRMWARE/<major.minor>\n` or `USRMODEM/1/LITTLEFS/<major.minor>\n` marker, validates target/size/chip and computes SHA-256 locally (also on HTTP). Firmware identity is linked into the application; `build_web.py` creates the filesystem identity file before image construction. `image_manifest.py` verifies both release images and writes their lengths and hashes to a companion JSON file. Identity is a project check, not a cryptographic publisher signature.

`POST /api/update?target=firmware|littlefs&version=<major.minor>` sends raw bytes:

- `Content-Type: application/octet-stream`, exact Content-Length.
- Digest authentication, same-modem Host and Origin.
- `X-CSRF-Token`: the current socket's `hello.csrf`.
- `X-Image-SHA256`: 64 lowercase hexadecimal digits.
- `X-Confirm-Downgrade`: that SHA-256 after confirming an older version.
- `X-Confirm-Filesystem`: that SHA-256 after confirming filesystem replacement.

Only the browser owning the terminal may upload, with no call, pending prompt or partial command. Admission reserves the modem, closes any menu and returns the browser to read-only status (the modem loop has 2 s to answer; a late answer changes nothing and frees the modem again, so no upload is left half-started); calls, other menus, tests, joins/scans and ArduinoOTA cannot overlap the write. Uploads have finite length/time limits, verify the full image identity and hash on the modem, and select/reboot only after successful finalization. The UI displays bytes sent followed by verification; it waits for the HTTP result before claiming success. HTTP 200 means installed and reboot pending; 400 means invalid image/write failure, 401 login needed, 403 origin/CSRF failure, 409 busy or missing confirmation. The development PlatformIO test gate is retained.

LittleFS replaces sounds, web assets and call history together. Its raw image must be exactly 0x7E0000 bytes. It is first staged in the inactive app slot using bounded literal/blank-run records. The filesystem is unmounted only after full identity and SHA verification. Images that cannot fit the 4 MiB staging slot are rejected before the live filesystem changes. Staging replaces any old inactive firmware copy; it does not change the active app or boot selection. The final copy has no second filesystem partition for power-loss rollback. USB `uploadfs` is the recovery path; the firmware never auto-formats a damaged filesystem. Missing assets return an authenticated recovery message, while serial access remains available.

Browser joins use a 60-second candidate transaction, which continues after socket loss. DHCP/static and hostname changes use the same rollback guard. Successful connection commits; failure restores the previous saved network. Serial joins keep their existing 20-second timeout. Intentional Wi-Fi OFF/forget ends browser access.

Example:

```json
{"t":"call","state":"connected","mode":"telnet","dir":"out","number":"1",
 "host":"www.amigaretro.com","ip":"170.64.142.64","port":23,"speed":33600,"since":1792745225}
```

## Demo mode

Open `index.html?demo` (or the single-file preview from `python scripts/make_preview.py`) to run the page against a synthetic terminal. The terminal opens automatically; type `AT*CONFIG` for the friendly letter-list preview, or `ATDT1` for an echoed, coloured demo BBS and the real page's idle guard. Idle expiry returns to read-only with the reconnect overlay. All values are synthetic; no network call or device service is run.

## Tests

`cd web; npm test` runs `web/test/` with Node's built-in test runner, including fake-clock idle deadlines, safe toast lifecycle, access transitions, bounded paste flow, terminal-mode resizing, app wiring, the demo and upload flows. GitHub Actions runs them on every push.
