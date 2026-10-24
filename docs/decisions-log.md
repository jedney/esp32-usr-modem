# Decisions log

New decisions and spec changes made while coding in this repo, newest last. The project
brief (`docs/project-brief.md`) is a snapshot of a master copy shared with the Case and
Electronics workstreams, so it isn't edited here. Jon brings this log back to the
Firmware chat, which merges it into the master brief.

Format: date, version, what was decided, why, and who decided (Jon / proposed by Claude).
Mark items that affect the electronics or the case **[Electronics]** or **[Case]** so
Jon can raise them in those chats.

| Date | Version | Decision | Why | By |
|---|---|---|---|---|
| 2026-10-24 | — | Firmware work moves to Claude Code in VS Code, following `CLAUDE.md` and `docs/ROADMAP.md` | Hand-over from the Firmware chat | Jon |
| 2026-10-24 | — | **Where the hardware tests run (Q79):** USB console and web page run all tests, with the RS232 line, loopback and bridge tests only while the modem is idle; an RS232 session runs everything except those three (shown as unavailable with the reason); a remote caller runs none. Already merged into the master brief (6.14) | The three RS232 tests take over the port an RS232 session uses; the web page doesn't use that port | Jon (proposed by Claude) |
| 2026-10-24 | v0.4 | **`ATI0`–`ATI7` text** comes from a capture of a real "Sportster 33600/Fax V2.31" (`ATI3`–`ATI7`, `ATI9`) and the USR technical reference. `ATI0` (`3361`) and `ATI1` (`C2A4`) are **placeholders**: no capture was found; replace them if a real unit's output turns up | Brief 6.6, Q55: authentic identity so old operating systems detect the modem | Jon (sources found by Claude) |
| 2026-10-24 | v0.4 | Factory result-code set is **`X4`** (all codes). This is also the real Sportster's default | `BUSY` and `NO DIAL TONE` (6.5) show without setup | Jon |
| 2026-10-24 | v0.4 | **Spec correction to 6.5:** the result text is **`NO DIAL TONE`** (code 6), not `NO DIALTONE` | Authentic USR text; terminal scripts match on it | Jon |
| 2026-10-24 | v0.4 | All Sportster settings (`&A &B &C &D &G &H &I &K &M &N &P &R &S &T &U &Y`, `B E F L M Q V X`, P/T) are parsed, stored and shown now; each takes effect in its roadmap version | Keeps the saved-settings layout stable | Jon |
| 2026-10-24 | v0.4 | **Two stored templates, as on the Sportster:** `&W0`/`&W1` write Y0/Y1, `Yn` picks the power-on template (Y2–Y4 = factory &F0–&F2) and saves at once, `Z0`–`Z5` reset to them. As shipped, Y0 = &F1 and Y1 = &F2. Four stored numbers `&Z0`–`&Z3` (for `ATDSn` in v0.8), separate from the phonebook | The real modem works this way (user guide, `ATI5` capture); the plan assumed one profile | Proposed by Claude |
| 2026-10-24 | v0.4 | **`AT&V` is a project addition** (the Sportster has no `&V`): it shows `ATI4` (current) then `ATI5` (stored) | Roadmap asks for `&V`; old Hayes users expect it | Proposed by Claude |
| 2026-10-24 | v0.4 | **`AT&F` = `AT&F0`**, the Sportster's low-performance template (no flow control, X1). The project defaults are **`&F1`**, which is what Y0 holds as shipped. Brief 6.7 says "`AT&F` loads the factory settings"; **Jon to confirm** that `AT&F1` is the command to document for "back to defaults" | Hayes rule: a missing number is 0; that is what a real Sportster does | Proposed by Claude, closed (see below) |
| 2026-10-24 | v0.4 | **Open question for v0.5 — `&B` meaning:** on the Sportster, `&B0` = serial speed follows the connection speed and `&B1` = fixed speed (default; the speed is always detected from "AT"). Brief 6.2 (Q1) uses `&B0` = detect from "AT" (default) and `&B1` = fixed 38400. v0.4 stores `&B0`–`&B2` with the brief's meaning and keeps a speed per template (`BAUD=`, default 38400). Decide before v0.5 builds the RS232 port | The two meanings differ; software that sends `AT&B1` expects USR behaviour | Proposed by Claude, **open** |
| 2026-10-24 | v0.4 | Factory `&A0` (plain `CONNECT 33600`), tone dialling, `&P1` (UK pulse ratio). The Sportster ships with `&A3`, pulse dialling and `&P0` | Brief 6.2 (Q34), 6.4 (UK tones) | Proposed by Claude |
| 2026-10-24 | v0.4 | `E`, `Q`, `V`, `&C`, `&D` are stored in the templates (on a real Sportster they come from DIP switches). `ATI4` also shows `L`; `ATI5` also shows `E L Q V &C &D`; `ATI5` doesn't pause with "Strike a key when ready" | No DIP switches in this build; `ATL` sets the volume here (brief 6.4) | Proposed by Claude |
| 2026-10-24 | v0.4 | Command lines may be **128 characters** (Sportster: 58). DEL (0x7F) deletes as well as S5 | Room for `AT*` arguments (host names, Wi-Fi names); many terminals send DEL for Backspace | Proposed by Claude |
| 2026-10-24 | v0.4 | Saved settings: NVS namespace `modem`, key `cfg`, one blob with magic `USRM`, layout version 1 and a CRC-32. A blob from another layout version is ignored (factory settings, banner says so) until a migration is written | Simple, safe against half-written or old data | Proposed by Claude |
| 2026-10-24 | v0.4 | `ATD`, `ATA` and `ATO` give `ERROR` until calls exist (v0.5/v0.8/v0.9); `ATH0`/`ATH1` give `OK` | Init strings often contain `ATH0` | Proposed by Claude |
| 2026-10-24 | v0.4 | **Tooling:** the PC unit tests need GCC on Windows. Installed WinLibs GCC 14.2 (with clang-format 19) for Jon's user with winget; native tests link statically on Windows (`scripts/native_link.py`) so they still run during an upload | The PC had no compiler, so the upload gate could never pass | Jon |
| 2026-10-24 | v0.4 | **`AT&F` confirmed as `AT&F0`**, as on the Sportster. **`AT&F1`** is the command to document for "back to the project defaults" (brief 6.7 wording to follow) | Authentic behaviour; software that sends `AT&F` expects the USR result | Jon |
| 2026-10-24 | v0.4 | v0.4 checked on the bench (bare board, console COM15): on-board `test_board` 5/5, end-to-end 34 passed (RS232 bridge test skipped, no RS232 port), `AT&V` shows the saved template after a reset. `test_hw` not run: amp, LEDs and pot not wired | Roadmap v0.4 "done when" | Jon |
