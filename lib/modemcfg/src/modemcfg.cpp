// Modem settings: see modemcfg.h.
#include "modemcfg.h"

#include <cctype>
#include <cstring>

namespace modemcfg {

namespace {

constexpr uint64_t range(unsigned lo, unsigned hi) {
    return ((hi >= 63 ? UINT64_MAX : ((UINT64_C(1) << (hi + 1)) - 1)) & ~((UINT64_C(1) << lo) - 1));
}
constexpr uint64_t bit(unsigned n) {
    return UINT64_C(1) << n;
}

// Values each setting accepts (Sportster technical reference; reserved values refused).
const SettingInfo kInfo[kSettingCount] = {
    {"B", range(0, 1)},
    {"E", range(0, 1)},
    {"F", range(0, 1)},
    {"L", range(0, 3)},
    {"M", range(0, 3)},
    {"Q", range(0, 2)},
    {"V", range(0, 1)},
    {"X", range(0, 4)},
    {"T", range(0, 1)},
    {"&A", range(0, 3)},
    {"&B", range(0, 2)},
    {"&C", range(0, 1)},
    {"&D", bit(0) | bit(2) | bit(3)},
    {"&G", range(0, 2)},
    {"&H", range(0, 3)},
    {"&I", range(0, 2) | bit(5)},
    {"&K", range(0, 3)},
    {"&M", bit(0) | bit(4) | bit(5)},
    {"&N", range(0, kMaxLineSpeedIndex)},
    {"&P", range(0, 1)},
    {"&R", range(1, 2)},
    {"&S", bit(0)},
    {"&T", range(4, 5)},
    {"&U", range(0, kMaxLineSpeedIndex)},
    {"&Y", range(0, 3)},
};

// Factory S-register values (Sportster 33600; S25 and S36 as a real 33600 reports them).
void factorySRegs(uint8_t *s) {
    memset(s, 0, kSRegCount);
    s[2] = 43;   // escape character '+'
    s[3] = 13;   // carriage return
    s[4] = 10;   // line feed
    s[5] = 8;    // backspace
    s[6] = 2;    // dial wait, s
    s[7] = 60;   // carrier wait, s
    s[8] = 2;    // comma pause, s
    s[9] = 6;    // carrier detect time, 0.1 s
    s[10] = 14;  // carrier loss wait, 0.1 s
    s[11] = 70;  // DTMF tone duration, ms
    s[12] = 50;  // escape guard time, 1/50 s (1 s, brief 6.2)
    s[21] = 10;  // break length, 10 ms
    s[22] = 17;  // XON
    s[23] = 19;  // XOFF
    s[25] = 5;   // DTR recognition time, 0.01 s
    s[28] = 8;   // V.21/V.23 fallback delay
    s[29] = 20;  // V.21 answer fallback timer
    s[31] = 128;
    s[32] = 2;
    s[36] = 14;
}

void putU32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t getU32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

const uint8_t kMagic[4] = {'U', 'S', 'R', 'M'};

}  // namespace

const SettingInfo &settingInfo(Setting s) {
    return kInfo[s < kSettingCount ? s : 0];
}

int findSetting(char prefix, char letter) {
    char want[3] = {};
    size_t n = 0;
    if (prefix != 0) {
        want[n++] = prefix;
    }
    want[n] = static_cast<char>(toupper(static_cast<unsigned char>(letter)));
    for (int i = 0; i < kSettingCount; ++i) {
        if (i == kTone) {
            continue;  // set by the P and T commands
        }
        if (strcmp(kInfo[i].name, want) == 0) {
            return i;
        }
    }
    return -1;
}

bool isHostRate(uint32_t baud) {
    for (uint32_t r : kHostRates) {
        if (r == baud) {
            return true;
        }
    }
    return false;
}

bool settingAllows(Setting s, unsigned value) {
    return s < kSettingCount && value < 64 && (kInfo[s].allowed & (UINT64_C(1) << value)) != 0;
}

const uint8_t kSRegsCurrent[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
                                 11, 12, 13, 15, 16, 18, 19, 21, 22, 23, 25,
                                 27, 28, 29, 30, 31, 32, 33, 34, 36, 38, 0xFF};
const uint8_t kSRegsStored[] = {0,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 15, 19,
                                21, 22, 23, 25, 27, 28, 29, 30, 31, 32, 33, 34, 36, 38, 0xFF};

bool operator==(const Profile &a, const Profile &b) {
    return memcmp(a.v, b.v, sizeof a.v) == 0 && memcmp(a.s, b.s, sizeof a.s) == 0 &&
           a.baud == b.baud && a.lineExplicit == b.lineExplicit;
}

bool validModel(uint32_t speed) {
    return speed == 14400 || speed == 28800 || speed == 33600 || speed == 56000;
}

const char *modelName(uint32_t speed) {
    switch (speed) {
        case 28800:
            return "Sportster 28.8k Series";
        case 33600:
            return "Sportster 33.6k Series";
        case 56000:
            return "Sportster 56k Series";
        default:
            return "Sportster 14.4k Series";
    }
}

Profile factoryProfile(unsigned n) {
    Profile p = {};
    p.v[kB] = 0;
    p.v[kE] = 1;
    p.v[kF] = 1;
    p.v[kL] = 2;
    p.v[kM] = 1;
    p.v[kQ] = 0;
    p.v[kV] = 1;
    p.v[kX] = 4;     // all result codes (Sportster default; Jon, v0.4)
    p.v[kTone] = 1;  // tone dialling (the Sportster ships with pulse)
    p.v[kAmpA] = 0;  // plain CONNECT result (Q34)
    p.v[kAmpB] = 1;  // speed stays as detected from "AT" (Sportster default; Jon, v0.5)
    p.v[kAmpC] = 1;  // brief 6.2, Q33
    p.v[kAmpD] = 2;  // brief 6.2, Q33
    p.v[kAmpG] = 0;
    p.v[kAmpH] = 1;  // brief 6.2, Q33
    p.v[kAmpI] = 0;
    p.v[kAmpK] = 1;
    p.v[kAmpM] = 4;
    p.v[kAmpN] = 8;  // 14400 default; higher speeds remain selectable (Jon, v0.8)
    p.v[kAmpP] = 1;  // UK pulse ratio (brief 6.4: UK tones)
    p.v[kAmpR] = 2;  // brief 6.2, Q33
    p.v[kAmpS] = 0;  // fixed (brief 6.2)
    p.v[kAmpT] = 5;
    p.v[kAmpU] = 0;
    p.v[kAmpY] = 1;
    factorySRegs(p.s);
    p.baud = kFactoryBaud;

    if (n == 0) {  // no flow control, basic result codes
        p.v[kX] = 1;
        p.v[kAmpA] = 1;
        p.v[kAmpH] = 0;
        p.v[kAmpR] = 1;
    } else if (n == 2) {  // software flow control
        p.v[kAmpH] = 2;
        p.v[kAmpR] = 1;
        p.v[kAmpI] = 2;
    }
    return p;
}

Nvram factoryNvram() {
    Nvram n = {};
    n.tmpl[0] = factoryProfile(1);
    n.tmpl[1] = factoryProfile(2);
    n.y = 0;
    return n;
}

Profile powerOnProfile(const Nvram &n) {
    if (n.y < kTemplates) {
        return n.tmpl[n.y];
    }
    return factoryProfile(n.y - kTemplates);
}

uint32_t crc32(const uint8_t *data, size_t len) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

void serialize(const Nvram &n, uint8_t *out) {
    uint8_t *p = out;
    memcpy(p, kMagic, 4);
    p += 4;
    *p++ = kLayoutVersion;
    *p++ = n.y;
    for (const Profile &t : n.tmpl) {
        memcpy(p, t.v, kSettingCount);
        p += kSettingCount;
        memcpy(p, t.s, kSRegCount);
        p += kSRegCount;
        putU32(p, t.baud);
        p += 4;
    }
    for (const auto &phone : n.phone) {
        // Fixed width, zero-padded, so the blob is always the same size.
        memset(p, 0, kPhoneLen);
        memcpy(p, phone, strnlen(phone, kPhoneLen));
        p += kPhoneLen;
    }
    putU32(p, n.hostBaud);
    p += 4;
    for (const Profile &t : n.tmpl) {
        *p++ = t.lineExplicit ? 1 : 0;
    }
    putU32(p, n.model);
    p += 4;
    *p++ = n.throttle ? 1 : 0;
    *p++ = n.answerConsole ? 1 : 0;
    putU32(p, n.ringDelayMs);
    p += 4;
    putU32(p, n.connectDelayMs);
    p += 4;
    putU32(p, crc32(out, static_cast<size_t>(p - out)));
}

LoadResult deserialize(const uint8_t *in, size_t len, Nvram *out) {
    *out = factoryNvram();
    if (len == 0) {
        return LoadResult::kEmpty;
    }
    if (len < 5) {
        return LoadResult::kBadSize;
    }
    if (memcmp(in, kMagic, 4) != 0) {
        return LoadResult::kBadMagic;
    }
    const uint8_t version = in[4];
    if (version < 1 || version > kLayoutVersion) {
        return LoadResult::kBadVersion;
    }
    const size_t size = version == 1   ? kBlobSizeV1
                        : version == 2 ? kBlobSizeV2
                        : version == 3 ? kBlobSizeV3
                        : version == 4 ? kBlobSizeV4
                                       : kBlobSize;
    if (len != size) {
        return LoadResult::kBadSize;
    }
    if (crc32(in, size - 4) != getU32(in + size - 4)) {
        return LoadResult::kBadCrc;
    }
    Nvram n = {};
    const uint8_t *p = in + 5;
    n.y = *p++;
    if (n.y > kTemplates + 2) {
        return LoadResult::kBadValue;
    }
    for (Profile &t : n.tmpl) {
        t.lineExplicit = version < 3;  // preserve old saved speed; its provenance is unknown
        memcpy(t.v, p, kSettingCount);
        p += kSettingCount;
        memcpy(t.s, p, kSRegCount);
        t.s[1] = 0;  // S1 is a live ring counter, never restored from a profile
        p += kSRegCount;
        t.baud = getU32(p);
        p += 4;
        for (int i = 0; i < kSettingCount; ++i) {
            if (!settingAllows(static_cast<Setting>(i), t.v[i])) {
                return LoadResult::kBadValue;
            }
        }
    }
    for (auto &phone : n.phone) {
        memcpy(phone, p, kPhoneLen);
        phone[kPhoneLen] = '\0';
        p += kPhoneLen;
    }
    if (version == 1) {
        // v0.4 stored &B with the brief's first meaning (0 = detect). Since v0.5 &B has
        // the Sportster meaning, and the speed is always detected unless AT*BAUD is set.
        for (Profile &t : n.tmpl) {
            t.v[kAmpB] = 1;
        }
        n.hostBaud = 0;
        *out = n;
        return LoadResult::kMigrated;
    }
    n.hostBaud = getU32(p);
    p += 4;
    if (n.hostBaud != 0 && !isHostRate(n.hostBaud)) {
        return LoadResult::kBadValue;
    }
    if (version >= 3) {
        for (Profile &t : n.tmpl) {
            if (*p > 1) {
                return LoadResult::kBadValue;
            }
            t.lineExplicit = *p++ != 0;
        }
        n.model = getU32(p);
        p += 4;
        if (!validModel(n.model) || *p > 1) {
            return LoadResult::kBadValue;
        }
        n.throttle = *p++ != 0;
    }
    if (version >= 4) {
        if (*p > 1) {
            return LoadResult::kBadValue;
        }
        n.answerConsole = *p++ != 0;
    }
    if (version >= 5) {
        n.ringDelayMs = getU32(p);
        p += 4;
        n.connectDelayMs = getU32(p);
        if (n.ringDelayMs > kMaxCallDelayMs || n.connectDelayMs > kMaxCallDelayMs) {
            return LoadResult::kBadValue;
        }
    }
    *out = n;
    return version < kLayoutVersion ? LoadResult::kMigrated : LoadResult::kOk;
}

const char *loadResultText(LoadResult r) {
    switch (r) {
        case LoadResult::kOk:
            return "saved settings";
        case LoadResult::kMigrated:
            return "saved settings (converted from an older firmware's format)";
        case LoadResult::kEmpty:
            return "factory settings (nothing saved yet)";
        case LoadResult::kBadVersion:
            return "factory settings (saved settings are from another firmware version)";
        case LoadResult::kBadSize:
        case LoadResult::kBadMagic:
        case LoadResult::kBadCrc:
        case LoadResult::kBadValue:
        default:
            return "factory settings (saved settings were damaged)";
    }
}

LoadResult loadNvram(Store &store, Nvram *out) {
    uint8_t buf[kBlobSize + 1];
    size_t len = store.load(buf, sizeof buf);
    return deserialize(buf, len, out);
}

bool saveNvram(Store &store, const Nvram &n) {
    uint8_t buf[kBlobSize];
    serialize(n, buf);
    return store.save(buf, sizeof buf);
}

}  // namespace modemcfg
