# WP-03 (v2.3): Hardware tests cannot freeze the modem

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | M |
| Branch | `firmware/v2.3-hwtest-bounds` |
| Depends on | WP-02 (same files: `src/rs232.cpp`, `src/usbport.cpp`, `src/main.cpp`) |
| Firmware change | yes (`src/hwconsole.*`, `src/main.cpp`, `lib/hwtest`, `lib/atcmd`) |

Follow the session workflow and token rules in [README.md](README.md).

## Findings

### STA-03 Interactive hardware tests can block for ever, and one can start during a call

- `src/hwconsole.h:16`: `connected()` is true by default; only the browser terminal
  overrides it (`src/main.cpp:225-285`).
- "Any key stops" loops: `src/hwconsole.cpp:116-126` (`waitKey`), `:272-282` (`ledChase`),
  `:284-300` (`ledWalk`), `:365-375` (endless tones). `:319`: `AT*HWLED` with no argument
  starts the chase. `src/main.cpp:354-361`: anything starting `LED` is allowed during a call.
- Scenario 1: `AT*HWTONE=DIAL` on USB, terminal closed: tone for ever, everything else
  dead until a key or a power cycle. Scenario 2: call up on RS232, `AT*HWLED` on the
  console freezes the call.
- **Fix:**
  1. `Rs232Term` and `UsbTerm` (`src/main.cpp:234-256`) get a `connected()` that follows
     DTR (RS232: the DTR input; USB: the CDC line state). The loops already stop when
     `connected()` goes false; check they do.
  2. An inactivity time-out in each "any key" loop (suggest 5 minutes for tones and LED
     patterns, configurable as a constant). Put the timing decision in `lib/hwtest` if it
     can be unit-tested there; otherwise keep it small in `hwconsole.cpp`.
  3. Refuse `LED`, `LED=CHASE`, `LED=WALK` while a call is up (narrow the `LED` prefix
     allowance at `main.cpp:354-361` to the single-LED forms). Reply `ERROR` with a short
     reason, as other refused tests do.
- Tests: native tests for any logic moved to `lib/hwtest` (time-out, refusal list).

### STA-09b `AT*BAUD` changes RS232 speed during an RS232 call

`src/main.cpp:419-441`, `lib/atcmd/src/atcmd.cpp:105-115`: `AT*BAUD=<n>` from the console
or USB changes the RS232 speed while a call is up on RS232. Refuse while another port
has a call (`ERROR`). Native test in `test_atcmd` if the check can live there.

### STA-09c `restoreBrowserResources()` cancels other ports' holds

`src/hwconsole.cpp:981-987` runs on any browser disconnect and cancels LED, amp and
signal holds set from another port. Remember which port set each hold and restore only
the browser's.

### MNT-05j (part) cppcheck note `src/hwconsole.cpp:895`

Index used before its bound check; reorder the two conditions.

### MNT-05i (part) stale comment `src/hwconsole.h:1-2`

Describes v0.5-v0.8 behaviour; update it.

## Steps

1. Branch, bump (`minor "Hardware tests stop on their own and can't freeze a call"`).
2. STA-03 items 3, 1, 2 in that order (smallest risk first).
3. STA-09b, STA-09c, then the two tidy-ups.
4. `docs/COMMANDS.md`: note the time-out and the refusals for `AT*HWLED`, `AT*HWTONE`,
   `AT*BAUD`; `AT*HELP` text if it mentions them. Decisions log entry for the time-out
   value and the refusals.

**Cut line:** STA-09c can move to WP-14 (main.cpp tidy-up) if short of time.

## End checks

Full `pio test -e native`, `pio run -e s3`, `pio check`, clang-format. Not run:
on-board/e2e (`e2e/test_hwtest.py` covers these on the bench).

## Done when

No hardware test can run longer than its time-out or past a closed terminal; the LED
patterns and `AT*BAUD` are refused during a call; finding map updated.
