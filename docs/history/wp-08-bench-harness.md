# WP-08 (v2.8): Bench harness safety

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / medium |
| Size | M |
| Branch | `firmware/v2.8-bench-harness` |
| Depends on | WP-01 |
| Firmware change | none (e2e Python, one on-board test, test docs) |

Follow the session workflow and token rules in [README.md](README.md). Nothing here can
be run without the bench: check with `python -m pytest test/e2e --collect-only -q`,
`python -m py_compile` on changed files, and compile the changed on-board test with
`pio test -e s3 -f test_board --without-uploading --without-testing`.

## Findings

- **TG-09 (High)** Runs hang instead of failing. `test/e2e/conftest.py:136`, `:151`;
  `test/e2e/test_rs232.py:137`; `test/interop/serial_pppd.py:75`: serial ports opened with
  RTS/CTS and no write time-out. `test_rs232.py:137` sets `write_timeout = 0` and never
  restores it. **Fix:** `write_timeout=5` everywhere a port is opened; restore the value
  in `test_rs232.py` (try/finally); add `pytest-timeout` to `test/e2e/requirements.txt`
  and a default `timeout` (e.g. 300 s) in the pytest config; document in `test/e2e/README.md`.
- **TG-10** Asserts can print secrets. `test/e2e/test_wifi.py:71-72`,
  `test/e2e/test_device.py:201`: `assert echoed == b"*" * len(password)` prints `echoed` on
  failure (the Wi-Fi password in clear). Other asserts print the boot banner, which can
  hold the first-boot device password. **Fix:** compute a boolean first and assert on it
  with a message that names no secret (the files already do this elsewhere; Grep for the
  pattern). Search all e2e files for asserts on banner or echo text.
- **TG-11** `test/embedded/test_board/test_main.cpp:39-67` uses the production settings
  store, erases it and restores it only at the end. **Fix:** use a scratch namespace like
  the other on-board suites (Grep `test_` namespaces in `test/embedded`).
- **TG-12** State left behind on failure: `test/e2e/test_hwtest.py:95-110` (bridge test
  without try/finally); `test/e2e/test_wifi.py:64-76` (a wrong `.env` Wi-Fi password
  replaces the saved network); `test/e2e/test_device.py:45-68` (`--device-tests` without
  `--save-password` can set a random device password nobody knows). **Fix:** try/finally;
  in the Wi-Fi test, restore or verify before saving; refuse `--device-tests` without a
  known password (`pytest.exit` with a clear message).
- **TG-13** `test/e2e/test_calls.py:262`, `test/e2e/test_internet.py:303-336`: a default
  run dials phonebook entry 1 (a real BBS) and needs the Internet. **Fix:** default to a
  PC-local test server number; the real BBS behind an opt-in flag.
  `test_internet.py:139` looks for `NO DIALTONE`; the modem prints `NO DIAL TONE`.
- **TG-17** `test/native/test_hwtest/test_main.cpp:176-183`, `:279-291`: two assertions
  test values that are clamped by definition, so they can't fail: assert on the unclamped
  value. All nine on-board RS232 tests (`test/embedded/test_rs232`) are ignored on the
  fitted board (no loopback plug): correct the matrix in `test/README.md` to say so.
- **SEC-07c leftovers** if WP-01 cut them: action SHA pinning and pip pins.

## Steps

1. Branch, bump (`minor "Bench tests fail instead of hanging and never print secrets"`).
2. TG-09, TG-10 (both High), then TG-12, TG-11, TG-13, TG-17.
3. `test/README.md` and `test/e2e/README.md`.

**Cut line:** TG-13 and TG-17 can move to a follow-up.

## End checks

`pytest --collect-only` on `test/e2e`, `py_compile` on `test/interop` changes,
`pio test -e native -f test_hwtest`, on-board compile of `test_board`,
`python scripts/check_version.py`. No firmware build needed.

## Done when

No e2e path can hang on a silent modem, print a secret on failure, or leave the modem in
a changed state; finding map updated.
