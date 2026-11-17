// GPIO pin map (spec 2.5, 3.13).
//
// The pin NUMBERS are not in the source: scripts/pin_config.py reads USR_MODEM_PIN_* from
// .env.example (the defaults), then .env, then the environment, and passes them to the
// compiler. Every driver and test reads its pins from this file.
// test/native/test_pins locks in the .env.example values, so a pin can't change by accident.
// The header layout below describes the defaults.
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
// [ ] = used below; ( ) = not usable (strapping, PSRAM, USB, UART0 console, and 47: the
// board's own RGB LED, wired on the DevKit but not used).
// Spare: 8, 9, 18, 21, 48 (18 and 8 are also the nominal pins of the test-only UART2
// below; nothing is connected to them). 19/20 (native USB) are also wired to the USB-C
// PD board's D-/D+: they are the USB modem port (TinyUSB, lib/hw/src/usb_port.h), reached
// through the rear USB-C socket. Amp VIN and GND come from the PD board, not the S3.
//
// RS232 goes through two RS232 Pal modules (U1, U2) with a DB9 DCE pinout. The DB9 pins
// are given beside each signal below.
//
// Polarity: the RS232 Pals invert both ways, so on the TTL side an RS232 signal that is
// asserted (ON, +V on the cable) is LOW at the GPIO (spec 3.2).

#pragma once

#include <cstdint>

#ifndef USR_MODEM_PIN_RXD
#error "GPIO pins are defined by scripts/pin_config.py (USR_MODEM_PIN_* in .env.example)"
#endif

namespace pins {

// --- RS232, DCE (names are the RS232 signals as seen on the DB9) -------------------------
// Modem outputs (S3 -> RS232 Pal driver input):
constexpr int kRxd = USR_MODEM_PIN_RXD;  // DB9 2, data to the computer        (UART1 TX)
constexpr int kCts = USR_MODEM_PIN_CTS;  // DB9 8, clear to send                (UART1 RTS output)
constexpr int kDcd = USR_MODEM_PIN_DCD;  // DB9 1, carrier detect                (GPIO)
constexpr int kRi = USR_MODEM_PIN_RI;    // DB9 9, ring indicator                 (GPIO)
// Modem inputs (RS232 Pal receiver output -> S3):
constexpr int kTxd = USR_MODEM_PIN_TXD;  // DB9 3, data from the computer       (UART1 RX)
constexpr int kRts = USR_MODEM_PIN_RTS;  // DB9 7, request to send               (UART1 CTS input)
constexpr int kDtr = USR_MODEM_PIN_DTR;  // DB9 4, data terminal ready            (GPIO)
// DSR (DB9 6) is hard-wired on from U2 V+ through 1 k: no pin.

// --- Audio: MAX98357A (spec 2.3) --------------------------------------------------------
constexpr int kI2sLrc = USR_MODEM_PIN_I2S_LRC;
constexpr int kI2sBclk = USR_MODEM_PIN_I2S_BCLK;
constexpr int kI2sDin = USR_MODEM_PIN_I2S_DIN;
constexpr int kAmpSd = USR_MODEM_PIN_AMP_SD;  // HIGH = on (left channel), LOW = shutdown

// --- Volume pot wiper, must be ADC1 (GPIO1-10) because ADC2 can't be used with Wi-Fi ---
constexpr int kPot = USR_MODEM_PIN_POT;

// --- Front-panel LEDs, in the order of hwtest::Led (AA, CD, RD, SD, TR, CS, ARQ) -------
constexpr int kLeds[7] = {USR_MODEM_PIN_LED_AA, USR_MODEM_PIN_LED_CD, USR_MODEM_PIN_LED_RD,
                          USR_MODEM_PIN_LED_SD, USR_MODEM_PIN_LED_TR, USR_MODEM_PIN_LED_CS,
                          USR_MODEM_PIN_LED_ARQ};
// GPIO -> LED anode; cathode -> GND, so HIGH = lit.
constexpr bool kLedActiveHigh = true;

// --- On-board RS232 test only (test/embedded/test_rs232) ---------------------------------
// UART2 plays the computer: its TX is routed inside the chip onto the TXD-in pin and
// UART1's TX onto its RX, so the test needs no wires. These two spare pins (L11, L12) are only
// UART2's nominal pins; nothing is connected to them.
constexpr int kTestUartTx = USR_MODEM_PIN_TEST_UART_TX;
constexpr int kTestUartRx = USR_MODEM_PIN_TEST_UART_RX;

// --- On the S3 board itself -------------------------------------------------------------
constexpr int kConsoleTx =
    USR_MODEM_PIN_CONSOLE_TX;  // UART0 via the onboard USB-serial chip: console only
constexpr int kConsoleRx = USR_MODEM_PIN_CONSOLE_RX;

}  // namespace pins
