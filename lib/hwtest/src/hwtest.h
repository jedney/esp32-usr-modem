// Hardware-test helpers (spec 3.13): LED names, test patterns and the argument
// parsing shared by the AT*HW... console commands.
//
// No Arduino dependencies: this module is unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hwtest {

// Front-panel LEDs, in the bit order of the web page's `led` message (web/PROTOCOL.md).
enum Led : uint8_t { kAA = 0, kCD, kRD, kSD, kTR, kCS, kARQ, kLedCount };

// "AA", "CD", ... "ARQ".
const char *ledName(uint8_t led);

// Parses an LED name (case-insensitive; "FAX" is accepted for ARQ). Returns -1 if `s` is
// not an LED name. The name must be followed by the end of the string, a comma or a space.
int parseLed(const char *s);

// LED mask for one step of a back-and-forth sweep: AA, CD ... ARQ, CS ... CD, AA, ...
uint8_t chaseMask(uint32_t step);

// RS232 handshake outputs the tests can drive (DSR is hard-wired on, spec 2.2).
enum class OutSignal : uint8_t { kDCD, kRI, kCTS };
const char *outSignalName(OutSignal s);
bool parseOutSignal(const char *s, OutSignal *out);

// --- Argument parsing ------------------------------------------------------------------
// Commands accept both "AT*HWLED=CD" and "AT*HWLED CD".

// Skips leading spaces and at most one '=' (with spaces around it).
const char *argStart(const char *s);

// True if `s` starts with `word` (case-insensitive) followed by the end, ',' or a space.
// `rest` (optional) is set to the text after the word and any separator.
bool wordIs(const char *s, const char *word, const char **rest = nullptr);

// Decimal number in [lo, hi], followed by the end, ',' or a space. `rest` as above.
bool parseUint(const char *s, uint32_t lo, uint32_t hi, uint32_t *out, const char **rest = nullptr);

// ON/OFF, 1/0.
bool parseOnOff(const char *s, bool *on);

// --- Time-out and call rules -------------------------------------------------------------

// An interactive test ("any key stops", prompts, bridge) ends after this long with no key,
// so a closed terminal or a forgotten test can't hold the modem for ever.
constexpr uint32_t kIdleTimeoutMs = 5u * 60u * 1000u;

// True when `timeoutMs` or more has passed since `lastMs`. Safe when millis() wraps.
bool idleExpired(uint32_t lastMs, uint32_t nowMs, uint32_t timeoutMs = kIdleTimeoutMs);

// Whether `args` (the text after "AT*HW", upper case) may run while a call is up on another
// port. The LED patterns run until a key, so only the single-LED forms are allowed.
bool allowedDuringCall(const char *args);

// A setting a test leaves in place (LEDs, amp mode, RS232 outputs) and the kind of port
// that set it. When the browser leaves it releases only the holds it made itself.
class Hold {
public:
    bool active() const { return owner_ != kNone; }
    void take(bool byBrowser) { owner_ = byBrowser ? kBrowser : kOther; }
    void release() { owner_ = kNone; }
    bool releaseForBrowser() {
        if (owner_ != kBrowser) {
            return false;
        }
        owner_ = kNone;
        return true;
    }

private:
    enum Owner : uint8_t { kNone, kBrowser, kOther };
    Owner owner_ = kNone;
};

// --- RS232 loopback ---------------------------------------------------------------------

// Test byte `i` of the loopback pattern. Any 256 consecutive bytes contain every value
// 0-255 once, in an order that mixes neighbouring bit patterns.
uint8_t loopbackByte(size_t i);

}  // namespace hwtest
