# Web interface: files and protocol

The page is a full-window **80 × 25 terminal** (xterm.js) showing the modem's
configuration screen, with a **status bar** along the bottom:

| Item | Source |
|---|---|
| Front-panel lights AA, CD, RD, SD, TR, CS, ARQ/FAX (live) | `led` messages |
| Connection status (Idle, Dialling, Ringing, Connected · Telnet · 33600, Internet (PPP)…) | `call` messages |
| Remote host and IP (`www.amigaretro.com (170.64.142.64:23)`) | `call` messages |
| Connection time | `call.since` and the modem clock |
| Modem Wi-Fi network and signal | `wifi` messages |
| Modem time (the modem's time zone) | `clock` messages, ticked by the page |
| Page ↔ modem link (Online / Offline / Connecting) | WebSocket state |

On narrow windows the bar drops Wi-Fi, then the remote host, then the connection time,
then the clock.

Planned model presentation (Jon, 25 Oct 2026): the first configuration page must
prominently display the selected Sportster 14.4k, 28.8k, 33.6k or 56k Series, with
a model selector in the shared menu (equivalent to `AT*MODEL=`). Consider an ASCII
art rendition of the selected modem above the menu. This is future configuration
and web work (v0.10/v0.11); it does not change the current wire protocol or demo.
The factory model remains 14.4k. Selection affects ATI identity and default CONNECT
reporting only, not the supported commands, speeds or other features. See
`docs/v0.8-plan.md` for the confirmed command, persistence and reporting rules.
The selector maps to `AT*MODEL=14400|28800|33600|56000` and saves device-wide
immediately. A change affects the next call's reporting default; an explicit
phonebook/`&N` speed wins, and the current call's CONNECT/ATO speed stays stable.

## Files

`web/src/` holds the page; `web/vendor/` holds third-party files (xterm.js 6.0.0, MIT;
IBM Plex Mono, OFL-1.1). `scripts/build_web.py` gzips both into `data/www/` when the
LittleFS image is built (`pio run -e s3 -t buildfs` or `-t uploadfs`). The firmware
serves `data/www/<file>.gz` with `Content-Encoding: gzip`.

Total size on the modem is about 120 KB.

## HTTP endpoints (v0.11 firmware)

| Method and path | Auth | Purpose |
|---|---|---|
| `GET /`, `GET /<file>` | Digest (`admin`, realm `usr-modem`) | Page files from LittleFS |
| `GET /api/session` | Digest | `{"token": "<random>"}`: a single-use token for the WebSocket, valid 30 s |
| `GET /ws?token=<token>` | Token | WebSocket upgrade |
| `POST /api/update` | Digest + CSRF token | Firmware or LittleFS image upload (brief 6.11) |

Every request: check the `Host` header is the modem's mDNS name or IP (blocks DNS
rebinding). Changes need a CSRF token (sent to the page on the WebSocket).

The listener defaults on; `AT*WEB[?]` and `AT*WEB=ON|OFF` query/change its saved
setting. OFF closes sockets and tickets. Serial access can re-enable it.
All files require Digest (`qop=auth`, MD5 HA1 from the device store); no Basic
fallback. Nonces expire after 60 seconds and nonce counts must increase. Successful
authentication removes the initial boot password; a storage failure refuses access.
Password changes invalidate nonces, socket tickets and existing connections.

Host accepts the current `<name>.local` or IPv4 address, optionally `:80`. Duplicate
security headers, Transfer-Encoding, oversized headers and pipelined requests are
refused before the HTTP library's header lookup. Ordinary HTTP connections close
after one request. Static serving uses a fixed gzip asset allowlist; other LittleFS
files are not reachable. Responses set CSP, no-sniff, no-referrer and frame denial.

## WebSocket `/ws`

- **Binary frames** are terminal bytes, both directions.
  - Modem → page: the session's screen output, UTF-8 with ANSI escape codes. A web
    session always renders in UTF-8, so the PC line-drawing style uses Unicode box
    characters (xterm.js draws them itself).
  - Page → modem: keystrokes as UTF-8.
- **Text frames** are JSON messages from the modem. Each has a type field `t`. The page
  ignores unknown types and fields, so new firmware can add them.

| `t` | Fields | When |
|---|---|---|
| `hello` | `name` (mDNS name), `ver` (firmware version), `csrf` (token for POSTs) | On connect |
| `led` | `m`: bit mask, bit 0 AA, 1 CD, 2 RD, 3 SD, 4 TR, 5 CS, 6 ARQ/FAX | On connect, then whenever the physical LEDs change, at most 20 per second. RD/SD flashes are the same stretched flashes as the front panel |
| `clock` | `epoch` (UTC seconds), `tzo` (UTC offset in minutes, e.g. 780 for NZDT) | On connect, every 60 s, and when NTP or the time zone changes. `epoch` 0 = clock not set |
| `call` | `state`: `idle`, `dialling`, `ringing`, `connected`, `ppp`, `slip`, `config`, `test`; `mode`: `telnet` or `raw`; `dir`: `out` or `in`; `number` (as dialled); `host`; `ip`; `port`; `speed` (emulated line speed); `since` (UTC seconds the call connected) | On connect and on every change |
| `wifi` | `state`: `up`, `connecting`, `down`; `ssid`; `rssi` (dBm); `ip` | On connect, on change, and every 10 s while up |
| `access` | `edit`: boolean | Interactive ownership changes; false means read-only |
| `hardware` | `active`: boolean; `mode`: `lines` when applicable | Show/disable line controls and test-stop button |
| `pong` | None | Reply to the browser's `{"t":"ping"}` text heartbeat |

The upgrade also requires a same-modem HTTP Origin. Invalid Origin does not consume
a ticket. Binary terminal input is authorized by the single-use upgrade capability
and Origin check; it does not carry a JSON/CSRF envelope. Unknown client text frames
cannot invoke commands. There are at most three connected browsers, one interactive
menu across console/RS232/browser, and bounded input/output queues. A slow terminal
that exhausts its output queue is disconnected. TCP close releases its resources.

Browsers connected while busy display read-only status. `E` explicitly attempts
menu entry; queued keys are discarded when ownership changes. A second browser
cannot take over. Quit returns to read-only status. Browser phonebook dialing starts
the call on the RS232 host port (which must be in idle command mode), so CONNECT
and payload go to the attached computer. The browser continues to show status.

Browser hardware tests use the shared AT* handlers. UART-taking tests require idle
exclusive ownership, with the browser menu's own reservation permitted. Binary
bridge data appears in xterm; Ctrl-X exits. `hardware.mode=lines` enables D/R/C
buttons, which send ordinary terminal keys. Disconnect stops an interactive test
and restores modem control. CP437 output is translated only at the browser boundary.

## Image upload

The browser sends `{"t":"ping"}` every five seconds and reconnects after 15 seconds
without a frame, covering half-open TCP connections after a power cycle. The
watchdog pauses during its own upload; success explicitly starts a new connection.
Old socket callbacks and ticket fetches cannot replace the new session. Heartbeats
are handled in the transport and never enter terminal input or light RD/SD.

The toolbar selects a raw project `.bin`. The page reads its embedded
`USRMODEM/1/FIRMWARE/<major.minor>\n` or `USRMODEM/1/LITTLEFS/<major.minor>\n`
marker, validates target/size/chip and computes SHA-256 locally (also on HTTP).
Firmware identity is linked into the application; `build_web.py` creates the
filesystem identity file before image construction. `image_manifest.py` verifies
both release images and writes their lengths and hashes to a companion JSON file.
Identity is a project check, not a cryptographic publisher signature.

`POST /api/update?target=firmware|littlefs&version=<major.minor>` sends raw bytes:

- `Content-Type: application/octet-stream`, exact Content-Length.
- Digest authentication, same-modem Host and Origin.
- `X-CSRF-Token`: the current socket's `hello.csrf`.
- `X-Image-SHA256`: 64 lowercase hexadecimal digits.
- `X-Confirm-Downgrade`: that SHA-256 after confirming an older version.
- `X-Confirm-Filesystem`: that SHA-256 after confirming filesystem replacement.

Only the browser owning the interactive menu may upload. Admission reserves the
modem and closes its menu; calls, other menus, tests, joins/scans and ArduinoOTA
cannot overlap the write. Uploads have finite length/time limits, verify the full
image identity and hash on the modem, and select/reboot only after successful
finalization. The UI displays bytes sent followed by verification; it waits for the
HTTP result before claiming success. HTTP 200 means installed and reboot pending;
400 means invalid image/write failure, 401 login needed, 403 origin/CSRF failure,
409 busy or missing confirmation. The development PlatformIO test gate is retained.

LittleFS replaces sounds, web assets and call history together. Its raw image must
be exactly 0x7E0000 bytes. It is first staged in the inactive app slot using bounded
literal/blank-run records. The filesystem is unmounted only after full identity and
SHA verification. Images that cannot fit the 4 MiB staging slot are rejected before
the live filesystem changes. Staging replaces any old inactive firmware copy; it
does not change the active app or boot selection. The final copy has no second
filesystem partition for power-loss rollback. USB `uploadfs` is the recovery path;
the firmware never auto-formats a damaged filesystem. Missing assets return an
authenticated recovery message, while serial access remains available.

Browser joins use a 60-second candidate transaction, which continues after socket
loss. DHCP/static and hostname changes use the same rollback guard. Successful
connection commits; failure restores the previous saved network. Serial joins keep
their existing 20-second timeout. Intentional Wi-Fi OFF/forget ends browser access.

Example:

```json
{"t":"call","state":"connected","mode":"telnet","dir":"out","number":"1",
 "host":"www.amigaretro.com","ip":"170.64.142.64","port":23,"speed":33600,"since":1792745225}
```

## Demo mode

Open `index.html?demo` (or the single-file preview from `python scripts/make_preview.py`)
to run the page against a simulated modem: press **D** to fake a call to phonebook
entry 1 and **H** to hang up. The demo's menu is a mock-up, not the final screen.

## Tests

`cd web && npm test` runs the status-bar unit tests (`web/test/`) with Node's built-in
test runner. GitHub Actions runs them on every push.
