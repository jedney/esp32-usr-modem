# WP-22 (v3.0): Network console

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high |
| Size | L |
| Depends on | WP-21 (rewritten Wi-Fi events, new OTA hash) |

Read [wp-19-common.md](../plans/wp-19-common.md) first (decisions, versions, board testing, rules).

Decision 11. The maintenance console (UART0) also works over a raw TCP port, like
ser2net, so with the PC's USB cable on the rear USB modem port, or no cable at all,
Claude and Jon can use it over Wi-Fi: `socket://usr-modem.local:<port>` in `pyserial`,
`pio device monitor` and the e2e `--console-port`. Together with ArduinoOTA (SHA-256
from WP-21), WP-23 to WP-26 can debug and flash the board without the console cable.

**Not possible over the network** (these still need the console cable): the first flash
after a full erase (the ROM bootloader, esptool), on-board tests (`pio test -e s3`),
panic output as it happens (the core dump still shows with `AT*RESETINFO`), and a modem
that doesn't boot or join Wi-Fi.

## Steps

0. **Jon confirms the open points first** (*Proposed* until then; decisions-log row):
   - Port: **2001** (ser2net's usual range; not 23, which is the answer port, and not
     2323, which clients treat as Telnet).
   - Factory setting: **on**, like the web page, which uses the same device password.
   - **Plain TCP, no TLS:** the password crosses the LAN unencrypted, as with Telnet.
     The web page keeps its Digest log-in. TLS would rule out `socket://` clients.
   - `AT*PASS` typed on the network console is **refused**: the new password must only
     be shown on a local serial port.
1. **Behaviour:**
   - Raw bytes both ways, no Telnet negotiation. One client at a time; a second
     connection gets a one-line "in use" message and is closed.
   - **Log-in first:** `Password:` with no echo, the device password, compared in
     constant time, with the web log-in's lock-out rule (`lib/webinterface/src/web_policy.h`).
     Three wrong passwords close the connection. Nothing else is sent before log-in.
   - **The same console session, not a second one:** after log-in the client types into
     the console's AT session (`console`, `consoleSink` in `src/main.cpp`), including
     `AT*CONFIG` and the hardware tests under the console's rules, and sees all console
     output. The UART keeps working alongside it.
   - **Secrets:** the device password is never sent to the network console or kept for
     it. The boot banner's password line (`device.bootPassword()`) and `AT*PASS`'s new
     password go through an explicit local-serial-only write. The Wi-Fi password is never
     printed anywhere already.
   - **Never waits:** output to the socket is non-blocking. If the socket can't take
     more, it is dropped under the same rule as the other serial sinks
     (`hostport::SinkGate`), so the loop and the watchdog never wait for the network.
   - **Boot log:** a RAM ring buffer (about 16 KB, in PSRAM) keeps console output since
     boot, including ESP-IDF and Arduino log output (`esp_log_set_vprintf`). It is replayed
     to the client after log-in, so boot messages from before Wi-Fi came up can be read
     over IP. The password lines never enter it. Optional if cheap: keep the previous
     boot's buffer across a software restart, watchdog or panic (no-init RAM).
   - Callers on port 23 still never reach command mode (Q44 unchanged). This is a
     separate maintenance port with its own password.
   - **Settings:** `AT*NETCONSOLE=ON|OFF` and `AT*NETCONSOLE?` (name to confirm), saved
     through `nvs_store`. Turning it off closes a connected client. It also gets an entry
     on the configuration screen, a line in `AT*HELP` and in `docs/COMMANDS.md` (the
     parity rule). `AT*STATUS` shows a connected client's address.
2. **Code:** the logic goes in `lib/`, with native tests first: the log-in (prompt, no
   echo, compare, lock-out, tries), one client, the output copy with local-only writes
   kept out of the socket and the buffer, the ring buffer, and the non-blocking rule. The
   socket code goes in `lib/hw` (reuse `hw::TcpListener` and `hw::TcpSocket`). In
   `src/main.cpp`, console output is written straight to `Serial` in about 30 places:
   route all of it through one function, so nothing can bypass the copy by accident.
   Keep it in the loop task.
3. **Tooling:**
   - The e2e `conftest.py` opens `--console-port` with `serial.serial_for_url()`, so
     `socket://host:2001` works.
   - On a socket it logs in with `USR_MODEM_PASSWORD` from `.env` (never printed), and it
     reconnects after tests that restart the modem.
   - A VS Code task "Modem: network console" (`pio device monitor --port
     socket://usr-modem.local:2001`; Jon types the password).
4. **Tests:** native as above. A new `test/e2e/test_net_console.py`:
   - log-in: a wrong password is refused, with the lock-out; nothing is sent before
     log-in; a second client is refused;
   - AT commands work; the boot log is replayed;
   - the device password never reaches the socket (a boot banner, `AT*PASS`; canary like
     the existing canary sweeps);
   - a client that stops reading doesn't stall the modem (RS232 `AT` still answers);
   - `AT*NETCONSOLE=OFF` closes the client.

   Then run the existing console e2e tests once through `socket://`, and do one OTA
   upload followed by the boot log on the network console.
5. **Docs:** decisions-log row; `docs/COMMANDS.md`; `AT*HELP`; README; CLAUDE.md (the
   "Secrets" rule: the device password is never shown on the network console; the build
   and test table); the firmware spec (`docs/project-brief.md`; WP-25 trims it);
   `test/README.md` matrix; `CHANGES.md` [3.0] "Added".
6. **Hand-over:** update "Testing on the board" in
   [wp-19-common.md](../plans/wp-19-common.md) and Claude's bench memory with the port and how to
   use it.

Cut line: 1, 2, 4's log-in, secrets and stall tests, and 3's `conftest.py`. The boot log
buffer, the VS Code task and the full console suite over `socket://` can move to
WP-23.

## Result (13-14 Nov 2026)

Done. Reasons and details are in the decisions log (rows "network console: open points",
"network console: design", "found in WP-22, not changed").

- **Jon's answers (step 0):** port 2001, factory ON, plain TCP, `AT*NETCONSOLE`. No
  special password rules: the network console counts as a local console, so there is no
  local-only write path and no canary test for it (step 1 "Secrets" and the step 4 canary
  are replaced by this).
- **Code:** `lib/netconsole` (log-in, one client, console log ring, setting; native
  `test_netconsole`, 25 cases), `Device::matches()` (constant-time hash check),
  `calls::Listener::refuse()`, per-socket keepalive in `hw::TcpSocket`, and
  `src/console_out.cpp`, the one path for console output (`Serial` is no longer written
  anywhere else in `src/`). ESP-IDF log lines are captured; Arduino `log_x` lines and panic
  output are not, and the previous boot's log is not kept (optional, not done).
- **Found on the board:** pyserial's `socket://` empties its input just after connecting,
  so a prompt sent at once was sometimes lost: the prompt now waits 250 ms and the e2e
  fixture presses Enter if none shows. A client that vanished held the port for about
  2 minutes: its socket now has a 10 s / 2 s / 3 keepalive. Two e2e tests read one byte
  where the UART gave a whole line (`in_waiting` on a socket is 0 or 1); fixed in the tests.
- **Tests on the board** (console on the network unless said): `test_net_console.py`
  7/7, three runs in a row (with `--rs232-port COM18`); `test_console` 7, `test_settings`
  13, `test_session` 16, `test_config` 13, `test_hwtest` 17, `test_client` 9,
  `test_device` 2 (5 need flags), `test_rs232` 21, `test_calls` 3 (7 need a call server),
  `test_answer` 9, `test_internet` 13 (2 UDP skips) all pass. The three "Wi-Fi loss" tests
  turn Wi-Fi off and so need the cable (deselected); `test_wifi.py` likewise. USB not
  connected; web needs `--web-tests`. OTA upload followed by the replayed boot banner on
  the network console: works. App image 1,499,969 bytes (35.8 %), static RAM 153,424 bytes.
- **Local checks:** native 514 (1 skip: pins overridden by the bench `.env`), cppcheck,
  clang-format, UI tables, version.
- **For Jon:** try the VS Code task "Modem: network console" (it needs a real terminal,
  so it was not run here; it opens the port the same way as the tests, which get the
  prompt 10 times out of 10).
