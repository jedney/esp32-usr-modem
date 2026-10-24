// GPIO pin map (project brief 5.2, 6.14).
//
// *** PROPOSED by the Firmware chat, 24 Oct 2026, NOT YET CONFIRMED by Electronics. ***
// If the wiring ends up different, change the numbers here (and only here): every
// driver and test reads its pins from this file.
//
// Board: ESP32-S3 N16R8 DevKitC-1 clone (ePartners ST8002). Header pins as listed in
// brief 5.2, from the antenna end (pin 1) to the USB-C end:
//   Left:  3V3 3V3 RST [4 5 6 7 15 16 17] 18 8 (3) (46) 9 10 [11 12 13 14] 5V GND
//   Right: GND (43) (44) [1] 2 [42 41 40 39 38] (37 36 35) (0) (45) [48] (47) [21]
//          (20) (19) GND GND
// [ ] = used below; ( ) = not usable (strapping, PSRAM, USB, UART0 console, RGB LED).
// Grouping: the 7 RS232 lines are one run on the left (to Board B), the amp's 4 signals
// sit next to 5V and GND so the 6-wire amp lead comes from one block, and the LEDs and
// pot are on the right. Spare: 2 (ADC1), 8, 9 (ADC1), 10 (ADC1), 18.
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
constexpr int kI2sBclk = 11;
constexpr int kI2sLrc = 12;
constexpr int kI2sDin = 13;
constexpr int kAmpSd = 14;  // HIGH = on (left channel), LOW = shutdown

// --- Volume pot wiper, must be ADC1 (GPIO1-10) because ADC2 can't be used with Wi-Fi ---
constexpr int kPot = 1;

// --- Front-panel LEDs, in the order of hwtest::Led (AA, CD, RD, SD, TR, CS, ARQ) -------
constexpr int kLeds[7] = {42, 41, 40, 39, 38, 48, 21};
// GPIO -> resistor -> LED -> GND, so HIGH = lit. Set false if the LEDs are wired to 3V3
// and the GPIO sinks the current.
constexpr bool kLedActiveHigh = true;

// --- On-board RS232 test only (test/embedded/test_rs232) ---------------------------------
// UART2 plays the computer: its TX is routed inside the chip onto the TXD-in pin and
// UART1's TX onto its RX, so the test needs no wires. These two spare pins are only
// UART2's nominal pins; nothing is connected to them.
constexpr int kTestUartTx = 18;
constexpr int kTestUartRx = 8;

// --- On the S3 board itself -------------------------------------------------------------
constexpr int kRgbLed = 47;     // WS2812-type RGB LED (Jon, 22 Oct)
constexpr int kConsoleTx = 43;  // UART0 via the onboard USB-serial chip: console only
constexpr int kConsoleRx = 44;

}  // namespace pins
