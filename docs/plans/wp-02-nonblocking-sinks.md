# WP-02 (v2.2): Serial output never blocks the loop

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high |
| Size | M-L |
| Branch | `firmware/v2.2-nonblocking-sinks` |
| Depends on | WP-01 |
| Firmware change | yes (`src/rs232.cpp`, `src/usbport.cpp`, `lib/hostport`, maybe `lib/calls`) |

Follow the session workflow and token rules in [README.md](README.md).

## Findings

### STA-01 (High) A serial port that is not read freezes the modem 2 s per result

- `src/rs232.cpp:22-44`: `UartSink::write` waits up to 2 s in `delay(1)` steps when the
  UART has no room. `src/usbport.cpp:18-37`: same for USB.
- `lib/hw/src/hw.h:62`: `kHostTxBuffer = 0` (only the 128-byte UART FIFO; decided in
  v0.12 so writes never block in the driver).
- `lib/hw/src/hw.cpp:396-410`: `&R2` makes the UART obey the computer's RTS; factory
  settings are `&H1 &R2` (`lib/modemcfg/src/modemcfg.cpp:195`, `:201`); flow control
  applies in command mode too (`src/rs232.cpp:250-255`).
- `lib/calls/src/incoming.cpp:45-63`: `RING` is written to all three ports with no check
  that anyone is listening.
- Effect: nothing on RS232 (RTS off), modem used from USB or the browser. 16 `RING`s fill
  the FIFO; from then on every `RING` blocks the loop 2 s (sound, other ports, web pump,
  RI timing, Wi-Fi, OTA). The bytes never drain. USB does the same with a program that
  holds the port open without reading (1024-byte buffer).
- **Fix (agreed in the review):** never wait in a sink.
  - Unsolicited output (`RING`, and any other result not answering a command on that
    port): drop the write when the port has no room.
  - Command replies: wait once (short, bounded), then mark the port **stalled** and drop
    at once until a write succeeds again (room appears).
  - Data during a call: keep today's behaviour unless it also waits in `delay()`; check
    how `Session`'s online path uses the sink before changing it, and don't lose call
    data silently (flow control already holds off the network side).
- Put the policy in `lib/hostport` as plain logic (no Arduino calls), e.g. a small
  `SinkGate` that takes "room available", "unsolicited or reply", and the clock, and
  answers write / drop / retry. `UartSink` and `UsbSink` use it. Find how the sinks learn
  that a write is unsolicited: look at how `incoming.cpp` and `Session` write results
  (Grep `write(`, `result(` in `lib/hostport/src/session.cpp` and `lib/calls/src/incoming.cpp`).
- **Tests (TG-08 part):** in `test/native/test_hostport` (or `test_answer` for `RING`):
  a fake sink that reports no room; `RING` is dropped without delay; a reply waits once,
  then the port is stalled and later writes return at once; a write after room returns
  clears the stall. Use the fake clock, assert no time passes in the drop path.
- Optional bench check (only if Jon has the board handy; not required): factory
  settings, RS232 unplugged, call the modem, let it ring 20 times, watch `AT*STATUS`
  response time on the USB port. Confirms the RS232 Pal reads RTS as off with nothing
  connected.

### SEC-07e Read buffers that can hold a typed password are not wiped

`src/main.cpp:862-871` and `src/rs232.cpp:156`, `:185` keep read buffers that can hold
a typed password; `src/usbport.cpp:70-79` already wipes its buffer. Do the same (volatile
memset or the helper `usbport.cpp` uses) after use.

### 5.5-13 (confirm first) `captureBegin()` failure ignored

`src/rs232.cpp:215-218`, `:257-263`: if `captureBegin()` fails the port is deaf in AUTO
speed, and `runs` is used unchecked if both allocations fail. Confirm by reading; if
real, fall back to a fixed speed with a console message and guard `runs`.

## Steps

1. Branch, bump (`minor "Serial ports never block the modem"`).
2. Read `src/rs232.cpp:1-60`, `src/usbport.cpp:1-50`, the sink interface in `lib/hostport`
   and how `RING` is written. Design the gate; keep it small.
3. Tests first in `test_hostport`, then the gate in `lib/hostport`, then wire both sinks.
4. SEC-07e wipes; 5.5-13.
5. Decisions log: "Sinks never wait: unsolicited results are dropped when the port has
   no room; a reply waits once, then the port is marked stalled" (why: STA-01).
   `test/README.md` matrix rows.

**Cut line:** STA-01 with its tests is the package. 5.5-13 can move to WP-12 if needed.

## End checks

Full `pio test -e native`, `pio run -e s3`, `pio check`, clang-format on changed files.
Not run: on-board, end-to-end (`e2e/test_rs232.py` and `test_usb.py` are worth a bench
run by Jon before release).

## Done when

No sink path sleeps for more than the single bounded reply wait; tests prove `RING` to a
full port costs no time; finding map rows STA-01, SEC-07e, 5.5-13, TG-08 (STA-01 part)
marked.
