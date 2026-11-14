# Roadmap

What is left to do after v3.0. The firmware itself is feature-complete against [project-brief.md](project-brief.md) (the firmware spec); the work that remains is checking it on the finished unit and a few follow-ups. One version per branch, tests first, and a row in [decisions-log.md](decisions-log.md) for each decision.

## Checks that need a person

Automated tests can't cover these. Do them once on the finished unit.

Part of the v3.0 bench run (`docs/plans/wp-26-bench-run.md`, which goes when v3.0 merges):

- Listen to the dial tone, DTMF, pulse, ringing, handshake sounds and the volume knob. The generated call tones should be as loud as the handshake recordings; tune `volume::kCallToneDb` if not.
- Watch every front-panel LED during a call (ARQ shows Wi-Fi).
- `AT*HWLOOP` with a loopback plug.

Needs kit that is not on the bench; do it when it is:

- The configuration screen and Client Tests on a real Amiga terminal, and the PuTTY visual check.
- Web browsing from Windows 9x or an Amiga TCP stack over PPP and SLIP.
- The final build in the case with the DB25 connector (the bench uses a DB9).
- A real crash record in `AT*RESETINFO` (the board shows "none available" after a clean boot).

## Follow-ups

These wait on upstream releases (see the open follow-ups in the decisions log):

- **IDF 6.1.1 or later:** check upstream `httpd_accept_conn()` for the queued least-recently-used purge that can close a just-accepted connection when idle connections fill all six slots. Not changed in `release/v6.1` or `master` on 14 Nov 2026.
- **Arduino 4.0:** at RC1 on 14 Nov 2026. IDF 6.x will need it later. Move when 4.0.0 is stable; a new minor or major framework version needs the owner's say-so.

## Rules for new work

- A change to `lib/hw/src/pins.h` and the values locked in by `test/native/test_pins` goes together, and only when the board changes.
- The native USB port (GPIO19/20) is the USB modem port, so the built-in USB-JTAG debugger is not available while the firmware runs.
