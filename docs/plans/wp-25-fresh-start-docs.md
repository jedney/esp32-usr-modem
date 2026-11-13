# WP-25 (v3.0): Fresh start, documentation and credits

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | L |
| Depends on | WP-24 |

Read [wp-19-common.md](wp-19-common.md) first (decisions, versions, board testing, rules).

## Steps

1. **Delete** `docs/history/` (this includes the WP-18 spike plan) and, as the last step
   of WP-19 (after WP-26 has been done), `docs/plans/`. Fold anything still useful, such
   as the session workflow and token rules, into CLAUDE.md.
2. **`docs/decisions-log.md`:** rewrite as the current decisions only, one row each,
   superseded rows gone (the file stays, and new decisions keep being appended).
3. **`CHANGES.md`:** single line summary of v1, v2 and v3. Other entries removed; `scripts/bump_version.py` and `check_version.py`
   still work.
4. **README, `docs/COMMANDS.md`, `AT*HELP`, `test/README.md`, `web/PROTOCOL.md`,
   `docs/ROADMAP.md`:** current behaviour only; no "from vX" notes; ROADMAP becomes what
   is left to do after v3.0 (or goes).
5. **`credits/LICENSES.md` and `credits/`:** only what is in the v3.0 build or the repo
   (Arduino-ESP32 3.3.12, IDF 6.1 with lwIP, HTTP server, Mbed TLS 4 / TF-PSA-Crypto,
   TinyUSB 0.21.0, joltwallet/littlefs, the managed components that end up linked,
   Zimodem parts still used, the sound files in `sound/`, xterm.js, IBM Plex Mono, Unity,
   test peers). Remove rows and licence files for removed sounds, the old pinned LittleFS,
   Arduino 2.0.x and IDF 4.4, and the historical publication notes. Regenerate the
   About-screen credits (`credits_generated.h`).
   - The LittleFS row is now `joltwallet/littlefs` 1.23.1 from the component registry (no
     `credits/littlefs/` download any more).
   - The Mbed TLS row no longer uses CTR-DRBG: it is MD5, SHA-256, HMAC-SHA-256 and the
     hardware noise source through PSA.
6. **`ui/`:** keep what `scripts/generate_ui.py` and the tests use and the current
   design spec; remove the Codex prompt, reference images and previews that only record
   how the design came about.
7. **`docs/project-brief.md` → current firmware spec** (decision 7). Keep: the goal, the
   hardware the firmware drives (from section 3), the firmware spec (section 6) and the
   firmware side of the interfaces (section 7), written as the code now behaves; the
   attribution (section 8); only the open questions that are still open. Remove: the case
   and electronics workstreams (sections 4 and 5; `case/` and `electronics/` hold their
   own docs), answered questions, *Proposed* items that were decided (write the decision
   in), and anything the code no longer does. Rename the title to say it is the firmware
   spec. Then grep `lib/`, `src/`, `test/`, `scripts/`, `web/` and the docs for "brief"
   section references and fix each one against the new numbering.
8. **CLAUDE.md:** current project only (no review, history or old plan pointers; build
   table for IDF 6.1; hardware summary). The brief entry becomes "the firmware spec: keep
   it current with the code"; remove the "don't edit" rule and "Jon merges the log back".
9. **`images/`, `data/`, `.vscode/`, `.github/`:** remove anything unused.
10. End checks; `git status` before committing.

Note: WP-26 still edits the docs after this session. It writes current behaviour
only, in the trimmed files.
