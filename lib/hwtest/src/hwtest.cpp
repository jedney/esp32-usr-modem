#include "hwtest.h"

#include <cctype>
#include <cstring>

namespace hwtest {

namespace {

const char *const kLedNames[kLedCount] = {"AA", "CD", "RD", "SD", "TR", "CS", "ARQ"};

bool isSeparator(char c) {
    return c == '\0' || c == ',' || c == ' ';
}

const char *skipSeparator(const char *s) {
    while (*s == ' ') {
        ++s;
    }
    if (*s == ',') {
        ++s;
        while (*s == ' ') {
            ++s;
        }
    }
    return s;
}

}  // namespace

const char *ledName(uint8_t led) {
    return led < kLedCount ? kLedNames[led] : "?";
}

int parseLed(const char *s) {
    for (uint8_t i = 0; i < kLedCount; ++i) {
        if (wordIs(s, kLedNames[i])) {
            return i;
        }
    }
    return wordIs(s, "FAX") ? kARQ : -1;
}

uint8_t chaseMask(uint32_t step) {
    constexpr uint32_t kPeriod = 2 * (kLedCount - 1);  // 0..6 then 5..1
    uint32_t pos = step % kPeriod;
    uint32_t led = pos < kLedCount ? pos : kPeriod - pos;
    return static_cast<uint8_t>(1u << led);
}

uint8_t progressMask(uint32_t done, uint32_t total) {
    if (total == 0 || done == 0) {
        return 0;
    }
    if (done >= total) {
        return (1u << kLedCount) - 1;
    }
    // AA from the first byte, then one more LED per sixth; ARQ only at the end.
    uint64_t lit = 1 + uint64_t(done) * (kLedCount - 1) / total;
    return static_cast<uint8_t>((1u << lit) - 1);
}

const char *outSignalName(OutSignal s) {
    switch (s) {
        case OutSignal::kDCD:
            return "DCD";
        case OutSignal::kRI:
            return "RI";
        case OutSignal::kCTS:
        default:
            return "CTS";
    }
}

bool parseOutSignal(const char *s, OutSignal *out) {
    if (wordIs(s, "DCD") || wordIs(s, "CD")) {
        *out = OutSignal::kDCD;
    } else if (wordIs(s, "RI")) {
        *out = OutSignal::kRI;
    } else if (wordIs(s, "CTS") || wordIs(s, "CS")) {
        *out = OutSignal::kCTS;
    } else {
        return false;
    }
    return true;
}

bool idleExpired(uint32_t lastMs, uint32_t nowMs, uint32_t timeoutMs) {
    return static_cast<uint32_t>(nowMs - lastMs) >= timeoutMs;
}

bool allowedDuringCall(const char *args) {
    auto starts = [args](const char *prefix) { return strncmp(args, prefix, strlen(prefix)) == 0; };
    if (starts("LED")) {
        const char *a = argStart(args + 3);
        return parseLed(a) >= 0 || wordIs(a, "ALL") || wordIs(a, "OFF") || wordIs(a, "NONE") ||
               wordIs(a, "AUTO");
    }
    return strcmp(args, "POT?") == 0 || strcmp(args, "PINS") == 0 || strcmp(args, "INFO") == 0 ||
           strcmp(args, "?") == 0 || *args == '\0' || starts("SIG") || starts("LINES") ||
           starts("LOOP") || starts("BRIDGE");
}

const char *argStart(const char *s) {
    while (*s == ' ') {
        ++s;
    }
    if (*s == '=') {
        ++s;
        while (*s == ' ') {
            ++s;
        }
    }
    return s;
}

bool wordIs(const char *s, const char *word, const char **rest) {
    const char *p = s;
    for (; *word; ++p, ++word) {
        if (toupper(static_cast<unsigned char>(*p)) != toupper(static_cast<unsigned char>(*word))) {
            return false;
        }
    }
    if (!isSeparator(*p)) {
        return false;
    }
    if (rest) {
        *rest = skipSeparator(p);
    }
    return true;
}

bool parseUint(const char *s, uint32_t lo, uint32_t hi, uint32_t *out, const char **rest) {
    if (!isdigit(static_cast<unsigned char>(*s))) {
        return false;
    }
    uint64_t v = 0;
    const char *p = s;
    while (isdigit(static_cast<unsigned char>(*p))) {
        v = v * 10 + static_cast<uint64_t>(*p - '0');
        if (v > hi) {
            return false;  // also stops overflow
        }
        ++p;
    }
    if (!isSeparator(*p) || v < lo) {
        return false;
    }
    *out = static_cast<uint32_t>(v);
    if (rest) {
        *rest = skipSeparator(p);
    }
    return true;
}

bool parseOnOff(const char *s, bool *on) {
    if (wordIs(s, "ON") || wordIs(s, "1")) {
        *on = true;
    } else if (wordIs(s, "OFF") || wordIs(s, "0")) {
        *on = false;
    } else {
        return false;
    }
    return true;
}

uint8_t loopbackByte(size_t i) {
    // 167 is odd, so i * 167 visits every residue mod 256 once per 256 steps.
    return static_cast<uint8_t>((i * 167u + 0x5Au) & 0xFFu);
}

}  // namespace hwtest
