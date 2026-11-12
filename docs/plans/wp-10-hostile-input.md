# WP-10 (v2.10): Hostile-input tests: PPP, X/Y/ZMODEM

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | M-L |
| Branch | `firmware/v2.10-hostile-input` |
| Depends on | WP-01 |
| Firmware change | tests, plus fixes for any confirmed 5.5 row |

Follow the session workflow and token rules in [README.md](README.md). `lib/clienttest`
is Zimodem-derived (Apache-2.0): keep the licence headers and mark changed files.

## Findings

### TG-03 (High) No hostile-input tests for PPP or X/Y/ZMODEM

The guards read correctly today but could be deleted with every test passing.

- PPP (`lib/internet/src/internet.cpp:201`, `:281`, `:336`, `:441`): a table-driven test
  in `test/native/test_internet` that feeds malformed control packets straight into the
  engine: LCP/IPCP with length shorter than the header, longer than the frame, option
  length 0 and 1, option running past the end, unknown codes, bad FCS. Each case: no
  crash (run under the native build's sanitizers if `platformio.ini` has them; check),
  the right reject or silent drop, and the engine still negotiates a good packet after.
- X/Y/ZMODEM (`lib/clienttest/src/transfer.cpp:184-212`, `:331-347`, `:527`, `:543-549`):
  table-driven tests in `test/native/test_client`: bad block numbers (repeat, skip,
  complement mismatch), YMODEM block 0 with a bad or overlong file name or size, bad
  ZMODEM headers (CRC, type, hex/binary mix), cancel sequences (`CAN` × 2 and × 5).
- The X-1K "wrap" test sends 33 blocks, so it never wraps: make it send 257+ blocks (or
  start the counter near 255) so the block number really wraps.

### 5.5 rows (confirm each with a test first; fix only the real ones)

- **5.5-09** `lib/internet/src/internet.cpp:386-393`: an IPCP Terminate-Request resets the
  30 s negotiation time-out even before PPP is up, so a peer can keep it alive for ever.
  Expected: the time-out runs from the start of negotiation.
- **5.5-10** `lib/clienttest/src/transfer.cpp:92-98`, `:168-170`, `:629-632`: any received
  byte resets the inactivity timer (only the 30-minute cap bounds a babbling peer); a
  missing "OO" after ZFIN ends in a time-out instead of success. Expected: only valid
  progress resets the timer; ZFIN without "OO" finishes after a short wait.
- **5.5-11** `lib/clienttest/src/client_service.cpp:304-307`: the size probe looks only at
  the first `ESC[` in its buffer, so an echoed query hides the real reply. Expected: scan
  for the reply pattern anywhere in the buffer.

### MNT-05e `lib/clienttest/src/transfer.h:1`

Zimodem-derived, but the header names no licence and no "changed" mark (the `.cpp` has
both). Add "Apache-2.0, changed for USR Modem" in the same form as the `.cpp`.

## Steps

1. Branch, bump (`minor "Hostile-input tests for PPP and file transfers"`).
2. MNT-05e (one minute).
3. PPP table, then X/Y/ZMODEM table, then the wrap test.
4. 5.5-09, 5.5-10, 5.5-11.
5. `test/README.md`; decisions log for any behaviour change from 5.5 rows.

**Cut line:** if short, finish the PPP and XMODEM tables and move ZMODEM and the 5.5 rows
to a follow-up (same model).

## End checks

`pio test -e native -f test_internet -f test_client`, full suite, `pio check`,
clang-format; `pio run -e s3` if library code changed. Not run: interop (Linux only).

## Done when

Deleting any length or block-number guard makes a test fail; the 5.5 rows are fixed or
marked "not a bug" in the finding map.
