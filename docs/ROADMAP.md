# Roadmap

What is left to do after v3.0. The firmware itself is feature-complete against [project-brief.md](project-brief.md) (the firmware spec); the work that remains is checking it on the finished unit and a few follow-ups. One version per branch, tests first, and a row in [decisions-log.md](decisions-log.md) for each decision.

## Checks that need a person

Automated tests can't cover these. Do them once on the finished unit:

- Listen to the dial tone, DTMF, pulse, ringing, handshake sounds and the volume knob.
- Watch every front-panel LED during a call (ARQ shows Wi-Fi).
- `AT*HWLOOP` with a loopback plug.
- The configuration screen and Client Tests on a real Amiga terminal, and the PuTTY visual check.
- Web browsing from Windows 9x or an Amiga TCP stack over PPP and SLIP.
- The final build in the case with the DB25 connector (the bench uses a DB9).
- A real crash record in `AT*RESETINFO` (the board shows "none available" after a clean boot).

## Follow-ups

- **IDF 6.1.1 or later:** check upstream `httpd_accept_conn()` for the queued least-recently-used purge that can close a just-accepted connection when idle connections fill all six slots (see the open follow-ups in the decisions log).
- **Arduino 4.0:** IDF 6.x will need it later. Move when 4.0.0 is stable; a new minor or major framework version needs the owner's say-so.
- **Small clean-ups to decide:** the `FAX` spelling of the ARQ LED in the `AT*HW` LED tests, and the web `hello` message's always-true `size` flag.

## Rules for new work

- A change to `lib/hw/src/pins.h` and the values locked in by `test/native/test_pins` goes together, and only when Electronics changes the board.
- The native USB port (GPIO19/20) is the USB modem port, so the built-in USB-JTAG debugger is not available while the firmware runs.
