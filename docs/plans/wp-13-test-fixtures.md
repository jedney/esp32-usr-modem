# WP-13 (v2.13): Shared native test fixtures

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / medium |
| Size | M |
| Branch | `firmware/v2.13-test-fixtures` |
| Depends on | WP-09, WP-10 (they add tests to the same files) |
| Firmware change | none (tests only) |

Follow the session workflow and token rules in [README.md](README.md).

## Finding

### TG-16 Fakes are copied between suites and have drifted

`test/native/*/test_main.cpp`: fakes (sound, settings stores, file store, clock, ports)
are copied between suites and have drifted: `FakeSound::fail` is never set in
`test_calls`; several fake stores can't fail. This hides whole classes of failure path.

## Approach

1. Find the copies without reading whole files:
   `grep -n "struct Fake\|class Fake" test/native/*/test_main.cpp`. List each fake, the
   suites that copy it and the differences (diff the extracted blocks).
2. Create one shared header (e.g. `test/native/support/fakes.h`; check how PlatformIO
   native tests can include it: a relative `#include "../support/fakes.h"` or
   `build_flags = -I test/native/support` in the `native` env). Each fake gets working
   failure switches (`failNext`, `failAll`).
3. Convert one suite at a time, starting with the most-copied fake. After each suite:
   `pio test -e native -f test_<suite>`. Keep test behaviour the same; where a drifted
   copy behaved differently, keep the old behaviour unless it hid a bug.
4. Add failure-path tests the drift was hiding, at least: sound failure in `test_calls`,
   store write failure in one suite that couldn't fail before.

**Cut line:** stop after any converted suite; record which suites remain in the plan file
when it moves to `docs/history/`.

## Steps

1. Branch, bump (`minor "Shared fakes for the PC unit tests"`).
2. Approach above. Commit after each converted suite.
3. `test/README.md` (where the fakes live).

## End checks

Full `pio test -e native` (same pass count plus the new tests), `pio check`,
clang-format. No firmware build.

## Done when

Each fake exists once, can fail on demand, and every suite passes.
