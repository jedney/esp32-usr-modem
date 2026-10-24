// Host port line rules: see linectl.h.
#include "linectl.h"

namespace hostport {

using modemcfg::Profile;

// --- "+++" escape -------------------------------------------------------------------------

uint32_t guardMs(const Profile &p) {
    return static_cast<uint32_t>(p.s[12]) * 20u;  // S12 is in fiftieths of a second
}

void EscapeDetector::reset(uint32_t nowMs) {
    last_ = nowMs;
    count_ = 0;
}

void EscapeDetector::onByte(uint8_t b, uint32_t nowMs, const Profile &p) {
    const uint32_t guard = guardMs(p);
    const uint32_t gap = nowMs - last_;
    const bool escape = p.s[2] <= 127 && b == p.s[2];
    if (!escape || count_ >= 3) {
        count_ = 0;  // data, or a fourth escape character
    } else if (count_ == 0) {
        count_ = gap >= guard ? 1 : 0;  // guard time before the first one
    } else if (guard == 0 || gap < guard) {
        ++count_;
    } else {
        count_ = 1;  // too slow: this one may start a new sequence
    }
    last_ = nowMs;
}

bool EscapeDetector::poll(uint32_t nowMs, const Profile &p) {
    if (count_ == 3 && nowMs - last_ >= guardMs(p)) {
        count_ = 0;
        return true;
    }
    return false;
}

// --- DTR ----------------------------------------------------------------------------------

void DtrFilter::reset(bool on) {
    stable_ = on;
    pending_ = on;
}

bool DtrFilter::update(bool raw, uint32_t nowMs, const Profile &p) {
    if (raw == stable_) {
        pending_ = raw;
        return false;
    }
    if (pending_ != raw) {
        pending_ = raw;
        since_ = nowMs;
    }
    const uint32_t hold = static_cast<uint32_t>(p.s[25]) * 10u;  // S25 in 0.01 s
    if (nowMs - since_ < hold) {
        return false;
    }
    stable_ = raw;
    return !raw;
}

DtrAction dtrAction(const Profile &p) {
    switch (p.get(modemcfg::kAmpD)) {
        case 2:
            return DtrAction::kHangUp;
        case 3:
            return DtrAction::kReset;
        default:
            return DtrAction::kNone;
    }
}

// --- DCD ----------------------------------------------------------------------------------

bool dcdOn(const Profile &p, bool callUp) {
    return p.get(modemcfg::kAmpC) == 0 || callUp;
}

// --- Flow control -------------------------------------------------------------------------

FlowMode flowMode(const Profile &p) {
    const unsigned h = p.get(modemcfg::kAmpH);
    const unsigned i = p.get(modemcfg::kAmpI);
    FlowMode f = {};
    f.ctsFromBuffer = h == 1 || h == 3;
    f.sendXonXoff = h == 2 || h == 3;
    f.obeyRts = p.get(modemcfg::kAmpR) == 2;
    f.obeyXonXoff = i == 1 || i == 2;
    f.stripXonXoff = i == 2;
    return f;
}

bool operator==(const FlowMode &a, const FlowMode &b) {
    return a.ctsFromBuffer == b.ctsFromBuffer && a.obeyRts == b.obeyRts &&
           a.sendXonXoff == b.sendXonXoff && a.obeyXonXoff == b.obeyXonXoff &&
           a.stripXonXoff == b.stripXonXoff;
}

int XoffSender::update(size_t level, size_t cap, const Profile &p) {
    if (!flowMode(p).sendXonXoff) {
        if (sent_) {
            sent_ = false;
            return p.s[22];  // flow control turned off while paused: release the computer
        }
        return -1;
    }
    if (!sent_ && level * 4 >= cap * 3) {
        sent_ = true;
        return p.s[23];
    }
    if (sent_ && level * 4 <= cap) {
        sent_ = false;
        return p.s[22];
    }
    return -1;
}

}  // namespace hostport
