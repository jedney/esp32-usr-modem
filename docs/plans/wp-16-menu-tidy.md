# WP-16 (v2.16): Configuration screen code tidy-up

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | M |
| Branch | `firmware/v2.16-menu-tidy` |
| Depends on | WP-15 (same file) |
| Firmware change | yes, behaviour-preserving (`lib/config`) |

Follow the session workflow and token rules in [README.md](README.md).

## Finding

### MNT-04 (menu part) Long functions

- `lib/config/src/menu.cpp:1243-1456` `Terminal::key` (214 lines).
- `lib/config/src/menu.cpp:352-483` `buildFrame`.
- `lib/config/src/commands.cpp:52-214`.

(Line numbers from `1f51a09`; WP-07 and WP-15 change these files. Grep the names.)

## Approach

- Split by mode/state: `Terminal::key` into one handler per mode (menu navigation, form
  field editing, confirmation, masked prompt, captured command), dispatched from a short
  `key()`. `buildFrame` into header, body per view, footer. `commands.cpp` into one
  function per command group, or a table if it is a name → handler chain.
- No behaviour change. `test/native/test_config` and `test_config_services` are the
  safety net: run them after every extraction.
- Pick up any 5.5-14 rows WP-15 deferred.

## Steps

1. Branch, bump (`minor "Tidy the configuration screen code"`).
2. `buildFrame` (smallest), `commands.cpp`, then `Terminal::key`. Commit after each.
3. No docs change expected beyond `CHANGES.md` (Changed).

**Cut line:** stop after any function; the rest goes to a follow-up.

## End checks

`pio test -e native -f test_config -f test_config_services` after each step; full suite,
`pio run -e s3`, `pio check`, clang-format at the end.

## Done when

No function in these files is much over 80 lines and every test still passes unchanged.
