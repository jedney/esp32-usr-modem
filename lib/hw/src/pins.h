// GPIO pin map (project brief 5.2, 6.14).
//
// *** Rev B (project brief Q89), replacing the rev A map confirmed in Q76. ***
// Every driver and test reads its pins from this file; change them here and only here.
// test/native/test_pins locks in these values, so a pin can't change by accident.
//
// Board: ESP32-S3 N16R8 DevKitC-1 clone (ePartners ST8002). Header J1 = left (L), J3 =
// right (R); pin 1 is at the antenna end.
//   Left:  L1 3V3 out (RS232 Pals VIN) | L2 3V3 out (pot) | L3 RST (not wired)
//          L4-L10 LEDs [4 AA, 5 CD, 6 RD, 7 SD, 15 TR, 16 CS, 17 ARQ]
//          L11 18 (test UART2 TX) | L12 8 (test UART2 RX) | L13 (3) | L14 (46) | L15 9
//          L16 pot [10] | L17-L20 [11 LRC, 12 BCLK, 13 DIN, 14 SD] | L21 5V in | L22 GND
//   Right: R1 GND (not connected) | R2 (43) | R3 (44)
//          R4-R10 RS232 [1 RXD, 2 TXD, 42 CTS, 41 RTS, 40 DTR, 39 DCD, 38 RI]
//          R11-13 (37 36 35) | R14 (0) | R15 (45) | R16 48 | R17 (47) | R18 21
//          R19 (20) | R20 (19) | R21-22 GND (not connected)
// [ ] = used below; ( ) = not usable (strapping, PSRAM, USB, UART0 console, RGB LED).
// Spare: 8, 9, 18, 21, 48 (18 and 8 are also the nominal pins of the test-only UART2
// below; nothing is connected to them). 19/20 (native USB) are also wired to the USB-C
// PD board's D-/D+. Amp VIN and GND come from the PD board, not the S3.
//
// RS232 goes through two RS232 Pal modules (U1, U2) with a DB9 DCE pinout. The DB9 pins
// are given beside each signal below.
//
// Polarity: the RS232 Pals invert both ways, so on the TTL side an RS232 signal that is
// asserted (ON, +V on the cable) is LOW at the GPIO (brief 6.2).

#pragma once

#include <cstdint>

namespace pins {

// --- RS232, DCE (names are the RS232 signals as seen on the DB9) -------------------------
// Modem outputs (S3 -> RS232 Pal driver input):
constexpr int kRxd = 1;   // DB9 2, data to the computer        (UART1 TX)
constexpr int kCts = 42;  // DB9 8, clear to send                (UART1 RTS output)
constexpr int kDcd = 39;  // DB9 1, carrier detect                (GPIO)
constexpr int kRi = 38;   // DB9 9, ring indicator                 (GPIO)
// Modem inputs (RS232 Pal receiver output -> S3):
constexpr int kTxd = 2;   // DB9 3, data from the computer       (UART1 RX)
constexpr int kRts = 41;  // DB9 7, request to send               (UART1 CTS input)
constexpr int kDtr = 40;  // DB9 4, data terminal ready            (GPIO)
// DSR (DB9 6) is hard-wired on from U2 V+ through 1 k: no pin.

// --- Audio: MAX98357A (brief 5.3) --------------------------------------------------------
constexpr int kI2sLrc = 11;
constexpr int kI2sBclk = 12;
constexpr int kI2sDin = 13;
constexpr int kAmpSd = 14;  // HIGH = on (left channel), LOW = shutdown (10 k pull-down)

// --- Volume pot wiper, must be ADC1 (GPIO1-10) because ADC2 can't be used with Wi-Fi ---
constexpr int kPot = 10;

// --- Front-panel LEDs, in the order of hwtest::Led (AA, CD, RD, SD, TR, CS, ARQ) -------
constexpr int kLeds[7] = {4, 5, 6, 7, 15, 16, 17};
// GPIO -> resistor -> LED -> GND, so HIGH = lit.
constexpr bool kLedActiveHigh = true;

// --- On-board RS232 test only (test/embedded/test_rs232) ---------------------------------
// UART2 plays the computer: its TX is routed inside the chip onto the TXD-in pin and
// UART1's TX onto its RX, so the test needs no wires. These two spare pins (L11, L12) are only
// UART2's nominal pins; nothing is connected to them.
constexpr int kTestUartTx = 18;
constexpr int kTestUartRx = 8;

// --- On the S3 board itself -------------------------------------------------------------
constexpr int kRgbLed = 47;     // WS2812-type RGB LED (Jon, 22 Oct): Wi-Fi status (v0.6)
constexpr int kConsoleTx = 43;  // UART0 via the onboard USB-serial chip: console only
constexpr int kConsoleRx = 44;

}  // namespace pins
