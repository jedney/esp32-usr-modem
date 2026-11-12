# WP-01 (v2.1): Process gates, docs and decisions

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / medium |
| Size | M |
| Branch | `firmware/v2.1-process-gates` |
| Depends on | nothing; do this first |
| Firmware change | none (scripts, workflows, docs, one native test) |

Follow the session workflow and token rules in [README.md](README.md).

## Findings

- **MNT-01** `.github/workflows/ci.yml:27`:
  `run: echo "version=$(python3 scripts/check_version.py)" >> "$GITHUB_OUTPUT"` throws
  away the exit status, so the version job never fails and the error text becomes the
  artifact name (`ci.yml:125`). `release.yml:30-31` does it correctly; copy that.
- **MNT-02** (a) `scripts/pre_upload_tests.py:28-29` gates `upload` and `uploadfs` only;
  `uploadfsota` skips the unit tests. (b) `ci.yml:75` passes `--fail-on-defect=medium`;
  PlatformIO compares exact labels, so a cppcheck "high" does not fail. Add
  `--fail-on-defect=high` (keep medium).
- **SEC-05** `release.yml:7-13`: `workflow_dispatch` with no branch guard, `contents: write`
  for the whole job; `:23-25` checkout keeps the token; `:53` `pip install platformio`
  unpinned; `:78-79` tags `$GITHUB_SHA`. Fix: `if: github.ref == 'refs/heads/main'` on the
  job, `persist-credentials: false` on checkout (publish already gets `GH_TOKEN`), pin
  `platformio==6.1.x` (the version CI uses today; check `pio --version` locally).
- **SEC-07c** Both workflows: actions referenced by tag, not commit; `pip install`
  unpinned; CI caches restored into the release build. Pin each action to a commit SHA
  with the tag as a comment (`gh api repos/<owner>/<action>/git/ref/tags/<tag>`), pin pip
  packages, and don't restore caches in `release.yml`.
- **SEC-07d** `scripts/network_dependencies.py:78-88` writes a download to the cache
  before checking its SHA-256, so a bad body breaks every later build. Verify in memory
  (or a temp file), then write; delete on mismatch. Extend
  `test/scripts/test_network_dependencies.py`.
- **SEC-07b** `scripts/pin_config.py:20`, `:40-41` only checks 0-48. Reject GPIO 22-25
  (absent on the S3), 26-37 (flash and octal PSRAM on the N16R8), 19/20 (USB modem port)
  and a pot pin outside ADC1 (GPIO 1-10). Check the rev B defaults in `.env.example`
  still pass. Add script tests.
- **TG-07** `test/native/test_pins/test_main.cpp:19-21` skips the pin-lock test when `.env`
  overrides any pin, and the upload gate still passes. Make it fail unless an explicit
  flag says the override is on purpose (e.g. `USR_MODEM_PINS_OVERRIDDEN=1` passed through
  `pin_config.py` as a macro). Document the flag in `.env.example` and CLAUDE.md. Ask Jon
  whether his `.env` overrides are deliberate before relying on the flag (don't read
  `.env`).
- **SEC-06** Decision (Jon, 11 Nov): `AT*PASS` may print a generated password once on the
  requesting local serial port. Amend the CLAUDE.md Secrets rule to: "The device password
  is shown in the boot banner until first use, and once on the requesting local serial
  port when `AT*PASS` generates a new one. It is never shown to the browser, never logged,
  exported or sent to the web page, and never shown on a remote (port 23) session." Add a
  native test in `test/native/test_devicecfg` that the browser form of `AT*PASS` is still
  refused (`lib/devicecfg/src/device_commands.cpp:34-37`).
- **MNT-03** `README.md:16` ("a speaker with a hard volume limit"), `README.md:170`
  (`AT*HWTEST` list says "volume limit"), `lib/config/ui-bindings.json:435` ("cap always
  applies"). There is no cap since 7 Nov. Fix the text, then
  `python scripts/generate_ui.py` and `python scripts/generate_ui.py --check`.
- **MNT-05c** Add `/build/` to `.gitignore`.
- **MNT-05d** CLAUDE.md "Hardware summary" says `espressif32@^6.9.0`; `platformio.ini:23`
  pins `6.13.0`, Arduino 2.0.17, ESP-IDF 4.4.7. Update the line.
- **5.5-18** (confirm first) `scripts/ota_upload.py:13-16` (and `load_env.py`): a UTF-16 or
  BOM `.env` breaks the build: read with `utf-8-sig`, give a clear error for UTF-16.
  `bump_version.py:50-51` writes CRLF on Windows: open with `newline="\n"`.
  `generate_ui.py:20` hotkey check is a substring test: compare whole keys. Script tests
  for each.

## Records

- Move `REVIEW.md` to `docs/history/v2-code-review.md` (`git add` it; it is untracked).
  Fix the links in `docs/plans/README.md` if needed.
- Commit `docs/plans/` (this folder).
- `docs/decisions-log.md` entries dated 2026-11-11 (Jon) for the review decisions:
  - SEC-01: an unanswered incoming call is dropped after 8 rings; `S0` above 8 counts as
    8 (implemented in WP-04).
  - STA-02: every browser starts read-only; `E` takes the configuration screen
    (implemented in WP-06).
  - SEC-06: the amended password rule above.
  - STA-02 open point (Jon, 12 Nov): a configuration screen opened on purpose
    (`AT*CONFIG`, or `E` in the browser) closes after 5 minutes with no key; fixed
    value, no `AT*` command (implemented in WP-07).
  - SEC-03: target Arduino core 3.3.x on ESP-IDF 5.5.x after a spike; **v3.0**, after all
    the review fixes (Jon, 12 Nov, replaces "v2.1" in the review).
  - Review fixes are split into work packages v2.1-v2.17 (`docs/plans/`).
  - PERF-03 and the review's "leave alone" items: no change unless a measurement shows a
    need.

## Steps

1. Branch, bump (`minor "Review fixes: CI, release and upload gates; review decisions"`).
2. Records above (move review, decisions log, CLAUDE.md, `.gitignore`).
3. MNT-01, MNT-02 (b), SEC-05, SEC-07c (workflows). Validate YAML with
   `python -c "import yaml,sys; yaml.safe_load(open(sys.argv[1]))" <file>` if PyYAML is
   there; otherwise read the diff carefully.
4. MNT-02 (a), SEC-07d, SEC-07b, 5.5-18 (scripts, with tests).
5. TG-07, SEC-06 test (native).
6. MNT-03 and regenerate UI data.

**Cut line:** if short of time, leave 5.5-18 and SEC-07c's action SHA pinning for WP-08;
everything else is small and should land here.

## End checks

`python scripts/check_version.py`, `python -m pytest test/scripts -q`,
`python -m unittest discover -s ui/tests`, `python scripts/generate_ui.py --check`,
`pio test -e native -f test_pins -f test_devicecfg`, then the full `pio test -e native`.
No firmware build needed unless the generated UI header changed (then `pio run -e s3`).
CI on the pushed branch checks the workflow changes; look at its result before the PR.

## Done when

CI fails on a bad version and on a cppcheck high; release runs only from `main`;
`uploadfsota` is gated; pin overrides fail the gate unless flagged; the rule text,
decisions and docs match; finding map rows marked done.

## Outcome (v2.1)

All items landed; nothing deferred. Notes:

- Release `pip install platformio==6.2.0` (the version in use locally; CI used the latest
  before, unpinned). `pytest==9.1.1` is pinned in CI.
- SEC-06 test: `test_browser_password_generation_cannot_disclose_plaintext` in
  `test_devicecfg` already covers the refused browser form, so no new test was added.
- TG-07: Jon's `.env` overrides pins on purpose (12 Nov). He adds
  `USR_MODEM_PINS_OVERRIDDEN=1` to `.env`; until then the pin test fails on his PC.
