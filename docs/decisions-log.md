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
