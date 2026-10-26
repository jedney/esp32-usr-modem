# Licensing record

The repository is **private** (decision 22 Oct 2026). This record lists every
third-party component so that publication can be revisited later. Update it whenever
a file, library or recording from someone else is added. The firmware's About screen
is generated from the same list.

| Component | Used for | Author / source | Licence | Obligations | Status |
|---|---|---|---|---|---|
| RetroExternalModem case model (`3d-model/`) | 3D-printed case (modified) | luizpacheco, <https://www.thingiverse.com/thing:7063155> | CC BY-NC | Attribute; non-commercial only; modified versions keep the same terms | In repo |
| Front label art (`images/USR*.PNG`) | Printed front label | luizpacheco (as above) | CC BY-NC | As above | In repo |
| `sound/dial-up.wav`, `sound/busy-signal.wav` | Reference sounds | caiot5, RetroExternalModem, <https://github.com/caiot5/retroexternalmodem> | GPL-3.0 | Source must be offered if distributed; GPL applies to the combined work if bundled in firmware | In repo; not yet used in firmware |
| Zimodem Telnet handler (`handleAsciiIAC`, adapted); PPP/SLIP later | Incremental Telnet parsing in `lib/calls/src/telnet.*` | Bo Zimmerman, <https://github.com/bozimmerman/Zimodem>, revision `a534e5129d88b8b09aa01fe17498d5cd5368569e`, `zimodem/pet2asc.ino` | Apache-2.0 | Original headers kept; changes marked; LICENSE and NOTICE in `credits/zimodem/` | Telnet used in v0.8; PPP/SLIP planned |
| Arduino-ESP32 core 2.0.x | Firmware framework | Espressif Systems | LGPL-2.1 (core), Apache-2.0 (ESP-IDF) | Allow relinking if distributed as a binary | Used |
| ArduinoOTA and bundled `tools/espota.py` (Arduino-ESP32 core 2.0.x) | Password-authenticated firmware updates; existing uploader called through a local secret-safe wrapper | Espressif Systems and the contributors named in `espota.py` | Distributed with Arduino-ESP32 (above); original files unchanged | Keep upstream notices and framework licensing obligations | Used in v0.7 |
| Mbed TLS (bundled with ESP-IDF) | MD5 keys for OTA/HTTP Digest, upload SHA-256 and CTR-DRBG seeded from the ESP hardware entropy source | Mbed TLS contributors | Apache-2.0 | Keep upstream licence and notices; bundled library unchanged | Used in v0.7 and v0.11 |
| ESP-IDF HTTP server and WebSocket implementation (bundled with Arduino-ESP32) | HTTP port 80, WebSocket framing and streamed browser updates | Espressif Systems, <https://github.com/espressif/esp-idf/tree/v4.4.7/components/esp_http_server> | Apache-2.0 | Keep upstream licence and notices; bundled library unchanged | Used in v0.11; no additional server libraries |
| Time-zone rules (`lib/netcfg/src/net_tz.cpp`, the POSIX rules in the zone table) | Local time for the clock (`AT*TZ`) | IANA Time Zone Database, <https://www.iana.org/time-zones> | Public domain | None | Used |
| Calendar date algorithms (`daysFromCivil`, `civilFromDays` in `net_tz.cpp`) | Date from a UTC timestamp | Howard Hinnant, <https://howardhinnant.github.io/date_algorithms.html> | Public domain | None (credited in the source) | Used |
| Unity test framework | Unit tests | ThrowTheSwitch.org | MIT | Keep copyright notice | Used (tests only) |
| xterm.js 6.0.0 (`web/vendor/xterm.mjs`, `xterm.css`) | Web interface terminal | The xterm.js authors, <https://github.com/xtermjs/xterm.js> | MIT | Keep `web/vendor/LICENSE-xterm.txt` | Used |
| IBM Plex Mono (`web/vendor/ibm-plex-mono-*.woff2`, via Fontsource 5.3.0) | Web interface font | IBM Corp., <https://github.com/IBM/plex> | SIL OFL-1.1 | Keep `web/vendor/LICENSE-ibm-plex-mono.txt`; don't sell the font on its own | Used |
| Zimodem ZMODEM code (`proto_zmodem`), from Synchronet/SyncTERM | Client Tests file transfer | Jacques Mattheij (1994), rights transferred to Stephen Hurd (2015); via Bo Zimmerman's Zimodem | Not stated in the file; **accepted by Jon, 23 Oct** | Keep the original headers; mark changes | Planned |
| USR Sportster 33.6 command behaviour, defaults and `ATI` text (`lib/atcmd`, `lib/modemcfg`) | Authentic modem identity and command set (brief 6.6, Q55) | U.S. Robotics: Sportster technical reference (<https://support.usr.com/support/sportster/sportster-files/techref_uk.pdf>) and Sportster user guide (copy at <https://www.rigacci.org/docs/biblio/online/usr/sportster_guide.txt>); `ATI3`–`ATI7`/`ATI9` output of a Sportster 33600/Fax V2.31 posted in the fido7.ru.modem echo (<https://fido7.ru.modem.narkive.com/YzTdiljZ/sportster-33600>) | Facts and short identification strings, re-implemented (no code copied) | None beyond this note; the About screen should name U.S. Robotics as the source of the command set | Used |
| `modem1200.wav` (HQ MP3 preview, converted) | 1200-baud handshake asset | guitarguy1985, <https://freesound.org/people/guitarguy1985/sounds/78657/>; source `sound/sources/modem1200-78657.mp3` | CC0 1.0, <https://creativecommons.org/publicdomain/zero/1.0/> | None; attribution and conversion kept for provenance | Used in v0.8: `data/sounds/handshake-1200.wav`, mono 8000 Hz signed 16-bit PCM, FFmpeg resampling and metadata removal |
| `Dial up modem noises.ogg` | Generic handshake fallback for five missing speed recordings | William Termini, <https://commons.wikimedia.org/wiki/File:Dial_up_modem_noises.ogg>; original in `sound/sources/` | Public-domain dedication by copyright holder | None; source and conversion retained | Used in v0.8 with Jon's approval; not verified as any of the five selected speeds; conversion below |

Concept credits (no code used): Luis Miguel Silva (WiFi2DialUp), Matt Whitted (PodSix
pimodem), Jim Brain and FozzTexx (tcpser).

## Publication notes

### v0.8 handshake assets

The 1200-baud recording is credited in the table above. The five other speed files
use William Termini's [Dial up modem noises.ogg](https://commons.wikimedia.org/wiki/File:Dial_up_modem_noises.ogg),
released into the public domain by its copyright holder on that page. The original
is retained as `sound/sources/dial-up-modem-noises.ogg`.

`python scripts/build_handshakes.py` uses FFmpeg to rebuild all six WAVs as mono
8000 Hz signed 16-bit PCM, without metadata. The five generic fallback files are
identical copies of the 9.9-26.8 second segment, removing recorded dialing/ringing
and final silence. Their filenames select an emulator sound; they are not verified
recordings of 2400, 14400, 28800, 33600 or 56000 negotiated rates. Jon approved this
explicit fallback on 25 Oct 2026. Original sources make conversion reproducible.

Intermediate operational rates use the nearest lower of the six asset rates;
300 uses 1200. Model selection does not change this mapping. Genuine freely
licensed recordings can replace individual fallback files when available.

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
CC BY-NC (case) forbids commercial use, GPL-3.0 (reference sounds) would apply to any
firmware image that bundles them, and Apache-2.0 (Zimodem) is compatible with GPL-3.0
but not the reverse.
