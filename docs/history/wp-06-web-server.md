# WP-06 (v2.6): Web server: read-only start, upload hand-shakes, login lock-out

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / medium |
| Size | M-L |
| Branch | `firmware/v2.6-web-server` |
| Depends on | WP-02 (`src/main.cpp`; and STA-01 removes the 2 s stall that triggers STA-07) |
| Firmware change | yes (`lib/hw/src/web_service.*`, `src/main.cpp`, `lib/webinterface`) |

Follow the session workflow and token rules in [README.md](README.md).
`lib/hw/src/web_service.cpp` runs in the HTTP server task; `src/main.cpp` in the loop
task. They share state only through a mutex, bounded queues and snapshots. Keep it so.

## Findings

### STA-02 The browser opens the configuration screen and blocks every call

- `src/main.cpp:1104-1112`: the first browser to connect is put straight into the menu,
  which claims the call slot. Nothing closes an idle screen, so a tab left open turns
  callers away and makes `ATD` give `BUSY`.
- **Decision (Jon, 11 Nov 2026): every browser starts read-only** (status view and
  lights). It takes the configuration screen only when the user presses `E` and the modem
  is free; that path exists at `src/main.cpp:1113-1131`. Remove the automatic claim.
- Docs: `web/PROTOCOL.md` wording "Browsers connected while busy display read-only
  status" becomes "every browser starts read-only; `E` opens the configuration screen
  when the modem is free". README line "Open `http://usr-modem.local` ... a terminal with
  the same configuration screen". Decisions log: implemented.
- Tests: update tests that expect to land in the menu: `test/e2e/test_web.py` (edit only;
  not run) and `web/test/` (run). The page may need a hint ("Press E to configure") in the
  read-only view; check `web/src/` for what it shows now.

### STA-07 Upload hand-shakes can leave stale state

- `lib/hw/src/web_service.cpp:750-768`: the server waits 2 s for the loop, then clears
  `uploadRequest_`. `:678-683`: `allowUpload()` sets `uploadActive_` without checking that
  anyone is still waiting. `src/main.cpp:1059-1076`, `:1299-1303`. If the loop answers
  just after the time-out, `uploadActive_` stays true and `loop()` returns early for ever.
- Second case: `web_service.cpp:801-818` with `src/main.cpp:1043-1057`: a late unmount
  request after a failed upload unmounts LittleFS again.
- **Fix:** the server sets a "waiting" flag under the mutex and clears it on time-out;
  `allowUpload()` and the unmount handler act only while it is set (check and act under
  the same lock). If logic can move into `lib/webinterface` as a small state machine,
  unit-test it in `test_web`; otherwise reason it through carefully and describe the
  interleavings in a comment.

### SEC-02 Unauthenticated requests can stall the web server task

- `web_service.cpp:340-343`: `delay(1000)` after a wrong Digest response, in the single
  server task. `:40-46` with `cfg.recv_wait_timeout = 3` (`:271`): the receive gate blocks
  up to 3 s for the rest of a header. `:572-574`: a browser whose 16 KB output queue fills
  is disconnected.
- **Fix:** replace the delay with a non-blocking lock-out: record the time of the last
  wrong response; for the next second answer `401` at once without checking the password.
  Put the timing rule in `lib/webinterface/src/web_policy.*` and unit-test it in
  `test_web`. Lower `recv_wait_timeout` to 1 s.
- Docs: `web/PROTOCOL.md` (wrong-password behaviour), decisions log (supersedes the v1.0
  "answered after one second" entry), `test/e2e/test_web.py` wrong-password test (edit,
  not run).

### MNT-05f `web/PROTOCOL.md` mismatches

`wifi` is republished every 10 s in every state, not only while up; `npm test` runs five
suites, not only the status bar; statuses 404 and 503 are not listed. Update the text.

## Steps

1. Branch, bump (`minor "Browsers start read-only; web upload and login fixes"`).
2. STA-02 (smallest, decided), with web tests and docs.
3. SEC-02 (policy + test, then the server change).
4. STA-07 (the careful one).
5. MNT-05f.

**Cut line:** STA-07 may move to its own follow-up session (Opus) if STA-02 and SEC-02
used most of the window; it is Low severity once WP-02 has landed.

## End checks

`pio test -e native -f test_web`, full suite, `pio run -e s3`, `pio check`, clang-format,
`node --test test/*.test.mjs` in `web/`. Not run: `e2e/test_web.py` (bench).

## Done when

Opening the page claims nothing; wrong passwords never block the server task; a late
answer from the loop can't leave an upload or unmount half-done; PROTOCOL.md matches.
