// Hardware layer for the modem's own electronics: front-panel LEDs, MAX98357A audio,
// volume pot and the RS232 lines (pins in pins.h).
//
// Nothing here touches a pin until it is first used, so a board with nothing (or
// something different) wired up is never driven by surprise.

#pragma once

#include <Arduino.h>

#include "hwtest.h"
#include "tones.h"

namespace hw {

// --- Front-panel LEDs ------------------------------------------------------------------
// Bit i = hwtest::Led i (AA bit 0 ... ARQ bit 6), as in web/PROTOCOL.md.
void ledsSet(uint8_t mask);
uint8_t ledsMask();

// --- Amplifier shutdown pin --------------------------------------------------------------
enum class AmpMode : uint8_t { kAuto, kOn, kOff };  // Auto: on only while a sound plays
void ampSetMode(AmpMode mode);
AmpMode ampMode();
bool ampIsOn();

// --- Audio -----------------------------------------------------------------------------
using StopFn = bool (*)();   // polled every block (16 ms); return true to stop
using GainFn = float (*)();  // user gain 0..1, polled every block; the cap is applied here

// Plays `src` at tones::kSampleRate through I2S, with the volume cap (volume.h) applied
// last. Returns false if it was stopped early or the I2S driver couldn't start.
bool audioPlay(tones::Source &src, GainFn gain, StopFn stop);
bool audioReady();  // I2S driver started

// --- Volume pot -------------------------------------------------------------------------
constexpr uint16_t kPotMax = 4095;
uint16_t potRaw();  // 0..4095, average of 16 readings
uint32_t potMillivolts();

// --- RS232 handshake lines (active-low at the GPIO, brief 6.2) --------------------------
void linesBegin();  // outputs deasserted, inputs with pull-ups
bool dtrAsserted();
bool rtsAsserted();
void lineSet(hwtest::OutSignal s, bool asserted);
bool lineIsAsserted(hwtest::OutSignal s);

// --- RS232 data on UART1 ------------------------------------------------------------------
void uartBegin(uint32_t baud);  // 8N1, no flow control
void uartEnd();
HardwareSerial &uart();

}  // namespace hw
