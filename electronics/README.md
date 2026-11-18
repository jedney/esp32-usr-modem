# USR Modem Emulator: Electronics (final build)

This describes the finished, working electronics of a Wi-Fi modem built into a US Robotics Sportster replica.

## 1. Summary

- **Controller:** ESP32-S3 board (A1).
- **RS232:** two RS232 Pal modules (U1, U2) form a full DCE port with all eight signals. The port is a DB9 female on a screw-terminal board (J1), made into a DB25 female by a straight-through adapter (J2).
- **Power:** a USB-C PD board (J3) set to 5 V feeds a power switch (SW1) and the 5 V rail. J3's USB data lines also carry the ESP32's native USB ("Serial USB") to the rear socket.
- **Audio:** an I2S amplifier (U3) drives a 2 W speaker (LS1).
- **Front panel:** seven LEDs (D1–D7) and a volume pot (VR1).
- **Construction:** all wiring is soldered direct. The only connectors are J1's screw terminals.

## 2. Parts

| Ref | Part | Qty | Role |
|---|---|---|---|
| A1 | ESP32-S3-WROOM-1 N16R8 on a DevKitC-1-style board (ePartners ST8002) | 1 | Controller: 16 MB flash, 8 MB PSRAM, Console USB and Serial USB ports |
| U1, U2 | Adafruit 5987 RS232 Pal (MAX3232E) | 2 | RS232 drivers and receivers; charge-pump capacitors on board |
| J1 | Tech Express DB9-F-TERMINAL-NZ | 1 | DB9 female DCE port; screw terminal n = DB9 pin n |
| J2 | Assmann AB468 DB9M–DB25F, straight-through | 1 | Turns J1 into a DB25 female port |
| J3 | Adafruit 5807 HUSB238 USB-C power board, 5 V jumper | 1 | 5 V power input; D+/D− carry the Serial USB |
| SW1 | SW0133 SPST rocker | 1 | Power switch |
| U3 | Adafruit 3006 MAX98357A I2S amplifier | 1 | Speaker amp, GAIN unconnected (9 dB) |
| LS1 | Same Sky CLS0361-L152, 8 Ω, 2 W, Ø36 mm | 1 | Speaker |
| VR1 | WH148 B10K, 10 kΩ linear | 1 | Volume pot |
| D1–D7 | Jaycar ZD0230 red 5 × 2 mm LED | 7 | Front panel AA, CD, RD, SD, TR, CS, ARQ |
| R1–R4 | 10 kΩ | 4 | Pull-ups to 3.3 V on the four RS232 driver inputs |

## 3. Wiring (netlist)

All grounds are common: J3 terminal −, A1 GND (L22), U1/U2 GND, U3 GND, the LED cathodes, the pot's GND end and DB9 pin 5.

| Net | Connects |
|---|---|
| +5 V (unswitched) | J3 terminal + → SW1 |
| 5 V rail | SW1 → A1 5V (L21); U3 Vin (taken at the power-board end of the rail) |
| 3.3 V | A1 3V3 (L1) → U1 VIN, U2 VIN, R1–R4; A1 3V3 (L2) → VR1 clockwise end |
| Serial USB D+ | J3 D+ → A1 GPIO20 (R19) |
| Serial USB D− | J3 D− → A1 GPIO19 (R20) |
| LEDs | A1 GPIO 4, 5, 6, 7, 15, 16, 17 (L4–L10) → D1–D7 anodes (AA, CD, RD, SD, TR, CS, ARQ); cathodes → GND; no resistors |
| Pot | VR1 wiper → A1 GPIO10 (L16); VR1 ends → 3.3 V (L2) and GND |
| I2S | A1 GPIO11 (L17) → U3 LRC; GPIO12 (L18) → U3 BCLK; GPIO13 (L19) → U3 DIN; GPIO14 (L20) → U3 SD |
| Speaker | U3 + / − → LS1 (bridged output; neither wire to GND) |
| RS232 | See section 5 |
| DSR | U2 V+ → J1 terminal 6, direct |
| DB25 | J1 → J2 (AB468), straight-through |

## 4. ESP32-S3 pinout (A1)

Top view, antenna end up. Header pins are counted from the antenna end: L = left header, R = right header. Pins not listed are not connected.

**Left header**

| Pin | Label | Connection |
|---|---|---|
| L1 | 3V3 | 3.3 V out → U1, U2 VIN; R1–R4 |
| L2 | 3V3 | 3.3 V out → VR1 clockwise end |
| L4 | 4 | LED AA |
| L5 | 5 | LED CD |
| L6 | 6 | LED RD |
| L7 | 7 | LED SD |
| L8 | 15 | LED TR |
| L9 | 16 | LED CS |
| L10 | 17 | LED ARQ |
| L16 | 10 | VR1 wiper (ADC1) |
| L17 | 11 | U3 LRC |
| L18 | 12 | U3 BCLK |
| L19 | 13 | U3 DIN |
| L20 | 14 | U3 SD (HIGH = on, LOW = shutdown) |
| L21 | 5V | 5 V in from SW1 |
| L22 | GND | Ground |

**Right header**

| Pin | Label | Connection |
|---|---|---|
| R2, R3 | 43, 44 | Wired on the board to the Console USB chip |
| R4 | 1 | RXD → U1 T1 in |
| R5 | 2 | TXD ← U1 R1 out |
| R6 | 42 | CTS → U2 T1 in |
| R7 | 41 | RTS ← U2 R1 out |
| R8 | 40 | DTR ← U1 R2 out |
| R9 | 39 | DCD → U1 T2 in |
| R10 | 38 | RI → U2 T2 in |
| R19 | 20 | Serial USB D+ ↔ J3 D+ |
| R20 | 19 | Serial USB D− ↔ J3 D− |

**USB ports**, looking at the USB end:

| Port | Position | Function |
|---|---|---|
| Serial USB | Left-hand USB-C | Native USB on GPIO19/20; also wired to J3 D+/D−, so the rear power socket carries it |
| Console USB | Right-hand USB-C | USB-serial chip on GPIO43/44; flashing and the maintenance console |

## 5. RS232 port

DCE (modem side), all eight signals, so standard straight cables work. The Pals invert both ways, so an asserted RS232 signal is LOW at the GPIO.

| DB9 | DB25 | Signal | Direction | Pal pin | A1 GPIO |
|---|---|---|---|---|---|
| 1 | 8 | DCD | Out | U1 T2 | 39 |
| 2 | 3 | RXD | Out | U1 T1 | 1 |
| 3 | 2 | TXD | In | U1 R1 | 2 |
| 4 | 20 | DTR | In | U1 R2 | 40 |
| 5 | 7 | GND | — | U1/U2 GND | GND |
| 6 | 6 | DSR | Out, always asserted | U2 V+ | — |
| 7 | 4 | RTS | In | U2 R1 | 41 |
| 8 | 5 | CTS | Out | U2 T1 | 42 |
| 9 | 22 | RI | Out | U2 T2 | 38 |

### U1 pinout (Adafruit 5987)

| Side | Pin | Connection |
|---|---|---|
| LV | T1 | ← GPIO1 (R4), RXD; 10 kΩ pull-up to 3.3 V |
| LV | R1 | → GPIO2 (R5), TXD |
| LV | T2 | ← GPIO39 (R9), DCD; 10 kΩ pull-up to 3.3 V |
| LV | R2 | → GPIO40 (R8), DTR |
| HV | T1 | → DB9 2 (RXD) |
| HV | R1 | ← DB9 3 (TXD) |
| HV | T2 | → DB9 1 (DCD) |
| HV | R2 | ← DB9 4 (DTR) |
| Power | VIN | 3.3 V (A1 L1) |
| Power | GND | Ground (DB9 5) |
| Power | V+ | Not connected |
| Power | V− | Not connected |

### U2 pinout (Adafruit 5987)

| Side | Pin | Connection |
|---|---|---|
| LV | T1 | ← GPIO42 (R6), CTS; 10 kΩ pull-up to 3.3 V |
| LV | R1 | → GPIO41 (R7), RTS |
| LV | T2 | ← GPIO38 (R10), RI; 10 kΩ pull-up to 3.3 V |
| LV | R2 | Not connected |
| HV | T1 | → DB9 8 (CTS) |
| HV | R1 | ← DB9 7 (RTS) |
| HV | T2 | → DB9 9 (RI) |
| HV | R2 | Not connected |
| Power | VIN | 3.3 V (A1 L1) |
| Power | GND | Ground (DB9 5) |
| Power | V+ | → DB9 6 (DSR), direct |
| Power | V− | Not connected |

## 6. Power and USB

- **J3** asks the charger for 5 V, so any USB-C charger works. Peak draw is about 1 A.
- **Power path:** J3 terminal + → SW1 → 5 V rail → A1 5V (L21) and U3 Vin. A1's regulator supplies 3.3 V to U1, U2 and VR1.
- **Powered from a computer:** a USB-C port or a powered hub gives 1.5–3 A. A USB-A port gives only 0.5–0.9 A, which can brown out at full volume.

### J3 pinout (Adafruit 5807 HUSB238)

| Pin | Connection |
|---|---|
| USB-C receptacle | Rear socket: power in and Serial USB |
| Header D− | A1 GPIO19 (R20) |
| Header D+ | A1 GPIO20 (R19) |
| Header GND | Same net as terminal − |
| Header SCL | Not connected |
| Header SDA | Not connected |
| Header V+ | Same net as terminal + |
| Terminal + | +5 V → SW1 |
| Terminal − | Ground |
| Jumpers | 5V closed only |

## 7. Audio

- U3 takes I2S on GPIO 11 (LRC), 12 (BCLK) and 13 (DIN). GPIO14 drives SD: LOW = shutdown, HIGH = on (left channel).
- GAIN unconnected = 9 dB. At 5 V the amp gives about 1.4–1.8 W into 8 Ω, within LS1's 2 W rating (3 W max).
- The output is bridged: neither speaker wire goes to ground.

### U3 pinout (Adafruit 3006 MAX98357A)

| Pin | Connection |
|---|---|
| LRC | GPIO11 (L17) |
| BCLK | GPIO12 (L18) |
| DIN | GPIO13 (L19) |
| GAIN | Not connected (9 dB) |
| SD | GPIO14 (L20) |
| GND | Ground |
| Vin | 5 V rail (power-board end) |
| Speaker + / − | LS1 |

## 8. LEDs and volume pot

- **LEDs:** each runs GPIO → anode, cathode → ground, with no series resistor; HIGH = lit. The ESP32's output driver sets the current.

| LED | AA | CD | RD | SD | TR | CS | ARQ |
|---|---|---|---|---|---|---|---|
| GPIO | 4 | 5 | 6 | 7 | 15 | 16 | 17 |
| Header | L4 | L5 | L6 | L7 | L8 | L9 | L10 |

### VR1 pinout (WH148), seen from the shaft side with the pins down

| Pin | Role | Connection |
|---|---|---|
| 1 | End | Ground |
| 2 | Wiper | GPIO10 (ADC1, L16) |
| 3 | Clockwise end | 3.3 V (L2) |

Turning clockwise raises the reading. There is no wiper capacitor.

## 9. Rules

1. **Never tie an RS232 Pal's V− to ground.** V− is the charge pump's −5.5 V output, not RS232 ground; tied down, the drivers stop working. RS232 ground is the module's GND pin.
2. **USB D+/D− go straight through:** J3 D+ to GPIO20, D− to GPIO19.
3. **Only one computer on the Serial USB at a time:** the rear socket and A1's own native socket share GPIO19/20.
4. **J3 stays at 5 V:** don't bridge another voltage jumper or connect SDA/SCL. 9 V or more would destroy U3 (5.5 V max) and A1's 5 V parts.
5. **The RS232 port uses only DB25 pins 2–8, 20 and 22.** Amiga serial ports carry supply voltages on other pins.
6. **Don't use GPIO 0, 3, 45 or 46 (strapping), 35–37 (PSRAM) or 47 (RGB LED)**, and keep the pot on ADC1 (GPIO1–10).
7. **Keep leads away from A1's antenna end**, and keep the I2S leads short and away from the RS232 wiring.
