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
| Unity test framework | Unit tests | ThrowTheSwitch.org | MIT | Keep copyright notice | Used (tests only) |
| xterm.js 6.0.0 (`web/vendor/xterm.mjs`, `xterm.css`) | Web interface terminal | The xterm.js authors, <https://github.com/xtermjs/xterm.js> | MIT | Keep `web/vendor/LICENSE-xterm.txt` | Used |
| IBM Plex Mono (`web/vendor/ibm-plex-mono-*.woff2`, via Fontsource 5.3.0) | Web interface font | IBM Corp., <https://github.com/IBM/plex> | SIL OFL-1.1 | Keep `web/vendor/LICENSE-ibm-plex-mono.txt`; don't sell the font on its own | Used |
| Zimodem ZMODEM code (`proto_zmodem`), from Synchronet/SyncTERM | Client Tests file transfer | Jacques Mattheij (1994), rights transferred to Stephen Hurd (2015); via Bo Zimmerman's Zimodem | Not stated in the file; **accepted by Jon, 23 Oct** | Keep the original headers; mark changes | Planned |
| Handshake recordings (1200, 2400, 14.4k, 28.8k, 33.6k, 56k) | Connect sounds | To be sourced | Must be freely licensed | Record source and licence here before adding | Planned |

Concept credits (no code used): Luis Miguel Silva (WiFi2DialUp), Matt Whitted (PodSix
pimodem), Jim Brain and FozzTexx (tcpser).

## Publication notes

If the repository is ever made public, the mixed licences need a decision first:
CC BY-NC (case) forbids commercial use, GPL-3.0 (reference sounds) would apply to any
firmware image that bundles them, and Apache-2.0 (Zimodem) is compatible with GPL-3.0
but not the reverse.
