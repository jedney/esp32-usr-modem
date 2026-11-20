#include "session_policy.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include "devicecfg.h"

namespace webinterface {
namespace {
int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}
bool lowerHex(const std::string &text, size_t n) {
    if (text.size() != n) {
        return false;
    }
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
// Decodes one form value into out. False if it is too long or holds a control character.
bool decode(const std::string &body, size_t begin, size_t end, char *out) {
    size_t n = 0;
    for (size_t i = begin; i < end; ++i) {
        int c = static_cast<unsigned char>(body[i]);
        if (c == '+') {
            c = ' ';
        } else if (c == '%') {
            int high = i + 2 < end ? hexValue(body[i + 1]) : -1;
            int low = i + 2 < end ? hexValue(body[i + 2]) : -1;
            if (high < 0 || low < 0) {
                return false;
            }
            c = high * 16 + low;
            i += 2;
        }
        if (c < 32 || c == 127 || n == kLoginFieldBytes) {
            return false;
        }
        out[n++] = static_cast<char>(c);
    }
    out[n] = 0;
    return true;
}
}  // namespace

bool parseLoginForm(const std::string &body, char user[kLoginFieldBytes + 1],
                    char password[kLoginFieldBytes + 1]) {
    user[0] = password[0] = 0;
    if (body.size() > kLoginFormBytes) {
        return false;
    }
    bool haveUser = false, havePassword = false, ok = true;
    size_t pos = 0;
    while (ok && pos <= body.size()) {
        size_t end = body.find('&', pos);
        if (end == std::string::npos) {
            end = body.size();
        }
        size_t equals = body.find('=', pos);
        if (equals == std::string::npos || equals > end) {
            ok = false;
            break;
        }
        const std::string key = body.substr(pos, equals - pos);
        if (key == "username" && !haveUser) {
            haveUser = ok = decode(body, equals + 1, end, user);
        } else if (key == "password" && !havePassword) {
            havePassword = ok = decode(body, equals + 1, end, password);
        } else {
            ok = false;
        }
        pos = end + 1;
    }
    ok = ok && haveUser && havePassword;
    if (!ok) {
        devicecfg::wipe(user, kLoginFieldBytes + 1);
        devicecfg::wipe(password, kLoginFieldBytes + 1);
    }
    return ok;
}

CookieResult parseSessionCookie(const std::string &header, std::string &token) {
    token.clear();
    const std::string name = kSessionCookie;
    unsigned found = 0;
    std::string value;
    size_t pos = 0;
    while (pos < header.size()) {
        size_t end = header.find(';', pos);
        if (end == std::string::npos) {
            end = header.size();
        }
        while (pos < end && header[pos] == ' ') {
            ++pos;
        }
        size_t equals = header.find('=', pos);
        if (equals != std::string::npos && equals < end &&
            header.compare(pos, equals - pos, name) == 0 && equals - pos == name.size()) {
            ++found;
            value = header.substr(equals + 1, end - equals - 1);
            while (!value.empty() && value.back() == ' ') {
                value.pop_back();
            }
        }
        pos = end + 1;
    }
    if (!found) {
        return CookieResult::kNone;
    }
    if (found > 1 || !lowerHex(value, 32)) {
        return CookieResult::kAmbiguous;
    }
    token = value;
    return CookieResult::kOk;
}

bool verifyPassword(Crypto &crypto, const char *user, const char *password, const char *otaHash) {
    if (!user || !password || !otaHash || !lowerHex(otaHash, 64)) {
        return false;
    }
    uint8_t digest[32] = {};
    bool hashed =
        crypto.sha256(reinterpret_cast<const uint8_t *>(password), strlen(password), digest);
    char hex[65] = {};
    const char *digits = "0123456789abcdef";
    for (size_t i = 0; i < sizeof digest; ++i) {
        hex[2 * i] = digits[digest[i] >> 4];
        hex[2 * i + 1] = digits[digest[i] & 15];
    }
    // Compare the hash first and the account second, so a wrong user name costs the same.
    bool correct = equalSecret(hex, otaHash);
    correct =
        hashed && correct && strcmp(user, kAccount) == 0 && devicecfg::validPassword(password);
    devicecfg::wipe(digest, sizeof digest);
    devicecfg::wipe(hex, sizeof hex);
    return correct;
}

bool LoginThrottle::begin(uint32_t now) {
    if (busy_ || (wrong_ && now - wrongAt_ < kLockOutMs)) {
        return false;
    }
    busy_ = true;
    return true;
}
void LoginThrottle::finish(bool correct, uint32_t now) {
    busy_ = false;
    if (!correct) {
        wrong_ = true;
        wrongAt_ = now;
    }
}

void Sessions::clear(Slot &s) {
    devicecfg::wipe(s.token, sizeof s.token);
    devicecfg::wipe(s.csrf, sizeof s.csrf);
    s.start = 0;
    s.serial = 0;
}
const Sessions::Slot *Sessions::slot(uint32_t serial) const {
    if (!serial) {
        return nullptr;
    }
    for (const auto &s : slots_) {
        if (s.serial == serial) {
            return &s;
        }
    }
    return nullptr;
}
Sessions::Result Sessions::create(const std::string &previous, uint64_t now, std::string &token,
                                  uint32_t &serial, uint32_t &replaced) {
    const std::string prior = previous;  // `previous` may be the same string as `token`
    token.clear();
    serial = replaced = 0;
    for (auto &s : slots_) {
        if (s.serial && !live(s, now)) {
            clear(s);
        }
    }
    Slot *target = nullptr;
    if (prior.size() == 32) {
        for (auto &s : slots_) {
            if (s.serial && equalSecret(s.token, prior)) {
                target = &s;
            }
        }
    }
    if (!target) {
        for (auto &s : slots_) {
            if (!s.serial) {
                target = &s;
                break;
            }
        }
    }
    if (!target) {
        return Result::kFull;
    }
    Slot fresh;
    if (!makeToken(crypto_, fresh.token) || !makeToken(crypto_, fresh.csrf)) {
        clear(fresh);
        return Result::kFailed;
    }
    replaced = target->serial;
    clear(*target);
    *target = fresh;
    target->start = now;
    target->serial = nextSerial_++;
    if (!nextSerial_) {
        nextSerial_ = 1;
    }
    clear(fresh);
    token = target->token;
    serial = target->serial;
    return Result::kOk;
}
uint32_t Sessions::find(const std::string &token, uint64_t now) const {
    if (token.size() != 32) {
        return 0;
    }
    uint32_t serial = 0;
    for (const auto &s : slots_) {
        if (live(s, now) && equalSecret(s.token, token)) {
            serial = s.serial;
        }
    }
    return serial;
}
bool Sessions::valid(uint32_t serial, uint64_t now) const {
    const Slot *s = slot(serial);
    return s && live(*s, now);
}
bool Sessions::csrfMatches(uint32_t serial, const std::string &csrf, uint64_t now) const {
    const Slot *s = slot(serial);
    return s && live(*s, now) && csrf.size() == 32 && equalSecret(s->csrf, csrf);
}
std::string Sessions::csrf(uint32_t serial) const {
    const Slot *s = slot(serial);
    return s ? s->csrf : "";
}
uint32_t Sessions::remainingSeconds(uint32_t serial, uint64_t now) const {
    const Slot *s = slot(serial);
    if (!s || !live(*s, now)) {
        return 0;
    }
    return static_cast<uint32_t>((kSessionLifetimeMs - (now - s->start) + 999) / 1000);
}
bool Sessions::revoke(uint32_t serial) {
    for (auto &s : slots_) {
        if (serial && s.serial == serial) {
            clear(s);
            return true;
        }
    }
    return false;
}
void Sessions::revokeAll() {
    for (auto &s : slots_) {
        clear(s);
    }
}
size_t Sessions::expire(uint64_t now, uint32_t out[kSessionSlots]) {
    size_t n = 0;
    for (auto &s : slots_) {
        if (s.serial && !live(s, now)) {
            out[n++] = s.serial;
            clear(s);
        }
    }
    return n;
}
size_t Sessions::count() const {
    return static_cast<size_t>(std::count_if(std::begin(slots_), std::end(slots_),
                                             [](const Slot &s) { return s.serial != 0; }));
}

void formatLogin(LoginKind kind, const char *peer, char *out, size_t cap) {
    if (!cap) {
        return;
    }
    const char *from = peer && *peer ? peer : "unknown";
    switch (kind) {
        case LoginKind::kWeb:
            snprintf(out, cap, "Login: web %s from %s", kAccount, from);
            break;
        case LoginKind::kNetConsole:
            snprintf(out, cap, "Login: network console from %s", from);
            break;
        case LoginKind::kOta:
            snprintf(out, cap, "Login: OTA upload");
            break;
    }
}
void formatPeer(const uint8_t *addr, size_t len, char out[16]) {
    static const uint8_t kMappedPrefix[12] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff};
    out[0] = 0;
    if (addr && len == 16 && memcmp(addr, kMappedPrefix, sizeof kMappedPrefix) == 0) {
        addr += sizeof kMappedPrefix;
        len = 4;
    }
    if (addr && len == 4) {
        snprintf(out, 16, "%u.%u.%u.%u", addr[0], addr[1], addr[2], addr[3]);
    }
}
void LoginEvents::push(LoginKind kind, const char *peer) {
    if (count_ == kCapacity) {
        head_ = (head_ + 1) % kCapacity;
        --count_;
        ++dropped_;
    }
    Event &e = events_[(head_ + count_++) % kCapacity];
    e.kind = kind;
    snprintf(e.peer, sizeof e.peer, "%s", peer ? peer : "");
}
bool LoginEvents::take(char *out, size_t cap) {
    if (dropped_) {
        snprintf(out, cap, "Login: %u earlier login events not shown", dropped_);
        dropped_ = 0;
        return true;
    }
    if (!count_) {
        return false;
    }
    const Event &e = events_[head_];
    formatLogin(e.kind, e.peer, out, cap);
    head_ = (head_ + 1) % kCapacity;
    --count_;
    return true;
}
}  // namespace webinterface
