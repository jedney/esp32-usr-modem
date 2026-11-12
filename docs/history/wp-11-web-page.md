# WP-11 (v2.11): Web page tests and flows

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / medium |
| Size | M |
| Branch | `firmware/v2.11-web-page` |
| Depends on | WP-06 (read-only start and protocol changes) |
| Firmware change | none expected (web page, Python and JS tests, e2e cases) |

Follow the session workflow and token rules in [README.md](README.md). Web tests:
`node --test test/*.test.mjs` in `web/` (Node path in the README). The page is plain
JavaScript; `web/vendor/xterm.mjs` is vendored: don't touch it.

## Findings

- **TG-14** `web/src/app.js` (message handling, the whole upload flow) has no tests;
  `web/test/upload.test.mjs:11-20` "conflicting identity" never builds a conflict.
  **Fix:** move the upload flow (preflight, hashing, target check, sending, result) into
  a module (e.g. `web/src/upload_flow.js`) with its browser calls passed in, so Node can
  test it; `app.js` keeps the wiring. Add a real conflicting-identity case and tests for
  each step's failure. Update `scripts/build_web.py` and the firmware's file list
  (`lib/hw/src/web_service.cpp:379-391`) if a new file is added; that list needs a
  firmware build (`pio run -e s3`).
- **5.5-17** (confirm in the new tests) `web/src/app.js:267-303`, `web/src/link.js:57-61`:
  the upload button can be pressed twice during the preflight; other tabs reconnect in a
  loop during an upload; the page does not reload itself after an update. Fix: disable
  the button for the whole flow; back off reconnects while the modem reports an upload;
  reload after a successful update once the modem is back.
- **MNT-05b** `web/src/demo.js:29-33`, `:39`: box-drawing characters were replaced by `?`
  (the file has no non-ASCII bytes), so demo borders draw `?????`. Restore them as `\u`
  escapes (CP437 single/double line set: `┌ ─ ┐ │ └ ┘`
  etc.; match what the firmware menu draws). Extend `web/test/demo.test.mjs`.
- **MNT-05h** The web file list is kept in three places with no test that they agree:
  `scripts/build_web.py:24-38`, `lib/hw/src/web_service.cpp:379-391`,
  `web/src/app.js:11-15`. Add one pytest in `test/scripts/` that parses all three and
  compares them.
- **TG-15** No tests at any level for: the three-browser limit, connection floods on
  ports 23 and 80, an interrupted upload, the wrong-password lock-out end to end, USB
  unplug during a call. Add e2e cases in `test/e2e/test_web.py` / `test_usb.py` behind
  opt-in flags (`--flood-tests`, `--upload-tests`; check `conftest.py` for existing
  flags). Not run here; `--collect-only` only.

## Steps

1. Branch, bump (`minor "Web page upload flow tests and fixes"`).
2. MNT-05b and MNT-05h (small).
3. TG-14 refactor with tests, then 5.5-17.
4. TG-15 e2e cases.
5. `web/PROTOCOL.md` if behaviour changed; `test/README.md`.

**Cut line:** TG-15 can move to a follow-up.

## End checks

`node --test test/*.test.mjs` in `web/`, `python -m pytest test/scripts -q`,
`pytest test/e2e --collect-only -q`, `pio run -e s3` only if the firmware file list
changed, `python scripts/check_version.py`.

## Done when

The upload flow is tested in Node, the demo draws real borders, the three file lists are
checked by a test; finding map updated.
