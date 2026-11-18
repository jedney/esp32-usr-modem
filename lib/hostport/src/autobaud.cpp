// Host speed detection and 8N1 decoding: see autobaud.h.
#include "autobaud.h"

namespace autobaud {

namespace {

constexpr uint64_t kNsPerUs = 1000;

// Walks the runs in time order. Times are nanoseconds from the start of the burst and
// must not go backwards between calls.
class Cursor {
public:
    Cursor(const Run *runs, size_t n) : runs_(runs), n_(n) {}

    // Level of the line at time t. After the last run the line is idle (1).
    uint8_t levelAt(uint64_t t) {
        while (i_ < n_ && runs_[i_].us != 0 && start_ + runs_[i_].us * kNsPerUs <= t) {
            start_ += runs_[i_].us * kNsPerUs;
            ++i_;
        }
        return i_ < n_ ? runs_[i_].level : 1;
    }

    // Start of the first low run that begins at or after t (a start bit).
    bool nextFall(uint64_t t, uint64_t *at) {
        while (i_ < n_) {
            if (runs_[i_].level == 0 && start_ >= t) {
                *at = start_;
                return true;
            }
            if (runs_[i_].us == 0) {
                return false;  // the line stayed at this level to the end
            }
            start_ += runs_[i_].us * kNsPerUs;
            ++i_;
        }
        return false;
    }

private:
    const Run *runs_;
    size_t n_;
    size_t i_ = 0;
    uint64_t start_ = 0;
};

bool matches(const Run *r, const uint8_t *bits, uint64_t bitNs) {
    for (int j = 0; j < 5; ++j) {
        uint64_t got = r[j].us * kNsPerUs;
        uint64_t want = bits[j] * bitNs;
        uint64_t diff = got > want ? got - want : want - got;
        if (diff * 5 > bitNs * 2) {  // more than 0.4 of a bit out
            return false;
        }
    }
    return true;
}

bool startsWithAt(const uint8_t *b, size_t len) {
    if (len == 0) {
        return false;
    }
    if (b[0] == 'A') {
        return len == 1 || b[1] == 'T';
    }
    if (b[0] == 'a') {
        return len == 1 || b[1] == 't';
    }
    return false;
}

}  // namespace

uint32_t snapRate(uint64_t bitNs) {
    for (uint32_t r : modemcfg::kHostRates) {
        uint64_t nominal = 1000000000ull / r;
        uint64_t diff = bitNs > nominal ? bitNs - nominal : nominal - bitNs;
        if (diff * 100 <= nominal * 6) {
            return r;
        }
    }
    return 0;
}

uint32_t rateOfA(const Run *runs, size_t n) {
    size_t i = 0;
    while (i < n && runs[i].level != 0) {
        ++i;  // idle before the start bit
    }
    // a capture can hold fewer runs than a character needs
    // cppcheck-suppress knownConditionTrueFalse
    if (n - i < 6) {
        return 0;  // start, b0, b1.., b6.., b7 and the stop bit
    }
    const Run *r = runs + i;
    uint64_t total = 0;
    for (int j = 0; j < 5; ++j) {
        if (r[j].us == 0 || r[j].level != (j % 2 == 0 ? 0 : 1)) {
            return 0;
        }
        total += r[j].us * kNsPerUs;
    }
    if (r[5].level != 1) {
        return 0;
    }
    uint64_t bitNs = total / 9;  // start bit to stop bit is 9 bits for both letters
    static const uint8_t kUpper[5] = {1, 1, 5, 1, 1};  // 'A' = 0x41
    static const uint8_t kLower[5] = {1, 1, 4, 2, 1};  // 'a' = 0x61
    if (!matches(r, kUpper, bitNs) && !matches(r, kLower, bitNs)) {
        return 0;
    }
    if (r[5].us != 0 && r[5].us * kNsPerUs * 2 < bitNs) {
        return 0;  // stop bit too short
    }
    return snapRate(bitNs);
}

size_t decode(const Run *runs, size_t n, uint32_t baud, uint8_t *out, size_t cap) {
    if (baud == 0 || runs == nullptr || out == nullptr) {
        return 0;
    }
    const uint64_t bit16 = 16000000000ull / baud;  // bit time in 1/16 ns
    Cursor c(runs, n);
    uint64_t pos = 0;
    uint64_t t0 = 0;
    size_t len = 0;
    while (len < cap && c.nextFall(pos, &t0)) {
        uint8_t b = 0;
        for (unsigned k = 0; k < 8; ++k) {  // sample each data bit in its middle
            if (c.levelAt(t0 + bit16 * (3 + 2 * k) / 32) != 0) {
                b = static_cast<uint8_t>(b | (1u << k));
            }
        }
        const uint64_t stopMid = t0 + bit16 * 19 / 32;  // 9.5 bits after the start
        if (c.levelAt(stopMid) != 0) {
            out[len++] = b;
        }
        pos = stopMid;
    }
    return len;
}

Burst decodeBurst(const Run *runs, size_t n, uint32_t current, uint8_t *out, size_t cap) {
    uint32_t rate = rateOfA(runs, n);
    if (rate != 0 && rate != current) {
        size_t len = decode(runs, n, rate, out, cap);
        if (startsWithAt(out, len)) {
            return Burst{rate, true, len};
        }
    }
    return Burst{current, false, decode(runs, n, current, out, cap)};
}

}  // namespace autobaud
