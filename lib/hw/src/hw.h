// Hardware layer for the modem's own electronics: front-panel LEDs, MAX98357A audio,
// volume pot, the RS232 lines and data port, and the RMT speed capture (pins in pins.h).
//
// Nothing here touches a pin until it is first used, so a board with nothing (or
// something different) wired up is never driven by surprise.

#pragma once

#include <Arduino.h>

#include "autobaud.h"
#include "hwtest.h"
#include "tones.h"

namespace hw {

// --- Front-panel LEDs ------------------------------------------------------------------
// Bit i = hwtest::Led i (AA bit 0 ... ARQ bit 6), as in web/README.md.
void ledsSet(uint8_t mask);
uint8_t ledsMask();
// Firmware-update progress: the LEDs fill from AA to ARQ (hwtest::progressMask). Writes the
// pins only when the mask changes, so it can be called for every chunk.
void ledsProgress(uint32_t done, uint32_t total);

// --- Amplifier shutdown pin --------------------------------------------------------------
enum class AmpMode : uint8_t { kAuto, kOn, kOff };  // Auto: on only while a sound plays
void ampSetMode(AmpMode mode);
AmpMode ampMode();
bool ampIsOn();

// --- Audio -----------------------------------------------------------------------------
using StopFn = bool (*)();   // polled every block (16 ms); return true to stop
using GainFn = float (*)();  // user gain 0..1, polled every block

// Plays `src` at tones::kSampleRate through I2S at the given gain (volume::outputGain).
// Returns false if it was stopped early or the I2S driver couldn't start.
bool audioPlay(tones::Source &src, GainFn gain, StopFn stop);
bool audioReady();  // I2S driver started
bool audioStreamBegin();
size_t audioStreamWrite(const int16_t *samples, size_t count, float userGain);
void audioStreamEnd();

// --- Volume pot -------------------------------------------------------------------------
constexpr uint16_t kPotMax = 4095;
uint16_t potRaw();  // 0..4095, average of 16 readings
uint32_t potMillivolts();

// --- RS232 handshake lines (active-low at the GPIO, spec 3.2) --------------------------
void linesBegin();  // outputs deasserted, inputs with pull-ups
bool dtrAsserted();
bool rtsAsserted();
void lineSet(hwtest::OutSignal s, bool asserted);
bool lineIsAsserted(hwtest::OutSignal s);

// --- RS232 data on UART1 ------------------------------------------------------------------
// Plain UART1 for the hardware tests (loopback, bridge): 8N1, no flow control.
void uartBegin(uint32_t baud);
void uartEnd();
HardwareSerial &uart();

// --- RS232 host port: UART1 with its flow-control pins ------------------------------
// UART1's RTS output drives our CTS (DB25 5) and its CTS input reads the computer's RTS
// (DB25 4); both are active-low at the GPIO, which matches the MAX3232's inversion.
constexpr size_t kHostRxBuffer = 8192;
constexpr size_t kHostTxBuffer = 0;  // direct, nonblocking FIFO writes; no driver TX ring
void hostBegin(uint32_t baud);
void hostEnd();  // UART1 off, CTS deasserted
bool hostActive();
size_t hostWriteSpace();  // free hardware FIFO bytes; hostWrite never waits for RTS
size_t hostWrite(const uint8_t *data, size_t length);
bool hostDrain(uint32_t timeoutMs);  // bounded even when the computer holds RTS off
void hostSetBaud(uint32_t baud);
// ctsFromBuffer (&H1): CTS drops while the receive buffer is full; otherwise CTS is held
// on. obeyRts (&R2): sending pauses while the computer's RTS is off.
void hostSetFlow(bool ctsFromBuffer, bool obeyRts);
bool ctsOutAsserted();  // the CTS output pin read back (for the CS LED)

// --- Speed capture: RMT receiver on the TXD-in pin -------------------------------------------
// Records the computer's data as runs of constant level (1 us steps); a burst is handed
// over once the line has been idle for kCaptureIdleUs. See lib/hostport/src/autobaud.h.
constexpr uint32_t kCaptureIdleUs = 32000;
bool captureBegin();  // false if the RMT driver couldn't start
void captureEnd();
bool captureActive();
// Copies the next finished burst. Returns the number of runs (0 if none is waiting).
size_t captureRead(autobaud::Run *runs, size_t cap);

// --- Task watchdog --------------------------------------------------------------

// The main loop is watched from the end of setup() (enableLoopWDT(); the core feeds it
// before every loop() pass). An operation that keeps one pass longer than the time-out
// (CONFIG_ESP_TASK_WDT_TIMEOUT_S) calls this as it goes. From another task it does nothing.
void feedWatchdog();
// The least free stack `task` has had since it started, in bytes; -1 if there is no task
// of that name.
int stackHeadroom(const char *task);

}  // namespace hw
