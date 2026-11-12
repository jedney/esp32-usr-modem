// Saved Wi-Fi and clock settings: see net_settings.h.
#include "net_settings.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#include "net_tz.h"

namespace netcfg {

namespace {

constexpr uint8_t kMagic[4] = {'U', 'S', 'R', 'N'};
constexpr uint8_t kFlagEnabled = 0x01;
constexpr uint8_t kFlagDhcp = 0x02;
constexpr size_t kSsidField = 32;
constexpr size_t kPassField = 64;
constexpr size_t kHostField = 32;
constexpr size_t kNtpField = 64;
constexpr size_t kTzField = 64;

void putU32(uint8_t *p, uint32_t v) {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
    p[2] = static_cast<uint8_t>(v >> 16);
    p[3] = static_cast<uint8_t>(v >> 24);
}

uint32_t getU32(const uint8_t *p) {
    return static_cast<uint32_t>(p[0]) | static_cast<uint32_t>(p[1]) << 8 |
           static_cast<uint32_t>(p[2]) << 16 | static_cast<uint32_t>(p[3]) << 24;
}

// Fixed width, zero-padded. A field may be full (no terminator), e.g. a 32-byte SSID.
uint8_t *putText(uint8_t *p, const char *s, size_t field) {
    memset(p, 0, field);
    memcpy(p, s, strnlen(s, field));
    return p + field;
}

// Reads up to `maxLen` characters of a field into `out` (which holds maxLen + 1).
const uint8_t *getText(const uint8_t *p, size_t field, char *out, size_t maxLen) {
    size_t n = strnlen(reinterpret_cast<const char *>(p), field);
    if (n > maxLen) {
        n = maxLen;
    }
    memcpy(out, p, n);
    out[n] = '\0';
    return p + field;
}

void copy(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src);
}

bool isHex(char c) {
    return isxdigit(static_cast<unsigned char>(c)) != 0;
}

}  // namespace

Settings defaults() {
    Settings s = {};
    s.enabled = true;
    s.dhcp = true;
    copy(s.host, sizeof s.host, kDefaultHost);
    copy(s.ntp, sizeof s.ntp, kDefaultNtp);
    copy(s.tz, sizeof s.tz, kDefaultTz);
    return s;
}

void serialize(const Settings &s, uint8_t *out) {
    uint8_t *p = out;
    memcpy(p, kMagic, 4);
    p += 4;
    *p++ = kLayoutVersion;
    *p++ = static_cast<uint8_t>((s.enabled ? kFlagEnabled : 0) | (s.dhcp ? kFlagDhcp : 0));
    p = putText(p, s.ssid, kSsidField);
    p = putText(p, s.pass, kPassField);
    putU32(p, s.ip);
    putU32(p + 4, s.mask);
    putU32(p + 8, s.gateway);
    putU32(p + 12, s.dns);
    p += 16;
    p = putText(p, s.host, kHostField);
    p = putText(p, s.ntp, kNtpField);
    p = putText(p, s.tz, kTzField);
    putU32(p, modemcfg::crc32(out, static_cast<size_t>(p - out)));
}

LoadResult deserialize(const uint8_t *in, size_t len, Settings *out) {
    *out = defaults();
    if (len == 0) {
        return LoadResult::kEmpty;
    }
    if (len != kBlobSize || memcmp(in, kMagic, 4) != 0 || in[4] != kLayoutVersion ||
        modemcfg::crc32(in, kBlobSize - 4) != getU32(in + kBlobSize - 4)) {
        return LoadResult::kDamaged;
    }
    Settings s = {};
    const uint8_t *p = in + 5;
    s.enabled = (*p & kFlagEnabled) != 0;
    s.dhcp = (*p & kFlagDhcp) != 0;
    ++p;
    p = getText(p, kSsidField, s.ssid, kSsidMax);
    p = getText(p, kPassField, s.pass, kPassMax);
    s.ip = getU32(p);
    s.mask = getU32(p + 4);
    s.gateway = getU32(p + 8);
    s.dns = getU32(p + 12);
    p += 16;
    p = getText(p, kHostField, s.host, kHostMax);
    p = getText(p, kNtpField, s.ntp, kNtpMax);
    getText(p, kTzField, s.tz, kTzMax);
    // Values a damaged blob could hold that would stop Wi-Fi or the clock working.
    char host[kHostMax + 1];
    tz::Rule rule;
    const bool ok = (s.ssid[0] == '\0' || validSsid(s.ssid)) && validPassword(s.pass) &&
                    normalizeHost(s.host, host, sizeof host) && validServer(s.ntp) &&
                    tz::resolve(s.tz, &rule, nullptr, 0) &&
                    (s.dhcp || validStatic(s.ip, s.mask, s.gateway));
    if (ok) {
        *out = s;
    }
    wipe(s.pass, sizeof s.pass);
    return ok ? LoadResult::kSaved : LoadResult::kDamaged;
}

LoadResult load(modemcfg::Store &store, Settings *out) {
    uint8_t buf[kBlobSize + 1];
    size_t n = store.load(buf, sizeof buf);
    LoadResult r = deserialize(buf, n, out);
    memset(buf, 0, sizeof buf);
    return r;
}

bool save(modemcfg::Store &store, const Settings &s) {
    uint8_t buf[kBlobSize];
    serialize(s, buf);
    bool ok = store.save(buf, sizeof buf);
    memset(buf, 0, sizeof buf);
    return ok;
}

const char *loadResultText(LoadResult r) {
    switch (r) {
        case LoadResult::kSaved:
            return "saved settings";
        case LoadResult::kEmpty:
            return "defaults (nothing saved yet)";
        case LoadResult::kDamaged:
        default:
            return "saved settings damaged, using defaults";
    }
}

// --- Checks ---------------------------------------------------------------------------------

bool validSsid(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > kSsidMax) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < ' ' || c == 0x7F) {
            return false;
        }
    }
    return true;
}

bool validPassword(const char *s) {
    size_t n = strlen(s);
    if (n == 0) {
        return true;  // open network
    }
    if (n == 64) {
        for (size_t i = 0; i < n; ++i) {
            if (!isHex(s[i])) {
                return false;
            }
        }
        return true;
    }
    if (n < 8 || n > 63) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        // A WPA2 passphrase is 8-63 bytes. Anything but control bytes is allowed, so UTF-8
        // text works (5.5-15).
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < ' ' || c == 0x7F) {
            return false;
        }
    }
    return true;
}

bool normalizeHost(const char *in, char *out, size_t cap) {
    size_t n = strlen(in);
    static const char kLocal[] = ".local";
    const size_t localLen = sizeof kLocal - 1;
    if (n > localLen) {
        bool local = true;
        for (size_t i = 0; i < localLen; ++i) {
            if (tolower(static_cast<unsigned char>(in[n - localLen + i])) != kLocal[i]) {
                local = false;
            }
        }
        if (local) {
            n -= localLen;
        }
    }
    if (n == 0 || n > kHostMax || n >= cap || in[0] == '-' || in[n - 1] == '-') {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        char c = static_cast<char>(tolower(static_cast<unsigned char>(in[i])));
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) {
            return false;
        }
        out[i] = c;
    }
    out[n] = '\0';
    return true;
}

bool validServer(const char *s) {
    size_t n = strlen(s);
    if (n == 0 || n > kNtpMax || s[0] == '.' || s[0] == '-' || s[n - 1] == '.') {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        char c = s[i];
        if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '.')) {
            return false;
        }
        if (c == '.' && i > 0 && s[i - 1] == '.') {
            return false;  // an empty label (5.5-15)
        }
    }
    return true;
}

bool parseIp(const char *s, uint32_t *out, const char **rest) {
    uint32_t ip = 0;
    for (int part = 0; part < 4; ++part) {
        if (part > 0) {
            if (*s != '.') {
                return false;
            }
            ++s;
        }
        if (*s < '0' || *s > '9') {
            return false;
        }
        unsigned v = 0;
        int digits = 0;
        while (*s >= '0' && *s <= '9') {
            v = v * 10 + static_cast<unsigned>(*s - '0');
            if (++digits > 3 || v > 255) {
                return false;
            }
            ++s;
        }
        ip = ip << 8 | v;
    }
    if (*s != '\0' && *s != ',' && *s != ' ') {
        return false;
    }
    while (*s == ' ') {
        ++s;
    }
    if (*s == ',') {
        ++s;
        while (*s == ' ') {
            ++s;
        }
    }
    *out = ip;
    if (rest != nullptr) {
        *rest = s;
    }
    return true;
}

void formatIp(uint32_t ip, char *buf, size_t cap) {
    snprintf(buf, cap, "%u.%u.%u.%u", static_cast<unsigned>(ip >> 24),
             static_cast<unsigned>((ip >> 16) & 0xFF), static_cast<unsigned>((ip >> 8) & 0xFF),
             static_cast<unsigned>(ip & 0xFF));
}

bool validStatic(uint32_t ip, uint32_t mask, uint32_t gateway) {
    if (mask == 0 || mask == 0xFFFFFFFFu) {
        return false;
    }
    const uint32_t inv = ~mask;
    if ((inv & (inv + 1)) != 0) {
        return false;  // not contiguous: the ones must come first
    }
    const uint32_t host = ip & inv;
    const uint32_t gwHost = gateway & inv;
    if (host == 0 || host == inv || gwHost == 0 || gwHost == inv) {
        return false;  // network or broadcast address
    }
    if ((ip & mask) != (gateway & mask) || ip == gateway) {
        return false;
    }
    const uint32_t first = ip >> 24;
    return first != 0 && first != 127 && first < 224;
}

void wipe(char *buf, size_t len) {
    volatile char *p = buf;  // volatile: the compiler must not skip the wipe
    for (size_t i = 0; i < len; ++i) {
        p[i] = '\0';
    }
}

}  // namespace netcfg
