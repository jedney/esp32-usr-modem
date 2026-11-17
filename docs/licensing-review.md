# Licensing review and recommendations

Reviewed 17 November 2026. This is a practical repository review, not a legal opinion or a clearance of third-party intellectual property rights.

The owner confirms that the original project work is theirs to license and that no permissions have been obtained beyond those recorded in `credits/LICENSES.md`. No new licence is adopted by this review.

## Recommendation

Use **separate licences for software, documentation, physical designs and third-party assets**, with a root licensing index explaining their scope. My preferred starting point is **Apache-2.0 for original software**, **CC BY 4.0 for original documentation and photographs**, and the **verified upstream CC BY-NC licence for case-derived designs**. Preserve all third-party licences and exclude US Robotics material from grants of rights you do not own.

This would preserve the project's non-commercial tribute as its purpose while permitting commercial reuse of the independently licensed software. It would not permit selling the CC BY-NC case or grant rights to US Robotics artwork. If preventing commercial reuse of your own software is essential, consider the non-commercial alternative below before adopting any licence.

There is currently no root licence for the original project work. `credits/LICENSE.txt` is a one-sentence case attribution, not a licence for the repository. Publishing source without a licence would not give users general permission to copy, adapt and redistribute it.

**Do not describe the current complete package as cleared for unrestricted redistribution.** The recordings, brand artwork, supplier documents/models, exact case licence version and ZMODEM provenance need resolution. A new project licence cannot supply missing permission from someone else.

## Scope and evidence

Reviewed the working-tree licensing record and notices; source provenance headers; `platformio.ini`; `dependencies.lock`; component manifests; `sdkconfig.defaults` and local `sdkconfig.s3`; the locally installed Arduino/ESP-IDF and managed-component licences; web vendors; sound and case assets; electronics references; build/preview scripts; and CI packaging.

This includes current uncommitted work, including `lib/ssh/`. That directory has no third-party provenance notice and appears to implement SSH with platform-supplied crypto; treat it as original only on the owner's authorship confirmation, rather than inferring a libssh2 dependency from algorithm names or Zimodem's general NOTICE.

The local build directory contains configuration and component metadata, but no final application ELF/link map was available there for inspection. A resolved dependency or CMake component is **not proof that its code is present in the final firmware**. Inventory findings below distinguish those cases. Before a binary release, use the final link map and a version-specific SBOM to finish the transitive dependency inventory, including bootloader and runtime code. This review is not an exhaustive per-file audit of every downloaded SDK/toolchain file.

## Component findings

| Component / location | Licence evidence and implications | Recommended treatment |
|---|---|---|
| Original firmware, browser code, scripts, tests and configuration | No general project licence found. Existing third-party-derived portions retain their own grants. | Apache-2.0 for original contributions, with explicit exclusions and retained notices. |
| Original README, guides, electronics brief, drawings and photos | Owner confirms original work is theirs. Embedded third-party images and visible brand artwork remain distinct. | CC BY 4.0, or CC BY-NC 4.0 if commercial reproduction of original documentation is unwanted. State the exclusions. |
| Case STLs and Bambu 3MF projects | `credits/LICENSE.txt` and the record identify luizpacheco's Thingiverse model as CC Attribution-NonCommercial. Neither local notice identifies a version. The Thingiverse page returned HTTP 403 during this review. | Verify the exact upstream licence/version and keep a copy or dated evidence. Attribute the author/source, link the licence and identify changes. Include derivatives embedded in 3MF projects. |
| Added mounts, inserts, braces, blanks and knobs | Some may be wholly original, others adaptations. Being in `case/` does not settle authorship or derivation. | Map each design to its provenance. Original independent parts may have their own licence; use the verified case terms for case-derived parts. A voluntary CC BY-NC 4.0 grant for original independent designs is possible. |
| `case/labels/`, `case/parts/usr-logo.stl`, replica appearance | Case-author rights and underlying US Robotics artwork/branding are separate layers. Higher resolution or AI-assisted redrawing does not itself clear underlying rights. | Preserve case attribution, but do not represent the case author's CC licence as a grant of US Robotics rights. Obtain appropriate permission or supply a debranded release. Review the replica appearance as well as the logo. |
| `web/src/usr-logo.png` | Record expressly says no redistribution licence supplied; cropped and recoloured USRobotics artwork. | Exclude from project licensing. Permission or replacement is needed before treating public redistribution as cleared. Apply this to embedded previews and screenshots too. |
| Arduino-ESP32 3.3.12, including ArduinoOTA and ESPmDNS integration | Mixed licences. `cores/esp32/Arduino.h` expressly uses LGPL-2.1-or-later; individual files include Apache-2.0 and other terms. | Retain per-file terms. For statically linked firmware, provide notices, applicable library source and a practical way to modify/relink the LGPL library under LGPL section 6. Full corresponding buildable source is a useful route; a `.bin` or `.elf` alone is not a complete compliance package. |
| ESP-IDF 6.1 and its Espressif HTTP/WebSocket, networking, OTA, drivers, storage and system code | Apache-2.0 at SDK level, with component/file exceptions. Local Wi-Fi, PHY and coexistence library licence files also contain Apache-2.0. | Keep actual shipped-component notices; do not label the entire SDK solely Apache-2.0. |
| lwIP / NAPT | lwIP BSD-3-Clause; Espressif additions Apache-2.0 per the record and source package. | Preserve copyright, conditions and disclaimers in source and binary accompanying material as applicable. |
| FreeRTOS, HTTP parser, console linenoise/argtable3, protobuf-c, SPIFFS | Local SDK contains additional MIT/BSD-family licences not individually listed in the current record. Some are optional. | Expand the framework inventory and include notices for what is redistributed; confirm final linkage rather than assuming all are used. |
| wpa_supplicant | Local COPYING directs readers to README; README supplies BSD-3-Clause terms. Relevant to Wi-Fi authentication. | Record separately and retain its binary attribution/disclaimer. |
| FatFs | Local `ff.c` contains ChaN's permissive notice; it is not simply covered by the SDK Apache licence. Configured as an SDK component does not establish that it is used by this LittleFS application. | Record if redistributed/linked, with its actual notice. |
| Picolibc / inherited Newlib material | `COPYING.picolibc` is a multi-licence, per-file inventory, alongside `COPYING.NEWLIB`. | Keep applicable full notices; avoid reducing the whole runtime to a single BSD-3-Clause attribution. |
| GCC runtime / libstdc++ | GPLv3 with GCC Runtime Library Exception for covered runtime files. | Normal eligible compilation can produce non-GPL application binaries. Preserve the relevant terms when redistributing runtime material; do not infer that compiler use alone forces this project to be GPL. |
| Mbed TLS 4 and TF-PSA-Crypto | Local packages expressly offer Apache-2.0 OR GPL-2.0-or-later. | Choose and document the Apache-2.0 option for this distribution; retain any per-file/subcomponent exceptions. |
| Mozilla-derived CA certificate bundle | Local SDK generates a bundle and local `sdkconfig.s3` enables the full bundle. Not covered in the existing record. | Determine whether included in the final image, record the source bundle and applicable terms, and preserve necessary notices. Do not infer its licence merely from the surrounding TLS library. |
| `espressif/mdns` 1.15.1 | Apache-2.0. | Keep licence/notices. Arduino's wrapper does not remove the underlying component's obligations. |
| `espressif/network_provisioning` 1.2.4 and `espressif/cjson` 1.7.19~2 | Apache-2.0 and MIT respectively. Recorded as pulled in through Arduino; whether unused code survives linkage is a separate question. | Retain applicable notices with distributed copies and record exact versions. |
| `joltwallet/littlefs` 1.23.1 / bundled littlefs | MIT wrapper; BSD-3-Clause littlefs. | Preserve both licences and copyright notices. |
| `espressif/tinyusb` 0.21.0~6 and local `tusb_config.h` | MIT for relevant stack/configuration; the local adapted configuration retains its MIT grant and change provenance. | Retain the notices; check platform files actually included rather than relicensing every bundled hardware port. |
| CDC descriptor in `lib/hw/src/usb_port.cpp` | Explicit Apache-2.0 provenance from Arduino-ESP32 and marked changes. | Keep the attribution/change notice and include the Apache licence in release material. |
| Zimodem Telnet, PPP/SLIP and X/YMODEM adaptations | Apache-2.0 provenance, retained headers and notices in `credits/zimodem/`. | Compatible with the proposed Apache software licence; retain attribution and modification notices. |
| ZMODEM in `lib/clienttest/src/transfer.cpp` | Local notice and pinned Zimodem source identify Mattheij's rights transfer to Stephen Hurd, but do not themselves state a public reuse grant. Current Synchronet `zmodem.c/.h` have explicit BSD-2-Clause-style grants. | Trace the portions used to a version covered by that grant, document the chain and restore applicable notices; or obtain confirmation/rebase on a clearly licensed version. Do not simply relabel the existing adaptation Apache-2.0. |
| IANA time-zone rules / Howard Hinnant calendar formulas | Upstream public-domain statements verified for the relevant rules/formulas. | Retain provenance as good practice. This is not a blanket public-domain assertion about every IANA file or Hinnant library. |
| Seven source WAVs and copies under `data/sounds/` | Gough Lui attribution recorded. The linked article gives no express audio reuse licence; its CC reference concerns someone else's illustration. | Obtain redistribution/adaptation permission or replace with independently created/clearly licensed recordings. Attribution alone is insufficient. Cover filesystem images and transfer/download features, not just source archives. |
| xterm.js 6.0.0 (`web/vendor/`) | MIT; vendor header also identifies Microsoft-derived portions. Local licence file is present. | Preserve embedded notices and ship the applicable full MIT notices, including those from the exact upstream package. |
| IBM Plex Mono / Fontsource package | SIL OFL-1.1 for font files. | Keep fonts under OFL, include copyright/licence, respect any Reserved Font Names if modified, and do not sell the font by itself. The OFL does not require application code or documents using the font to adopt OFL. |
| Supplier PDFs, images, pinmaps and STL board models in `electronics/` | Existing record says manufacturers' copyright/reference only; no general redistribution permission recorded. Individual supplier models could have separate grants that need checking. | Prefer links to official references unless an explicit redistribution licence is verified. Distinguish your original brief from supplier files. Private reference use does not establish public redistribution rights. |
| Unity, pppd, lrzsz | Unity MIT; pppd mixed upstream notices; lrzsz GPL. The latter two are external test peers, not linked firmware libraries. | Their use as separate test programs does not alone impose GPL on the firmware. If distributing tool binaries, containers or SDK images, satisfy their separate distribution obligations. |
| Python/Node/PlatformIO, pytest, pytest-timeout, pyserial, CI actions and other developer tools | Build/test tooling is distinct from linked application code. Test requirements explicitly include pytest/pytest-timeout and pyserial; these are missing from the current component record. | Record versions and upstream terms if distributing a development environment. Do not treat downloaded CI tools as bundled firmware. Preserve runtime exceptions and assess generated/copied content separately. |
| Concept credits: WiFi2DialUp, pimodem, tcpser, caiot5 RetroExternalModem | Existing record states no code copied. | Keep courtesy credit; another project's GPL licence does not apply solely because it inspired this project. Reassess if implementation/design content was actually copied. |

### Additional resolved dependencies

`dependencies.lock` contains more components than the main licensing record. Local top-level licences show:

| Resolved component | Licence finding | Follow-up |
|---|---|---|
| `chmorgan/esp-libhelix-mp3` 1.0.3 | Apache-2.0 wrapper, but bundled decoder `libhelix-mp3/LICENSE.txt` specifies RealNetworks RPSL or separately licensed RCSL. | Do not call the whole dependency Apache-2.0. Determine whether any decoder code ships; remove from the distributed set if unused, or review and fulfil its actual terms. |
| `espressif/esp-zboss-lib` 1.6.4 | Custom licence with a prohibition on reverse engineering/modifying/disassembling supplied binaries. | It is not ordinary BSD-3-Clause. Check whether shipped. Do not assume compatibility with GPL or blanket LGPL reverse-engineering permissions. Local Arduino Zigbee is disabled, but that is not final link-map evidence. |
| `espressif/esp-zigbee-lib` 1.6.8 | Apache-2.0 at top level; ZBOSS remains separate. | Inventory any included binary dependencies. |
| `espressif/esp-modbus` 2.1.4 | Apache-2.0 wrapper and BSD-3-Clause portions in `modbus/`. | Preserve both sets if included. |
| `espressif/esp-dsp` 1.8.2, `esp_modem` 2.1.0, `dm9051` 1.1.1, `w5500` 1.0.1 | Apache-2.0 at the inspected top level. | Confirm use and nested exceptions before release. |
| `espressif/ksz8851snl` 1.2.0 | MIT at the inspected top level. | Preserve the actual MIT notice if included. |

These findings concern resolved/downloaded packages. They do not assert that MP3, Modbus, Ethernet or Zigbee functionality is present in the final modem image.

## Corrections and distribution gaps

1. **CC BY-NC is not CC BY-NC-SA.** The case row says modified versions must keep the same terms. BY-NC alone has no ShareAlike condition. The original material remains subject to its NC grant, but an adapter's own contributions are a separate question. Verify the exact upstream version before correcting the record or choosing SPDX identifiers. See the [CC licence chooser](https://creativecommons.org/chooser/) and [BY-NC 4.0 legal text](https://creativecommons.org/licenses/by-nc/4.0/legalcode.en).
2. **Credits are not the same as licence delivery.** `scripts/generate_credits.py` generates project names/URLs, not full licences. `scripts/build_web.py` does not include either `web/vendor/LICENSE-*.txt` in its file mapping. `scripts/make_preview.py` embeds fonts without explicitly adding their licence text. Verify embedded font metadata, and include readily accessible notices in the standalone previews and filesystem/release package rather than relying on an external repository link.
3. **CI artifacts need accompanying licensing/source material.** The firmware artifact list currently contains binaries, an ELF and a manifest, without the complete third-party notice package or LGPL source/relink material. Bundle or otherwise supply the materials required by each applicable licence. Pinning a dependency in a lockfile does not itself fulfil a source-delivery obligation.
4. **Mixed files need accurate notices.** The Apache header on `transfer.cpp` does not clear the separately derived ZMODEM portions. Likewise the general libssh2 mention in the copied Zimodem NOTICE does not establish that this project uses libssh2; preserve required applicable notices and distinguish unrelated upstream credits.

## Licence choices for your original work

| Choice | Benefit | Trade-off and compatibility |
|---|---|---|
| **Apache-2.0 software; separate asset licences (preferred)** | Familiar permissive software terms, explicit contributor patent grant and trademark clause; fits the many Apache-derived portions. | Allows commercial reuse of your software. No requirement to publish downstream changes. Does not override LGPL, NC assets or US Robotics rights. Use CC BY 4.0 or CC BY-NC 4.0 for original documentation according to your preference. |
| **MIT software; separate asset licences** | Very short and widely understood. | Also allows commercial reuse; no Apache-style express patent grant. Existing Apache portions still require their notices/licence, so it does not make the repository MIT-only. |
| **GPL-3.0-or-later software; separate asset licences** | Recipients distributing a covered combined/modified program generally must provide corresponding source under GPL. Apache-2.0 code can be incorporated into GPLv3 works with its notices retained. | Still permits commercial sale/use. More downstream obligations; check every actually linked custom component first, especially ZBOSS and Helix. A case model or separately distributed documentation is not automatically relicensed GPL. Avoid GPL-2.0-only for an Apache-2.0-based combination. |
| **PolyForm Noncommercial 1.0.0 for your original software; CC BY-NC 4.0 for your original content** | A software-specific non-commercial alternative, expressly allowing hobby/educational uses. | Source-available rather than OSI open source. Its definition also permits uses by specified nonprofit/public institutions, so it is not simply a ban on all organisational use. It cannot restrict third-party code's independent rights. Before adopting it for an Arduino-linked binary, review LGPL section 6 and ensure the combined distribution permits required modification for the customer's own use and reverse engineering for debugging library modifications, plus relinking/source access. This is not a cleared drop-in recommendation for the present firmware. |

Sources: [Apache-2.0](https://www.apache.org/licenses/LICENSE-2.0), [Apache/GPL compatibility](https://apache.org/licenses/GPL-compatibility.html), [PolyForm Noncommercial](https://polyformproject.org/licenses/noncommercial/1.0.0), [OSI definition](https://opensource.org/osd), and [LGPL distribution guidance](https://www.gnu.org/licenses/gpl-faq.html#LGPLStaticVsDynamic). The [LGPL-2.1 text in GCC's upstream repository](https://raw.githubusercontent.com/gcc-mirror/gcc/master/COPYING.LIB) provides the section 6 conditions.

Do not add a no-commercial-use sentence to Apache, MIT or GPL and continue calling the result that standard licence. CC also [recommends software-specific licences for code](https://creativecommons.org/faq/#can-i-apply-a-creative-commons-license-to-software). The case's NC restriction does not automatically extend to independently authored firmware, nor can a permissive software licence cancel the case restriction.

## Respecting US Robotics' rights

A licence for your contributions grants only rights you control. Apache section 6 and CC BY-NC section 2(b) do not grant trademark rights; those provisions also cannot supply permission to reproduce someone else's copyrighted art. USR's own [legal notice](https://www.usr.com/legal-notice/) reserves its website copyright and identifies software/documentation rights as belonging to USR or suppliers. It is not a general public licence for project branding.

Keep the README's respectful acknowledgement and non-affiliation statement, and repeat a concise disclaimer in the web interface and distributed documentation. Use an independent project identity so a replica is clearly presented as a modern hobby build. Do not assert a trademark registration or current rights holder beyond what has been verified.

For the public package, seek permission covering the actual uses of the logo, modified logo, printed labels and printable logo model; address the replica enclosure/appearance as appropriate. Otherwise offer original, debranded assets and keep references to US Robotics and Sportster descriptive. Screenshots and photographs containing those elements need consideration as well. A disclaimer, attribution, non-commercial purpose or the age of the products is not by itself a grant of permission or a guarantee against infringement. Any reliance on exceptions, nominative use or design/trade-dress defences needs jurisdiction-specific advice.

Suggested scope statement, to accompany whichever project licences you choose:

> The project licences apply only to original material whose rights the project contributors control. Third-party components retain their own licences and notices. US Robotics and Sportster names, marks, logos, original artwork and other third-party material are excluded from any grant of rights the contributors do not own. No project licence grants rights in US Robotics trademarks, copyrighted material, designs or other intellectual property. This independent, unsanctioned, non-commercial hobby tribute is not affiliated with, authorised, sponsored or endorsed by US Robotics.

This statement records scope and intent; it does not cure an unauthorised inclusion.

## Practical path to a release

1. Choose whether the software's non-commercial character is a purpose or an enforceable restriction; decide separately whether downstream software changes must stay available.
2. Verify the case licence/version and provenance of each derivative. Resolve ZMODEM against the [pinned Zimodem source](https://raw.githubusercontent.com/bozimmerman/Zimodem/a534e5129d88b8b09aa01fe17498d5cd5368569e/zimodem/proto_zmodem.ino) and the explicit grants in [current Synchronet source](https://raw.githubusercontent.com/SynchronetBBS/sbbs/master/src/sbbs3/zmodem.c). Current terms are a lead, not automatic proof for the older adaptation.
3. Obtain permission for the [Gough Lui recordings](https://goughlui.com/2016/05/03/project-the-definitive-collection-of-v-90v-92-modem-sounds/), or replace them. Resolve brand assets and supplier reference redistribution, or omit/replace those files in a public package.
4. Add a root licensing index, complete licence texts and a path-by-path scope map. Preserve original headers and add original-contribution notices where appropriate. Include source formats for designs when available.
5. Expand the component record from the exact release build/link map, including resolved packages, runtime and bootloader. Supply notices with source archives, binary releases, filesystem images and standalone previews; provide LGPL-compliant source/relink materials.

No licences, existing credits, assets or build behaviour were changed as part of this review.
