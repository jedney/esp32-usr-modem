// Front-panel LED rules: see panel.h.
#include "panel.h"

#include "hwtest.h"

namespace hostport {

namespace {

// Keeps an activity LED lit for kStretchMs after the last byte.
bool stretch(uint32_t count, uint32_t *last, uint32_t *since, bool *lit, uint32_t nowMs) {
    if (count != *last) {
        *last = count;
        *since = nowMs;
        *lit = true;
    } else if (*lit && nowMs - *since >= Panel::kStretchMs) {
        *lit = false;
    }
    return *lit;
}

uint8_t bit(hwtest::Led led, bool on) {
    return on ? static_cast<uint8_t>(1u << led) : 0;
}

}  // namespace

uint8_t Panel::update(const PanelInput &in, uint32_t nowMs) {
    if (!started_) {  // counts from before the first call don't flash the LEDs
        lastTo_ = in.toComputer;
        lastFrom_ = in.fromComputer;
        started_ = true;
    }
    const bool rd = stretch(in.toComputer, &lastTo_, &rdSince_, &rd_, nowMs);
    const bool sd = stretch(in.fromComputer, &lastFrom_, &sdSince_, &sd_, nowMs);
    const bool aa = in.ringing ? (nowMs / kRingFlashMs) % 2 == 0 : in.autoAnswer;
    const bool arq = in.wifi == WifiLed::kConnected ||
                     (in.wifi == WifiLed::kConnecting && (nowMs / kWifiBlinkMs) % 2 == 0);
    return static_cast<uint8_t>(bit(hwtest::kAA, aa) | bit(hwtest::kCD, in.cd) |
                                bit(hwtest::kRD, rd) | bit(hwtest::kSD, sd) |
                                bit(hwtest::kTR, in.tr) | bit(hwtest::kCS, in.cs) |
                                bit(hwtest::kARQ, arq));
}

}  // namespace hostport
