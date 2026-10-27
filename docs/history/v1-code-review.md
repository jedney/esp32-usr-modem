# Pre-v1 code review

Reviewed 27 October 2026, against v0.13, commit `41e7527`.

The findings below describe the original v0.13 baseline. Their v0.14 fixes and
retest results are tracked in [v0.14 validation](v0.14-validation.md).

Original recommendation: fix the seven issues below before tagging v1.0. The main roadmap
features have implementations, but passing the existing tests does not cover the
failure paths reproduced here. No firmware source, saved device settings, passwords,
or installed images were changed during this review.

## Findings

### R1 - P1: serial adapters discard unconsumed PPP input

Locations: `src/rs232.cpp:175`, `src/main.cpp:803`,
`lib/hostport/src/session.cpp:142`, `lib/internet/src/internet.h:39`.

Both UART adapters read a chunk based on `Session::hostSpace()`, then ignore the
number returned by `Session::fromHost()`. That capacity is not a reservation. PPP
processing can generate a reply while consuming the chunk, reducing its output
queue space below the engine's 4096-byte admission threshold. The session then
returns early. The adapter has already removed the remaining bytes from the UART
and silently discards them.

Reproduced using the actual native engine/session: establish PPP, fill its reply
queue with LCP Echo replies, drain 32 bytes, then offer the advertised 256 bytes.
The session consumes only **50 of 256 bytes**. The current hardware adapter would
discard the remaining **206 bytes**. This requires reply-queue pressure, for example
while the serial receiver is paused; it is not a claim that ordinary HTTP payload
always loses data.

Fix: retain unconsumed UART input in a bounded pending buffer and retry it before
reading more, or feed bytes only when the next byte can be accepted. Respect the
returned count in command-mode transitions too. Test with backpressure changing
inside a chunk, through both serial paths, including RTS pause/resume.

### R2 - P2: browser network changes can roll back immediately

Locations: `lib/netcfg/src/netcfg.cpp:126`, `src/main.cpp:1215` and `1245`.

The main loop samples `now` before processing browser commands. A command starts
the network rollback guard using a newer `millis()`. Later in the same iteration,
`net.tick(now)` receives the older timestamp. Unsigned `nowMs - browserAt_` then
looks much larger than 60 seconds. A DHCP/static change that is reconnecting is
rolled back immediately instead of receiving its promised connection window.

Reproduced: start the guard at 101 ms, set a valid static address, then tick at
100 ms. The candidate changes back to DHCP immediately. Existing tests advance
time monotonically and miss this actual main-loop ordering.

Fix: use a fresh timestamp after processing commands and make the controller
reject stale ticks consistently. Add this ordering test alongside the existing
60-second and timer-wrap tests.

### R3 - P2: overlapping Wi-Fi password prompts use the wrong SSID

Locations: `lib/netcfg/src/net_commands.cpp:25`, `173`, `298`.

The target SSID is global. `pendingOp` is claimed only after the password has been
entered. While port A waits at its password prompt, port B can start another join
and overwrite `joinSsid`. Port A's password is then used to join port B's network.
Configuration-menu exclusivity does not prevent direct console/RS232 commands.

Reproduced with two production AT parser ports:

1. A sends `AT*WIFIJOIN="FirstNet"` and waits for a password.
2. B sends `AT*WIFIJOIN="SecondNet"` and waits for a password.
3. A supplies a synthetic password; the radio is asked to connect to **SecondNet**.

Fix: reserve the join operation when opening the password prompt, associate its
SSID with the requesting port, and release ownership on cancellation, timeout,
disconnect, or completion. Test two local ports plus browser/local overlap.

### R4 - P2: advertised sound downloads and ALL batches fail

Locations: `lib/hw/src/client_platform.cpp:6`, `scripts/build_handshakes.py:39`,
`README.md:448`.

The transfer source catalogue includes `/sounds/handshake-9600.wav`, which the
sound build does not create. `ALL` checks every source before starting, so that
missing file prevents the entire YMODEM/ZMODEM batch. The actual 56000 WAV is
omitted from the catalogue.

Confirmed against the deployed modem on COM15: both `AT*SY=HS9600.WAV` and
`AT*SY=ALL` return `ERROR`. The local sound directory contains 1200, 2400, 14400,
28800, 33600 and 56000 assets, matching the generation script.

Fix: use one sound catalogue for generation, playback, transfer names, and help.
Either replace the erroneous entry with 56000 or explicitly map a documented
9600 alias to its playback fallback. Verify all advertised individual sources and
both ALL batch protocols against the real filesystem.

### R5 - P2: PPP renegotiation inherits an expired startup timeout

Locations: `lib/internet/src/internet.cpp:245`, `386`, `519`.

An established connection can return to negotiation after a new LCP Configure
Request or IPCP Terminate Request. These paths clear readiness without resetting
`started_`. The next poll applies the original 30-second startup deadline. A call
older than 30 seconds therefore disconnects immediately instead of renegotiating.

Reproduced: establish PPP, poll at 60000 ms, receive a valid LCP Configure Request,
then poll at 60001 ms. The engine closes with the timeout path.

Fix: start a fresh negotiation deadline and retry budget on each supported
renegotiation transition. Test both LCP and IPCP after a long-established call.

### R6 - P2: failed saves still apply settings and can disconnect Wi-Fi

Locations: `lib/netcfg/src/netcfg.cpp:311` through `405`,
`lib/atcmd/src/atcmd.cpp:92` through `125`.

Several setters mutate live state before saving and retain the mutation on
failure. Network setters also perform their radio side effects after a failed
save. The caller gets `ERROR` although the setting took effect. A later unrelated
successful save can persist a change previously reported as rejected.

Reproduced: start connected, inject a store-save failure, call
`setEnabled(false)`. It returns false but disables Wi-Fi anyway. The same ordering
exists for DHCP/static address changes, forgetting the network, and several modem
NVRAM setters. Model/answer-port/throttle setters already demonstrate rollback.

Fix: validate and persist a candidate before committing live state or changing
the radio; restore the prior state on failure. Keep the browser transaction's
intentional deferred persistence explicit. Add fault-injection checks for each
setter family, including a subsequent successful unrelated save.

### R7 - P2: oversized NVS records are mistaken for missing records

Location: `lib/hw/src/nvs_store.cpp:8`.

The adapter truncates an oversized stored length to the destination capacity and
calls `Preferences::getBytes()`. Arduino 2.0.17's implementation returns **zero**
when the stored blob is larger than that buffer; it does not partially read it.
The adapter consequently reports an empty store rather than damaged or
unsupported data. The native memory-store fakes have different behavior.

Consequences depend on the namespace: web settings can default back to enabled;
device credentials can be regenerated and overwrite the old record. A larger
future layout followed by a downgrade is another trigger besides malformed NVS.

Verified against the installed framework's
`libraries/Preferences/src/Preferences.cpp:509`, including its explicit
`len > maxLen` return-zero branch. No production NVS corruption was injected.

Fix: distinguish absent, invalid/oversized, and I/O-error results. At minimum,
return an unambiguously invalid nonempty result without calling `getBytes` with
an undersized buffer, and check each caller's length contract. Add an adapter test
whose fake reproduces the actual Preferences semantics.

## Security review

The reviewed custom code has clear separation between caller payload and local
command input. Incoming TCP/Telnet data does not feed the AT parser; local escape
keeps remote input in data queues. I found no remote command-mode or unauthenticated
administration bypass in the paths reviewed. This is a code-review finding, not
a security certification or exhaustive fuzzing result.

The HTTP layer checks Host and Origin, authenticates assets and API requests,
uses expiring single-use WebSocket tickets, checks upload CSRF and menu ownership,
and bounds headers, frames, queues, image size and upload duration. Digest checks
method, URI, nonce expiry and replay count. The static asset allowlist avoids
filesystem path traversal. Export and structured logging avoid credential dumps.
Password input masking and the final audio cap have dedicated shared paths.

R3, R6 and R7 need attention at the configuration/persistence boundary. In
particular, damaged storage must not silently re-enable a deliberately disabled
service or replace an existing credential.

The pinned ESP-IDF **4.4.7** branch is out of security maintenance. Espressif's
[v4.4 end-of-life advisory](https://www.espressif.com/sites/default/files/advisory_downloads/AR2024-008%20End-of-Life%20Advisory%20for%20ESP-IDF%20v4.4%20Release%20Branch%20EN.pdf)
states that the branch reached EOL in July 2024 and no longer receives security
fixes. Core 2.0.17 is an explicit project decision; keep this dependency risk
visible and plan a separate supported-SDK migration with UART, audio, NAPT and
update regression coverage. This review does not establish a specific exploitable
SDK CVE in the enabled build, or complete an upstream dependency vulnerability audit.

Existing agreed limits remain: HTTP does not encrypt traffic or protect against
an active LAN interceptor; Digest/password use does not change that. Image markers
plus client-supplied SHA-256 detect wrong/corrupted files, not publisher authenticity.
NVS is unencrypted, and port 23 intentionally admits callers without a password.
These are documented design choices, not newly discovered implementation defects.

## ESP32 resource use and simplification

Measured release build: **1,222,565 bytes flash**, **139,520 bytes static RAM**.
That is 29.1% of the 4 MiB app slot and 42.6% of PlatformIO's 320 KiB RAM accounting.
Static RAM is not peak runtime usage.

Read-only `AT*HWINFO` on the existing COM15 firmware reported approximately
**107 KiB free heap**, **7,658 KiB free PSRAM**, and **1,636 KiB used LittleFS**.
This is one idle sample, not a minimum-free measurement during calls/uploads.

Prioritize these changes by benefit:

| Opportunity | Evidence / likely saving | Suggested scope |
|---|---|---|
| Put WebService client queues in explicitly allocated PSRAM | ELF symbol `webServer` is 56,568 bytes of internal BSS; queues alone are 55,296 bytes, including observers | Biggest direct internal-RAM improvement. Retain bounded sizes, locking and allocation-failure handling; avoid PSRAM allocation before initialization |
| Move phonebook/history buffers to PSRAM | ELF BSS: phonebook 10,960 bytes, history 10,176 bytes | About another 20.6 KiB of internal RAM, with simple ownership; preserve storage rollback and bounded buffers |
| Avoid rebuilding unchanged browser status each loop | `publishWebStatus()` always constructs call JSON, even with web disabled or no clients; `pump()` schedules work every 20 ms without checking whether clients/data need it | Gate on active consumers and update on changes or a modest fixed interval; retain fresh initial snapshots |
| Reuse the menu rendering workspace | Three terminal objects each contain a 32 KiB response buffer, a 132x50 cell grid and 4 KiB staging buffer; only one menu owns the slot | Roughly 98 KiB PSRAM saving from sharing two redundant workspaces; retain separate per-port preferences. Larger refactor, lower pre-release priority |
| Deduplicate identical handshake files | Five fallback WAVs are byte-identical at 270,444 bytes each | One shared fallback plus aliases saves 1,081,776 filesystem bytes, about 1.03 MiB. Keep per-speed replacement possible and fix R4's catalogue first |
| Reduce HTTP parser allocations | Header counts and Digest fields use `std::map` for small bounded collections | Fixed arrays/recognized-field structs can simplify bounded parsing and reduce short-lived allocations. Preserve duplicate-field rejection and all auth tests |
| Sample volume at a controlled cadence | `CallAudio::poll` reads 16 ADC samples and calculates gain even when nonblocking I2S cannot accept another block | Cache pot/gain at about 20 ms intervals and preserve the final cap. Measure loop latency and audio before/after |
| Share bounded numeric parsing where it helps | Many separate `sscanf`/decimal parsers; ELF includes substantial general scanf support | Consider after correctness fixes. Measure actual link-size changes; do not replace tested code merely to reduce line count |

The menu and debug log already use explicit PSRAM allocation. The SDK also uses
a 16 KiB malloc threshold, so large `new` allocations may already land in PSRAM;
do not count relocating those as an internal-RAM saving without measuring.

There is no flash-capacity reason to remove PPP, transfers, web configuration or
other requested features. Buffer placement and avoiding repeated work offer more
useful gains than deleting features. Do not shrink task stacks or TCP/UART queues
without stack-watermark, minimum-free/largest-block and backpressure measurements
during PPP negotiation, uploads, audio, and multiple browser connections.

## Roadmap completeness

| Roadmap area | Review result |
|---|---|
| Parser/settings, results, model identity, phonebook | Present; persistence issues R6/R7 remain. ATI identity placeholders are already documented |
| UART/autobaud, control lines, escape, flow control | Present; UART input handling needs R1 |
| Wi-Fi, mDNS, NTP/time zones | Present; R2/R3/R6/R7 affect edge/failure cases |
| Password, OTA, factory reset | Present; R7 affects the credential read path |
| Outgoing/incoming calls, Telnet/raw, sounds/volume, panel logic | Present; physical listening/panel acceptance remains distinct from source validation |
| Shared terminal/browser configuration, logs/history/export, hardware tests | Present; shared registry/parity tests exist. R2 affects browser network settings |
| Client tests and X/Y/ZMODEM | Present; R4 makes advertised sound/ALL downloads incomplete in the deployed image |
| PPP/SLIP and NAPT | Present; R1 and R5 need fixes |
| v1 production acceptance | Not established by this review; final enclosure, physical checks and Jon's sign-off remain |

The roadmap and plans still explicitly defer real Amiga terminal/transfer and
Windows 9x/Amiga browsing acceptance. PuTTY visual observation, loopback-plug and
speaker/panel checks also appear as unfinished historical acceptance items.
If already completed outside the repository, reconcile those records before the
v1 sign-off; source/tests cannot establish physical or unavailable-client behavior.
Some README/test-matrix wording still says hardware is unwired or features are
future work despite the later acceptance notes. Consolidate that status for v1.

## Validation and reproducibility

Fresh checks performed during this review:

- Native suite: **376/376 passed**.
- Release firmware build: **passed**, without uploading.
- Browser unit tests: **22/22 passed**, using installed Node 24.19.0.
- Script/layout pytest run: **17 passed, 7 subtests passed**.
- cppcheck: **passed**, zero high/medium defects; low-severity reports remain.
- clang-format dry run: **passed**.
- Version/changelog check and generated-UI consistency: **passed**.
- COM15: read-only hardware/memory query and the two failing source-selection
  commands above. No firmware upload, factory reset or credential/network change.

Additional review probes use the existing native fixtures and link the compiled
production libraries. They are stored locally in ignored `.pio/review-v1/`;
run `python .pio/review-v1/run.py` after `pio test -e native`. Their current nonzero
exit is intentional evidence of the findings, not a failure of the original suite.
The runner and probes are local review artifacts, not committed regression tests.

| Probe | Current result |
|---|---|
| Older tick after starting browser network guard | FAIL - immediate rollback, R2 |
| Two simultaneous SSID/password prompts | FAIL - wrong SSID, R3 |
| Second completed prompt cannot replace an already running join | PASS - controller rejects it |
| Save failure must retain enabled Wi-Fi | FAIL - Wi-Fi disabled despite failure, R6 |
| LCP renegotiation after a 60-second established session | FAIL - immediate timeout, R5 |
| Session consumes its advertised PPP input capacity under reply pressure | FAIL - only 50/256 consumed, R1 |

The existing on-board, full end-to-end, independent legacy-peer and destructive
update suites were not rerun. They already have recorded acceptance, and no
firmware changes were made. After fixing these findings, integrate the targeted
regressions, run the normal checks and appropriate wired acceptance, and reinstall
normal firmware if any embedded test programs are deployed.
