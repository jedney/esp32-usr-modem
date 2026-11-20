// Host port line rules (spec 3.2): the "+++" escape with guard time, DTR
// debouncing and what a DTR drop does (&D), DCD (&C) and flow control (&H &I &R).
// Behaviour follows the USR Sportster technical reference.
//
// Times are milliseconds from any free-running clock (millis() on the board), passed in
// so the rules can be unit-tested on the PC. No Arduino dependencies.

#pragma once

#include <cstddef>
#include <cstdint>

#include "modemcfg.h"

namespace hostport {

// --- "+++" escape (S2 escape character, S12 guard time in 1/50 s) ------------------------
// The sequence is: no data for the guard time, three escape characters each less than the
// guard time apart, then no data for the guard time. A fourth escape character or any
// other byte cancels it. S2 > 127 turns the escape off; S12 = 0 means no guard time.
// The escape characters are still sent on as data (Hayes behaviour).
class EscapeDetector {
public:
    void reset(uint32_t nowMs);  // call when going online
    void onByte(uint8_t b, uint32_t nowMs, const modemcfg::Profile &p);
    // True once, when the guard time after the third escape character has passed.
    bool poll(uint32_t nowMs, const modemcfg::Profile &p);

private:
    uint32_t last_ = 0;
    uint8_t count_ = 0;
};

uint32_t guardMs(const modemcfg::Profile &p);

// --- DTR ----------------------------------------------------------------------------------
// A DTR change counts once it has lasted S25 hundredths of a second.
class DtrFilter {
public:
    void reset(bool on);
    // Returns true when the filtered DTR goes from on to off.
    bool update(bool raw, uint32_t nowMs, const modemcfg::Profile &p);
    bool on() const { return stable_; }

private:
    bool stable_ = false;
    bool pending_ = false;
    uint32_t since_ = 0;
};

enum class DtrAction : uint8_t {
    kNone,    // &D0: DTR is ignored
    kHangUp,  // &D2: hang up and go back to command mode
    kReset,   // &D3: hang up, then reset to the power-on template (as ATZ)
};
DtrAction dtrAction(const modemcfg::Profile &p);

// --- DCD ----------------------------------------------------------------------------------
// &C0: always on. &C1: on while a call is connected (also in online-command mode).
bool dcdOn(const modemcfg::Profile &p, bool callUp);

// --- Flow control -------------------------------------------------------------------------
struct FlowMode {
    bool ctsFromBuffer;  // &H1/&H3: drop CTS while the receive buffer is full (UART RTS)
    bool obeyRts;        // &R2: stop sending while the computer's RTS is off (UART CTS)
    bool sendXonXoff;    // &H2/&H3: send XOFF/XON to the computer
    bool obeyXonXoff;    // &I1/&I2: stop sending after XOFF from the computer
    bool stripXonXoff;   // &I2: those XON/XOFF bytes are not passed on to the call
};
FlowMode flowMode(const modemcfg::Profile &p);
bool operator==(const FlowMode &a, const FlowMode &b);
inline bool operator!=(const FlowMode &a, const FlowMode &b) {
    return !(a == b);
}

// &H2: when to send XOFF (receive buffer 3/4 full) and XON again (1/4 full).
class XoffSender {
public:
    // `level` = bytes waiting in a buffer of `cap`. Returns the byte to send (S23 XOFF or
    // S22 XON), or -1 for none.
    int update(size_t level, size_t cap, const modemcfg::Profile &p);
    void reset() { sent_ = false; }
    bool paused() const { return sent_; }

private:
    bool sent_ = false;
};

}  // namespace hostport
