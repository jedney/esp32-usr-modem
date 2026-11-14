# Roadmap

What is left to do after v3.0. The firmware itself is feature-complete against [project-brief.md](project-brief.md) (the firmware spec); the work that remains is checking it on the finished unit. One version per branch, tests first, and a row in [decisions-log.md](decisions-log.md) for each decision.

## Checks that need a person

Automated tests can't cover these. Do them once on the finished unit.

Needs kit that is not on the bench; do it when it is:

- The configuration screen and Client Tests on a real Amiga terminal, and the PuTTY visual check.
- Web browsing from Windows 9x or an Amiga TCP stack over PPP and SLIP.
- The final build in the case with the DB25 connector (the bench uses a DB9).

## Rules for new work

- A change to `lib/hw/src/pins.h` and the values locked in by `test/native/test_pins` goes together, and only when the board changes.
- The native USB port (GPIO19/20) is the USB modem port, so the built-in USB-JTAG debugger is not available while the firmware runs.
