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

## Files

`web/src/` holds the page; `web/vendor/` holds third-party files (xterm.js 6.0.0, MIT;
IBM Plex Mono, OFL-1.1). `scripts/build_web.py` gzips both into `data/www/` when the
LittleFS image is built (`pio run -e s3 -t buildfs` or `-t uploadfs`). The firmware
serves `data/www/<file>.gz` with `Content-Encoding: gzip`.

Total size on the modem is about 120 KB.

## HTTP endpoints (firmware, to implement)

| Method and path | Auth | Purpose |
|---|---|---|
| `GET /`, `GET /<file>` | Digest (`admin`, realm `usr-modem`) | Page files from LittleFS |
| `GET /api/session` | Digest | `{"token": "<random>"}`: a single-use token for the WebSocket, valid 30 s |
| `GET /ws?token=<token>` | Token | WebSocket upgrade |
| `POST /api/update` | Digest + CSRF token | Firmware or LittleFS image upload (brief 6.11) |

Every request: check the `Host` header is the modem's mDNS name or IP (blocks DNS
rebinding). Changes need a CSRF token (sent to the page on the WebSocket).

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
