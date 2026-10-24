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
| Zimodem (PPP/SLIP and Telnet code, to be ported) | Firmware modules | Bo Zimmerman, <https://github.com/bozimmerman/Zimodem> | Apache-2.0 | Keep LICENSE and NOTICE; mark every ported or modified file as changed | Planned |
| Arduino-ESP32 core 2.0.x | Firmware framework | Espressif Systems | LGPL-2.1 (core), Apache-2.0 (ESP-IDF) | Allow relinking if distributed as a binary | Used |
| ArduinoOTA and bundled `tools/espota.py` (Arduino-ESP32 core 2.0.x) | Password-authenticated firmware updates; existing uploader called through a local secret-safe wrapper | Espressif Systems and the contributors named in `espota.py` | Distributed with Arduino-ESP32 (above); original files unchanged | Keep upstream notices and framework licensing obligations | Used in v0.7 |
| Mbed TLS (bundled with ESP-IDF) | MD5 keys for OTA/HTTP Digest and CTR-DRBG seeded from the ESP hardware entropy source | Mbed TLS contributors | Apache-2.0 | Keep upstream licence and notices; bundled library unchanged | Used in v0.7 |
| Time-zone rules (`lib/netcfg/src/net_tz.cpp`, the POSIX rules in the zone table) | Local time for the clock (`AT*TZ`) | IANA Time Zone Database, <https://www.iana.org/time-zones> | Public domain | None | Used |
| Calendar date algorithms (`daysFromCivil`, `civilFromDays` in `net_tz.cpp`) | Date from a UTC timestamp | Howard Hinnant, <https://howardhinnant.github.io/date_algorithms.html> | Public domain | None (credited in the source) | Used |
| Unity test framework | Unit tests | ThrowTheSwitch.org | MIT | Keep copyright notice | Used (tests only) |
| xterm.js 6.0.0 (`web/vendor/xterm.mjs`, `xterm.css`) | Web interface terminal | The xterm.js authors, <https://github.com/xtermjs/xterm.js> | MIT | Keep `web/vendor/LICENSE-xterm.txt` | Used |
| IBM Plex Mono (`web/vendor/ibm-plex-mono-*.woff2`, via Fontsource 5.3.0) | Web interface font | IBM Corp., <https://github.com/IBM/plex> | SIL OFL-1.1 | Keep `web/vendor/LICENSE-ibm-plex-mono.txt`; don't sell the font on its own | Used |
| Zimodem ZMODEM code (`proto_zmodem`), from Synchronet/SyncTERM | Client Tests file transfer | Jacques Mattheij (1994), rights transferred to Stephen Hurd (2015); via Bo Zimmerman's Zimodem | Not stated in the file; **accepted by Jon, 23 Oct** | Keep the original headers; mark changes | Planned |
| USR Sportster 33.6 command behaviour, defaults and `ATI` text (`lib/atcmd`, `lib/modemcfg`) | Authentic modem identity and command set (brief 6.6, Q55) | U.S. Robotics: Sportster technical reference (<https://support.usr.com/support/sportster/sportster-files/techref_uk.pdf>) and Sportster user guide (copy at <https://www.rigacci.org/docs/biblio/online/usr/sportster_guide.txt>); `ATI3`–`ATI7`/`ATI9` output of a Sportster 33600/Fax V2.31 posted in the fido7.ru.modem echo (<https://fido7.ru.modem.narkive.com/YzTdiljZ/sportster-33600>) | Facts and short identification strings, re-implemented (no code copied) | None beyond this note; the About screen should name U.S. Robotics as the source of the command set | Used |
| Handshake recordings (1200, 2400, 14.4k, 28.8k, 33.6k, 56k) | Connect sounds | To be sourced | Must be freely licensed | Record source and licence here before adding | Planned |

Concept credits (no code used): Luis Miguel Silva (WiFi2DialUp), Matt Whitted (PodSix
pimodem), Jim Brain and FozzTexx (tcpser).

## Publication notes

### v0.8 identity and extended speed references

The Sportster 14400 identity is modeled from the USR product name and inquiry
semantics in the [USR Sportster High Speed Modems Technical Reference Guide](https://www.rigacci.org/docs/biblio/online/usr/sportster_guide.txt)
(manufacturer-authored guide, mirrored). The earlier 33600 capture remains the
source for settings-screen layout, rather than a source for 14400 ROM details.
No verified 14400 ROM capture was found: product code `1441` and checksum `0000`
are emulator placeholders. `ATI3` has no ROM revision; `ATI7` marks physical
ROM/DSP information as emulated or unavailable; `ATI9` returns the model name
without the former 33.6 Plug and Play identifier. No new third-party code is copied.

Speed indexes `&N1`-`&N38` and `&U1`-`&U38`, and numeric CONNECT results, follow
the [USR V.90 Technical Quick Reference](https://support.usr.com/support/s-modem/s-modem-docs/v90tecrf.pdf),
printed pages 10-11 and 15-18. Existing indexes 1-16 are preserved. The later
indexes are not in increasing speed order; 38 selects 56000. These are factual
command mappings reimplemented in this project; no code or manual pages are bundled.

If the repository is ever made public, the mixed licences need a decision first:
CC BY-NC (case) forbids commercial use, GPL-3.0 (reference sounds) would apply to any
firmware image that bundles them, and Apache-2.0 (Zimodem) is compatible with GPL-3.0
but not the reverse.
