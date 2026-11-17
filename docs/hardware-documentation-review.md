# Hardware documentation differences for review

Reviewed on 16 November 2026 against the [final electronics brief](../electronics/USR%20Modem%20Electronics%20Final%20Brief.md), the [firmware specification](project-brief.md), the default pin configuration and the relevant implementation. This file also records related discrepancies found in source comments, the README and supporting material.

This file records the remaining differences for review. The proposed corrections listed below have not been applied.

## Which source takes precedence

- Your latest instructions clarify the supported DB9 and DB25 build options and their mounting arrangement.
- Code takes precedence for implemented behaviour, pin assignments and polarity.
- The final electronics brief takes precedence for fitted components and physical wiring where executable code does not contradict it.
- Hardware descriptions in source comments are identified explicitly below. A comment saying a resistor is fitted is a source-text conflict, but does not make that resistor an executable requirement. The review records both statements rather than treating a comment as proof of the physical wiring.

**No conflict was found between the brief's GPIO assignments or signal polarity and the default executable implementation.** The main differences are physical components described in older documentation, plus a few outdated descriptions of the build.

## 1. Volume-pot wiper capacitor

**Firmware specification:** [section 2.3](project-brief.md#23-audio) specifies a WH148 10 kΩ linear pot with **100 nF at the wiper**.

**Electronics brief:** [section 8](../electronics/USR%20Modem%20Electronics%20Final%20Brief.md#8-leds-and-volume-pot) explicitly says **there is no wiper capacitor**. The wiper connects to GPIO10; the outer terminals connect to ground and 3.3 V.

**Code:** `potBegin()` and `potRaw()` in [hw.cpp](../lib/hw/src/hw.cpp) configure the ADC and average 16 readings. Nothing in those functions requires or verifies an external 100 nF capacitor. The default GPIO10 assignment agrees with the brief.

**Resolution:** remove the 100 nF capacitor from the firmware specification's hardware description. The current README already follows the final brief here. No firmware change is indicated.

## 2. DSR connection: direct versus a 1 kΩ resistor

**Firmware specification:** [section 2.2](project-brief.md#22-rs232-dce-db25-female-in-the-final-build-db9-on-the-bench) says DSR is hard-wired asserted, but does not specify a series resistor.

**Electronics brief:** [section 3](../electronics/USR%20Modem%20Electronics%20Final%20Brief.md#3-wiring-netlist) and the [U2 pinout in section 5](../electronics/USR%20Modem%20Electronics%20Final%20Brief.md#u2-pinout-adafruit-5987) specify **U2 V+ directly to DB9 terminal 6**, which reaches DB25 pin 6 through the adapter. No 1 kΩ resistor is listed.

**Conflict in a code comment:** [pins.h](../lib/hw/src/pins.h#L52) says: `DSR (DB9 6) is hard-wired on from U2 V+ through 1 k: no pin.`

**Executable behaviour:** there is no physical DSR GPIO assignment. For the USB modem port, [SerialStateSender::poll()](../lib/hostport/src/serial_state.cpp) explicitly sets `want.dsr = true`. Both implementations agree with the brief that DSR is always asserted. Neither can establish whether a physical series resistor is fitted.

**Resolution:** the direct connection in the electronics brief is the physical build reference; the `through 1 k` comment should be corrected. This is a source-comment discrepancy, not a conflict in executable behaviour. No change to DSR handling is indicated. The current README already describes the direct connection.

## 3. DB9 is a supported finished build, not only a bench connector

**Older wording:** the [firmware specification](project-brief.md#22-rs232-dce-db25-female-in-the-final-build-db9-on-the-bench), [pin-map decision](decisions-log.md), [roadmap](ROADMAP.md) and [CLAUDE.md hardware summary](../CLAUDE.md#hardware-summary) describe DB9 as the bench version and DB25 as the final build. The electronics brief itself describes a DB25 configuration made from the DB9 terminal board and an adapter.

**Your clarification:** builders can choose either rear socket. DB25 matches the original modem; DB9 is convenient for modern USB-to-RS232 cables. Both use the same wired DB9 terminal board. DB25 uses the alternate mounting holes, setting the terminal board farther back inside the case, and adds the DB9-to-DB25 adapter. There are **no wiring or firmware differences**. All current build photos show DB9.

**Code:** the implementation uses the same GPIOs and signals for both connectors; there is no DB9/DB25 firmware selection. The printed loopback prompts in [hwconsole.cpp](../src/hwconsole.cpp) nevertheless call the DB9 pin pairs `bench DB9`.

**Resolution:** retain both socket options in build documentation and describe the mounting/adapter difference. The README has already been updated. The older specification, decision wording, CLAUDE.md and diagnostic prompt text still describe DB9 too narrowly. The roadmap's DB25 check can remain a pending check of that particular variant; it should not imply that a completed DB9 unit is only a prototype. A photo does not establish that the DB25 variant has been tested.

## 4. Diagnostic screenshot uses an older TR LED GPIO

**Screenshot:** [Example HW Diagnostic.png](../screenshots/Example%20HW%20Diagnostic.png) displays `TR=GPIO9`.

**Electronics brief and firmware specification:** both assign **TR to GPIO15**.

**Code:** [the default pin configuration](../.env.example) sets `USR_MODEM_PIN_LED_TR=15`; [pins.h](../lib/hw/src/pins.h) uses it in `kLeds`, and [the pin-map unit test](../test/native/test_pins/test_main.cpp) locks the default LED sequence to `4, 5, 6, 7, 15, 16, 17`.

**Resolution:** code takes precedence and agrees with the electronics brief: **TR is GPIO15** in the standard build. Replace the screenshot when a current capture is available, or keep it clearly labelled as an older bench capture. The README already includes that explanation. Do not change the firmware pin map to match the screenshot.

## Differences in detail that are not contradictions

The electronics brief supplies several construction details that the older firmware specification either omits or describes only generally. These are useful additions when reconciling the documentation, but they do not indicate firmware mismatches:

| Topic | Detail established by the electronics brief |
|---|---|
| Rear power input | Adafruit HUSB238 board, 5 V selected, front SPST switch in the positive supply path, common ground |
| Amplifier supply routing | Amplifier Vin taken from the power-board end of the switched 5 V rail |
| Rear USB data | HUSB238 D+ to GPIO20 and D− to GPIO19; shares the ESP32's native USB connection |
| RS232 module allocation | Exact U1/U2 LV/HV connections, the unused U2 R2 channel, and unconnected charge-pump pins |
| Ground versus charge-pump output | RS232 ground is GND; V− is a generated negative supply and is left unconnected |
| Pot orientation | Viewed from the shaft side with pins down: pin 1 GND, pin 2 wiper/GPIO10, pin 3 3.3 V; clockwise increases volume |
| Assembly style | Direct soldered wiring, with screw terminals at the DB9 terminal board |
| Specific purchased parts | Module product numbers, connector/adapter models, switch, LED and pot identifiers |

The main GPIO map, RS232 directions and inversion, four 10 kΩ driver-input pull-ups, native USB pins, amplifier I2S pins, unconnected GAIN pin, 8 Ω/2 W speaker and absence of a firmware volume cap agree across the relevant specification, brief and implementation.

The firmware also imposes pin restrictions more broadly than the brief's short rules list: [pin_rules.py](../scripts/pin_rules.py) rejects GPIO22–25 as absent, GPIO26–37 as reserved for flash/PSRAM, and GPIO19/20 for configurable non-USB signals. It restricts the pot to GPIO1–10. These implemented checks take precedence; the brief's shorter list should not be read as permission to use other reserved pins.

## Proposed reconciliation after review

| File or material | Proposed change |
|---|---|
| `docs/project-brief.md` | Remove the 100 nF wiper capacitor description; present DB9 and DB25 as supported builds |
| `lib/hw/src/pins.h` | Correct the physical-wiring comment about the DSR 1 kΩ resistor; retain executable definitions |
| `docs/decisions-log.md` and `CLAUDE.md` | Replace wording that reserves DB9 for bench use |
| `src/hwconsole.cpp` | Describe the loopback pin pairs as DB25 and DB9, without the `bench` qualifier |
| `docs/ROADMAP.md` | Keep any uncompleted DB25-specific test clearly separate from the status of a completed DB9 build |
| Diagnostic screenshot | Replace with a current capture when available; retain the existing correction note until then |

These are proposed documentation and descriptive-text corrections. This review does not identify a required change to executable firmware behaviour.
