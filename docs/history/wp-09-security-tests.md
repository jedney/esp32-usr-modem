# WP-09 (v2.9): Security regression tests

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | M |
| Branch | `firmware/v2.9-security-tests` |
| Depends on | WP-07 (settings guards land first so the sweep sees the final commands) |
| Firmware change | tests only, unless a test finds a real bug (then fix it here) |

Follow the session workflow and token rules in [README.md](README.md).

## Findings

### TG-02 (High) No canary sweep for the passwords

`test/native/test_config_services/test_main.cpp:134-194`: the "export has no secrets"
test never stores a Wi-Fi password and checks command names only. No test sets both
passwords and then searches every output for them; the Digest hash is searched nowhere.

**Add one canary sweep test** (in `test_config_services`, or a new suite if the set-up
is large):
- Set a Wi-Fi password and a device password to distinctive canaries (e.g.
  `CanaryWiFi7Q`, `CanaryDev3Z`); compute the stored forms: the Digest HA1 hash (MD5 of
  `user:realm:password`, hex) and any OTA hash.
- Run every registered command (iterate the `AT*` command table, so a new command is
  covered automatically) with no argument and with `?`, plus `AT&V`, `ATI0`-`ATI7`,
  `AT*EXPORT`, `AT*STATUS`, the logs; open every configuration screen page (iterate the
  menu catalogue) and render it.
- Search all output for each canary and hash, in plain, upper/lower hex. Skip commands
  that change state destructively (factory reset, reboot); list skipped ones in the test.
- Allowed exception: `AT*PASS` generating a new password on a local port (SEC-06, rule
  amended in WP-01); it prints a *new* password, not the canary.

### TG-05 Web policy guards untested

`lib/webinterface/src/web_policy.cpp:52-111`, `:306-352`; `image_check.cpp`. Add to
`test/native/test_web`:
- Authorization parser: unterminated quote, more than 12 parameters, header over 900
  bytes: each refused.
- Nonce eviction after 8 challenges (the 9th evicts the oldest), and a nonce count that
  goes backwards (refused, `stale` or 401 as the code does).
- One real MD5 test vector (RFC 1321 `"abc"` → `900150983cd24fb0d6963f7d28e17f72`)
  instead of the constant fake, so a Digest test with a real response passes end to end.
- `ImageCheck(true, ...)` (LittleFS path, exact size): accepted and rejected cases.

### TG-06 Clock wrap and edges in the host port

`lib/hostport/src/linectl.cpp:19-65`, `lib/hostport/src/session.cpp:228-242`: `+++` guard
time, DTR hold and throttle credit are never tested across the 2^32 ms wrap; throttle only
at 300 bps; S3/S5 edge values. Add to `test_hostport`: start the fake clock near
`0xFFFFFF00`, run guard time, DTR hold and throttle across the wrap; throttle at 2400 and
115200; S3 and S5 at their limits (0 and 127 after WP-07).

## Steps

1. Branch, bump (`minor "Tests for secrets, web login guards and clock wrap"`).
2. TG-02, TG-05, TG-06. If a test fails on today's code, it has found a bug: fix it,
   note it in the decisions log and `CHANGES.md` (Fixed).
3. `test/README.md` matrix.

**Cut line:** TG-06 can move to WP-13 (fixtures) if short of time.

## End checks

`pio test -e native -f test_config_services -f test_web -f test_hostport`, then full
suite, `pio check`, clang-format. Firmware build only if library code changed.

## Done when

Removing any masking, a parser guard or wrap-safe arithmetic makes a test fail.
