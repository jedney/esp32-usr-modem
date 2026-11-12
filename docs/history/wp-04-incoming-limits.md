# WP-04 (v2.4): Incoming-call limits

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | M |
| Branch | `firmware/v2.4-incoming-limits` |
| Depends on | WP-01 (decision recorded there) |
| Firmware change | yes (`lib/calls`, `lib/hw/src/tcp_socket.cpp`) |

Follow the session workflow and token rules in [README.md](README.md).

## Findings

### SEC-01 A caller who never hangs up holds the line, the browser menu and OTA

- `lib/calls/src/incoming.cpp:199-208`: ringing has no limit (`rings_` saturates at 255,
  line 53). `lib/hw/src/tcp_socket.cpp:11-31`, `:107-134`: no TCP keepalive anywhere.
- While a call rings or sits idle: the browser can't enter the menu, `AT*CONFIG` is
  refused, OTA is closed, `ATD` gives `BUSY`.
- **Decision (Jon, 11 Nov 2026): an unanswered call is dropped after 8 rings** (about
  24 s at the 3 s ring period). Fixed limit: no new `AT*` command, no parity work.
  - (a) End it as an unanswered call: close the socket, release the slot, record it in
    the call journal, stop the ring sound and RI. `S0` accepts 1-255: treat `S0` above 8
    as 8 for incoming calls. `test/native/test_answer` currently asserts that 260 rings
    are still ringing: change it to assert the drop after ring 8 and the journal entry.
    "Treat `S0` above 8 as 8" means such a call **answers on ring 8**, before the drop
    could happen. Test `S0=0` (dropped after 8 rings), `S0=8` and `S0=20` (both answer
    on ring 8).
  - (b) `SO_KEEPALIVE` with short idle/interval/count values in `TcpSocket::adopt()` and
    `connectAddress()` (e.g. idle 60 s, interval 10 s, count 6 → a dead peer ends in
    `NO CARRIER` within about two minutes). lwIP needs `LWIP_TCP_KEEPALIVE`: check
    `sdkconfig.defaults` / the generated `sdkconfig.s3` before relying on
    `TCP_KEEPIDLE` etc. Build to confirm.
  - (c) Stretch only: let the browser's status view reject a ringing call. Skip unless
    time is left; if skipped, record "not done, optional" in the finding map.
- Docs: `docs/COMMANDS.md` (`S0` note, 8-ring limit, keepalive), decisions log entry
  "implemented" with the keepalive values, README if it describes answering.

### TG-01 Remote `+++` test has no guard time

`test/native/test_answer/test_main.cpp:362-388` sends the remote `+++` 30 ms after data
and makes its final check after a *local* escape, so it would pass even if remote bytes
reached the escape detector. Rewrite: remote silence 1.2 s, `+++`, silence 1.2 s, assert
still online and no `OK`; repeat with the answer port set to RS232 and USB.

### TG-04 Telnet guards

`lib/calls/src/telnet.cpp:49-79` (option replies can't loop) and
`lib/calls/src/tcp_connection.cpp:53-57` (reads limited to 1/16 of the send queue's free
space) have only a `WILL 99` storm test. Add: a mixed `DO`/`WILL`/`DONT` storm, a test
with the send queue nearly full (removing the guard must fail the test), and IAC from an
answer-mode caller before and after `ATA`. Suites: `test_calls`, `test_answer`.

### TG-08 (part) Ring limit test

Covered by (a).

### 5.5-12 (confirm first) S7 runs during the handshake sound

`lib/calls/src/outgoing.cpp:259-263`: the S7 limit keeps running during the handshake
sound, so a short S7 can drop a connection that is already open. Confirm in
`test_calls` with a short S7 and a long handshake; if real, stop the S7 timer once the
TCP connection is open.

## Steps

1. Branch, bump (`minor "Unanswered calls drop after 8 rings; TCP keepalive"`).
2. TG-01 first (it guards the main security rule and doesn't change code).
3. SEC-01 (a) with tests, (b), then docs.
4. TG-04, 5.5-12, then (c) if time allows.

**Cut line:** TG-04 and 5.5-12 can move to a follow-up; SEC-01 (a)(b) and TG-01 must land.

## End checks

`pio test -e native -f test_answer -f test_calls` while working; full suite, `pio run -e
s3`, `pio check`, clang-format at the end. Not run: `e2e/test_answer.py` (Jon's bench).

## Done when

A silent caller is dropped after 8 rings; a vanished peer ends within about two minutes;
the remote `+++` test would fail if remote bytes reached the escape detector.
