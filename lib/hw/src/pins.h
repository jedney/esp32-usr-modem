// GPIO pin map (project brief 5.2, 6.14).
//
// *** CONFIRMED by Electronics, 24 Oct 2026, schematic rev A (Q76). ***
// Every driver and test reads its pins from this file; change them here and only here.
// test/native/test_pins locks in these values, so a pin can't change by accident.
//
// Board: ESP32-S3 N16R8 DevKitC-1 clone (ePartners ST8002). Header pins from the antenna
// end (pin 1) to the USB-C end:
//   Left:  L1-3 3V3 3V3 RST | L4-L10 RS232 [4 5 6 7 15 16 17] | L11 18 | L12 8 | L13 (3)
//          L14 (46) | L15 9 | L16 pot [10] | L17-L20 [11 LRC, 12 BCLK, 13 DIN, 14 SD]
//          L21 5V | L22 GND
//   Right: R1 GND | R2 (43) | R3 (44) | R4-R10 LEDs [1 AA, 2 CD, 42 RD, 41 SD, 40 TR,
//          39 CS, 38 ARQ] | R11-13 (37 36 35) | R14 (0) | R15 (45) | R16 48 | R17 (47)
//          R18 21 | R19 (20) | R20 (19) | R21-22 GND GND
// [ ] = used below; ( ) = not usable (strapping, PSRAM, USB, UART0 console, RGB LED).
// Grouping: the 7 RS232 lines are one run on the left (to Board B), the amp's 4 signals
// sit next to 5V and GND so the 6-wire amp lead comes from one block, and the seven LEDs
// are one run on the right in front-panel order. Spare: 8, 9, 18, 21, 48 (18 and 8 are
// also the nominal pins of the test-only UART2 below; nothing is connected to them).
//
// Boot state (Q77): the board has 10 k pull-ups on the four MAX3232 driver inputs (RXD,
// CTS, DCD, RI), so the computer sees idle and "off" while the S3 boots, and a 10 k
// pull-down on amp SD, so the amp stays off until the firmware turns it on.
//
// Polarity: the MAX3232 inverts both ways, so on the TTL side an RS232 signal that is
// asserted (ON, +V on the cable) is LOW at the GPIO (brief 6.2).

#pragma once

#include <cstdint>

namespace pins {

// --- RS232, DCE (names are the RS232 signals as seen on the DB25) ------------------------
// Modem outputs (S3 -> MAX3232 driver input):
constexpr int kRxd = 4;   // DB25 3, data to the computer       (UART1 TX)
constexpr int kCts = 6;   // DB25 5, clear to send               (UART1 RTS output)
constexpr int kDcd = 16;  // DB25 8, carrier detect               (GPIO)
constexpr int kRi = 17;   // DB25 22, ring indicator               (GPIO)
// Modem inputs (MAX3232 receiver output -> S3):
constexpr int kTxd = 5;   // DB25 2, data from the computer      (UART1 RX)
constexpr int kRts = 7;   // DB25 4, request to send              (UART1 CTS input)
constexpr int kDtr = 15;  // DB25 20, data terminal ready          (GPIO)
// DSR (DB25 6) is hard-wired asserted (brief 5.1): no pin.

// --- Audio: MAX98357A (brief 5.3) --------------------------------------------------------
constexpr int kI2sLrc = 11;
constexpr int kI2sBclk = 12;
constexpr int kI2sDin = 13;
constexpr int kAmpSd = 14;  // HIGH = on (left channel), LOW = shutdown (10 k pull-down)

// --- Volume pot wiper, must be ADC1 (GPIO1-10) because ADC2 can't be used with Wi-Fi ---
constexpr int kPot = 10;

// --- Front-panel LEDs, in the order of hwtest::Led (AA, CD, RD, SD, TR, CS, ARQ) -------
constexpr int kLeds[7] = {1, 2, 42, 41, 40, 39, 38};
// GPIO -> resistor -> LED -> GND, so HIGH = lit.
constexpr bool kLedActiveHigh = true;

// --- On-board RS232 test only (test/embedded/test_rs232) ---------------------------------
// UART2 plays the computer: its TX is routed inside the chip onto the TXD-in pin and
// UART1's TX onto its RX, so the test needs no wires. These two spare pins are only
// UART2's nominal pins; nothing is connected to them.
constexpr int kTestUartTx = 18;
constexpr int kTestUartRx = 8;

// --- On the S3 board itself -------------------------------------------------------------
constexpr int kRgbLed = 47;     // WS2812-type RGB LED (Jon, 22 Oct): Wi-Fi status (v0.6)
constexpr int kConsoleTx = 43;  // UART0 via the onboard USB-serial chip: console only
constexpr int kConsoleRx = 44;

}  // namespace pins
