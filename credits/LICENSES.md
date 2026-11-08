# Licensing record

The repository is **private** (decision 22 Oct 2026). This record lists every
third-party component so that publication can be revisited later. Update it whenever
a file, library or recording from someone else is added. The firmware's About screen
is generated from the same list.

| Component | Used for | Author / source | Licence | Obligations | Status |
|---|---|---|---|---|---|
| RetroExternalModem case model (`case/`: `parts/` STLs and the two Bambu Studio print projects) | 3D-printed case (modified, with added mounting parts) | luizpacheco, <https://www.thingiverse.com/thing:7063155> | CC BY-NC | Attribute; non-commercial only; modified versions keep the same terms | In repo |
| Front label art (`images/USR*.PNG`, `case/labels/`) | Printed front label | luizpacheco (as above) | CC BY-NC | As above | In repo |
| Component datasheets and models (`electronics/`) | Build reference for the amplifier, speaker, RS232 Pal, HUSB238, DB9 parts, LED and switch | Adafruit, Same Sky and the parts' suppliers (named in each file) | Manufacturers' copyright | Reference only; not part of the firmware | In repo (private) |
| `sound/dial-up.wav`, `sound/busy-signal.wav` (removed) | Former reference sounds | caiot5, RetroExternalModem, <https://github.com/caiot5/retroexternalmodem> | GPL-3.0 | Preserve applicable notices when redistributing historical copies | Removed 27 Oct 2026; never used in firmware |
| Zimodem Telnet handler (`handleAsciiIAC`, adapted) | Incremental Telnet parsing in `lib/calls/src/telnet.*` | Bo Zimmerman, <https://github.com/bozimmerman/Zimodem>, revision `a534e5129d88b8b09aa01fe17498d5cd5368569e`, `zimodem/pet2asc.ino` | Apache-2.0 | Original headers kept; changes marked; LICENSE and NOTICE in `credits/zimodem/` | Used in v0.8 |
| Zimodem PPP/SLIP framing | Portable binary framing in `lib/internet/src/internet.*`; control negotiation and lifecycle rewritten for this project | Bo Zimmerman, same revision, `zimodem/zpppmode.*` and `zslipmode.*` | Apache-2.0 | Attribution/licence retained, modifications marked; existing LICENSE/NOTICE in `credits/zimodem/` | Used in v0.13; shared-IP routing is replaced by private-address NAPT |
| ESP-IDF 4.4.7 lwIP NAPT | Source-built IPv4 translation and forwarding | Espressif Systems and lwIP contributors; IDF `38eeba213a`, lwIP `a45be9e438f6cf9c54ec150581819c3b95d5af6b` | Apache-2.0 (IDF), BSD-style notices in lwIP | Preserve upstream licences/notices; source remains unchanged in the pinned framework package | Used in v0.13 |
| esp_littlefs and littlefs | Source-built filesystem matching Arduino 2.0.17's SDK | Brian Pugh, <https://github.com/joltwallet/esp_littlefs/tree/41873c20fb5cdbcf28d7d6cc04e4bcb4a1305317>; littlefs authors/Arm, <https://github.com/littlefs-project/littlefs/tree/f53a0cc961a8acac85f868b431d2f3e58e447ba3> | MIT (esp_littlefs), BSD-3-Clause (littlefs) | Original source headers retained; licence copies in `credits/littlefs/` | Source built in v0.13; immutable downloads verified by SHA-256 |
| pppd | Independent host protocol/IP-stack test peer, separate executable | PPP project and contributors, <https://github.com/ppp-project/ppp> | Upstream BSD/GPL notices by file | No code linked into firmware; preserve package licensing if redistributing the test tool | v0.13 interoperability peer; installed version recorded with acceptance |
| Arduino-ESP32 core 2.0.x | Firmware framework | Espressif Systems | LGPL-2.1 (core), Apache-2.0 (ESP-IDF) | Allow relinking if distributed as a binary | Used |
| ArduinoOTA and bundled `tools/espota.py` (Arduino-ESP32 core 2.0.x) | Password-authenticated firmware updates; existing uploader called through a local secret-safe wrapper | Espressif Systems and the contributors named in `espota.py` | Distributed with Arduino-ESP32 (above); original files unchanged | Keep upstream notices and framework licensing obligations | Used in v0.7 |
| Mbed TLS (bundled with ESP-IDF) | MD5 keys for OTA/HTTP Digest, upload SHA-256 and CTR-DRBG seeded from the ESP hardware entropy source | Mbed TLS contributors | Apache-2.0 | Keep upstream licence and notices; bundled library unchanged | Used in v0.7 and v0.11 |
| ESP-IDF HTTP server and WebSocket implementation (bundled with Arduino-ESP32) | HTTP port 80, WebSocket framing and streamed browser updates | Espressif Systems, <https://github.com/espressif/esp-idf/tree/v4.4.7/components/esp_http_server> | Apache-2.0 | Keep upstream licence and notices; bundled library unchanged | Used in v0.11; no additional server libraries |
| Time-zone rules (`lib/netcfg/src/net_tz.cpp`, the POSIX rules in the zone table) | Local time for the clock (`AT*TZ`) | IANA Time Zone Database, <https://www.iana.org/time-zones> | Public domain | None | Used |
| Calendar date algorithms (`daysFromCivil`, `civilFromDays` in `net_tz.cpp`) | Date from a UTC timestamp | Howard Hinnant, <https://howardhinnant.github.io/date_algorithms.html> | Public domain | None (credited in the source) | Used |
| Unity test framework | Unit tests | ThrowTheSwitch.org | MIT | Keep copyright notice | Used (tests only) |
| xterm.js 6.0.0 (`web/vendor/xterm.mjs`, `xterm.css`) | Web interface terminal | The xterm.js authors, <https://github.com/xtermjs/xterm.js> | MIT | Keep `web/vendor/LICENSE-xterm.txt` | Used |
| IBM Plex Mono (`web/vendor/ibm-plex-mono-*.woff2`, via Fontsource 5.3.0) | Web interface font | IBM Corp., <https://github.com/IBM/plex> | SIL OFL-1.1 | Keep `web/vendor/LICENSE-ibm-plex-mono.txt`; don't sell the font on its own | Used |
| Zimodem X/YMODEM (`proto_xmodem`) | Incremental serial Client Tests transfers in `lib/clienttest/src/transfer.cpp` | Bo Zimmerman, Zimodem revision `a534e5129d88b8b09aa01fe17498d5cd5368569e` | Apache-2.0 | Header retained, changes marked; existing LICENSE/NOTICE in `credits/zimodem/` | Used in v0.12 |
| Zimodem ZMODEM (`proto_zmodem`), from Synchronet/SyncTERM | Incremental serial Client Tests framing/CRC in `lib/clienttest/src/transfer.cpp` | Jacques Mattheij (1994), rights transferred to Stephen Hurd (2015); via Bo Zimmerman's Zimodem revision `a534e5129d88b8b09aa01fe17498d5cd5368569e` | Not stated in the file; **accepted by Jon, 23 Oct** | Original notice in `credits/zimodem/zmodem-notice.txt`; adaptation marked in source | Used in v0.12 |
| lrzsz | Independent test peer, separate executable; no code linked into firmware | Uwe Ohse and contributors, <https://www.ohse.de/uwe/software/lrzsz.html> | GPL | Preserve package licensing when distributing the test tool; not bundled in firmware | Host interoperability tests in v0.12 |
| USR Sportster 33.6 command behaviour, defaults and `ATI` text (`lib/atcmd`, `lib/modemcfg`) | Authentic modem identity and command set (brief 6.6, Q55) | U.S. Robotics: Sportster technical reference (<https://support.usr.com/support/sportster/sportster-files/techref_uk.pdf>) and Sportster user guide (copy at <https://www.rigacci.org/docs/biblio/online/usr/sportster_guide.txt>); `ATI3`–`ATI7`/`ATI9` output of a Sportster 33600/Fax V2.31 posted in the fido7.ru.modem echo (<https://fido7.ru.modem.narkive.com/YzTdiljZ/sportster-33600>) | Facts and short identification strings, re-implemented (no code copied) | None beyond this note; the About screen should name U.S. Robotics as the source of the command set | Used |
| `modem1200.wav` (HQ MP3 preview, converted; removed) | Original 1200-baud handshake asset | guitarguy1985, <https://freesound.org/people/guitarguy1985/sounds/78657/>; former source `sound/sources/modem1200-78657.mp3` | CC0 1.0, <https://creativecommons.org/publicdomain/zero/1.0/> | None; historical attribution retained for provenance | Used in v0.8-v0.14; source and packaged recording replaced 27 Oct 2026 |
| `Dial up modem noises.ogg` (removed) | Original generic handshake fallback for five missing speed recordings | William Termini, <https://commons.wikimedia.org/wiki/File:Dial_up_modem_noises.ogg>; former source `sound/sources/dial-up-modem-noises.ogg` | Public-domain dedication by copyright holder | None; historical attribution retained for provenance | Used in v0.8-v0.14 with Jon's approval; source and packaged recordings replaced 27 Oct 2026 |
| `sound/1200bps.wav`, `sound/2400bps.wav`, `sound/9600bps.wav`, `sound/14400bps.wav`, `sound/28800bps.wav`, `sound/33600bps.wav`, `sound/56000bps.wav` | Modem handshake recordings for playback and downloads | Gough Lui, Gough's Tech Zone, "Project: THE DEFINITIVE COLLECTION of V.90/V.92 Modem Sounds" (3 May 2016), <https://goughlui.com/2016/05/03/project-the-definitive-collection-of-v-90v-92-modem-sounds/> | Audio reuse licence not stated on the linked page | Retain attribution and source; redistribution terms remain unspecified | Used in v0.15; supplied by Jon on 27 Oct 2026 as mono 8000 Hz signed 16-bit PCM WAVs |

Concept credits (no code used): Luis Miguel Silva (WiFi2DialUp), Matt Whitted (PodSix
pimodem), Jim Brain and FozzTexx (tcpser).

## Publication notes

### v0.8 handshake assets

The original 1200-baud recording is credited in the table above. The five other speed files
used William Termini's [Dial up modem noises.ogg](https://commons.wikimedia.org/wiki/File:Dial_up_modem_noises.ogg),
released into the public domain by its copyright holder on that page. The original
was retained as `sound/sources/dial-up-modem-noises.ogg` until 27 Oct 2026.

The v0.8-v0.14 `scripts/build_handshakes.py` used FFmpeg to rebuild all six WAVs as
mono 8000 Hz signed 16-bit PCM, without metadata. The five generic fallback files were
identical copies of the 9.9-26.8 second segment, removing recorded dialing/ringing
and final silence. Their filenames selected an emulator sound; they were not verified
recordings of 2400, 14400, 28800, 33600 or 56000 negotiated rates. Jon approved this
explicit fallback on 25 Oct 2026. The original sources were removed on 27 Oct 2026;
this describes the historical conversion and is no longer reproducible from the
current working tree.

### Replacement recordings (27 Oct 2026)

Jon supplied the seven WAVs in `sound/` and identified Gough Lui's collection above
as their source. Keep that attribution with the replacement assets. The linked
article does not state an audio reuse licence; no CC0 or public-domain claim is
made for these recordings. Individual original download names were not supplied.

All seven supplied files are mono 8000 Hz signed 16-bit PCM. The 1200/2400,
9600/14400 and 28800/33600 pairs are byte-identical. Their filenames describe Jon's
selected playback assignments, not independently verified negotiated rates.
`python scripts/build_handshakes.py` validates all seven inputs, then copies them
unchanged to `data/sounds/handshake-<rate>.wav`. No further conversion or trimming
is applied. The rates are 1200, 2400, 9600, 14400, 28800, 33600 and 56000.
Playback and Client Tests downloads share that catalogue; ALL includes all seven.
Intermediate operational rates use the nearest lower asset rate; 300 uses 1200.
Model selection does not change this mapping.

### v0.8 identity and extended speed references

The Sportster 14400 identity is modeled from the USR product name and inquiry
semantics in the [USR Sportster High Speed Modems Technical Reference Guide](https://www.rigacci.org/docs/biblio/online/usr/sportster_guide.txt)
(manufacturer-authored guide, mirrored). The earlier 33600 capture remains the
source for settings-screen layout, rather than a source for 14400 ROM details.
No verified complete ROM capture is used for the four selectable series. Product
codes `1441`, `2881`, `3361`, `5601` and checksum `0000` are emulator placeholders.
`ATI3`/`ATI9` return the selected series label; `ATI7` uses a corresponding identity
option label but marks physical ROM/DSP information as emulated or unavailable.
The model does not restrict firmware capabilities.

Speed indexes `&N1`-`&N38` and `&U1`-`&U38`, and numeric CONNECT results, follow
the [USR V.90 Technical Quick Reference](https://support.usr.com/support/s-modem/s-modem-docs/v90tecrf.pdf),
printed pages 10-11 and 15-18. Existing indexes 1-16 are preserved. The later
indexes are not in increasing speed order; 38 selects 56000. These are factual
command mappings reimplemented in this project; no code or manual pages are bundled.

If the repository is ever made public, the mixed licences need a decision first:
CC BY-NC (case) forbids commercial use, the `electronics/` datasheets are the
manufacturers' copyright, the replacement recordings' redistribution
terms are unspecified, and historical GPL-3.0 reference sounds retain their
original licensing requirements if redistributed.
