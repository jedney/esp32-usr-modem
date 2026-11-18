// Browser login: the form, the session cookie, the RAM session table, the password throttle
// and the console login events. No Arduino, sockets or filesystem dependencies.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "web_policy.h"

namespace webinterface {
constexpr uint64_t kSessionLifetimeMs = 12ull * 60 * 60 * 1000;  // from login, never renewed
constexpr unsigned kSessionSlots = 8;
constexpr size_t kLoginFormBytes = 256;  // largest accepted login body
constexpr size_t kLoginFieldBytes = 32;  // username and password, after decoding
constexpr const char *kAccount = "admin";
constexpr const char *kSessionCookie = "usr_session";

// Reads an application/x-www-form-urlencoded body holding exactly one "username" and one
// "password" field and nothing else. '+' and %XX are decoded; control characters, NUL and
// values longer than kLoginFieldBytes are refused. Outputs are terminated even on failure.
bool parseLoginForm(const std::string &body, char user[kLoginFieldBytes + 1],
                    char password[kLoginFieldBytes + 1]);

// kAmbiguous: the cookie appears more than once, or its value is not 32 lower-case hex digits.
// Such a request never counts as logged in.
enum class CookieResult : uint8_t { kNone, kOk, kAmbiguous };
CookieResult parseSessionCookie(const std::string &header, std::string &token);

// True if user is "admin" and the SHA-256 of password, in lower-case hex, equals otaHash (the
// stored device-password hash). The comparison takes the same time for any wrong password.
bool verifyPassword(Crypto &crypto, const char *user, const char *password, const char *otaHash);

// One password check at a time, and none in the second after a wrong password. Refused
// attempts never extend the wait, and nothing ever sleeps.
class LoginThrottle {
public:
    static constexpr uint32_t kLockOutMs = 1000;
    bool begin(uint32_t now);
    void finish(bool correct, uint32_t now);
    void reset() { busy_ = wrong_ = false; }

private:
    bool busy_ = false, wrong_ = false;
    uint32_t wrongAt_ = 0;
};

// Logged-in browsers, kept only in RAM. Time is monotonic uptime in milliseconds. A session
// ends exactly kSessionLifetimeMs after it began, however it is used. Each session has a serial
// number that is never reused, so tickets and sockets bound to a serial die with their session.
class Sessions {
public:
    enum class Result : uint8_t { kOk, kFull, kFailed };
    explicit Sessions(Crypto &crypto) : crypto_(crypto) {}
    // Ends expired sessions, then ends the submitting browser's own session (`previous`, its
    // cookie token, if still valid) and starts a new one. kFull if every other slot holds a
    // valid session: nobody else's session is ever ended to make room.
    Result create(const std::string &previous, uint64_t now, std::string &token, uint32_t &serial,
                  uint32_t &replaced);
    uint32_t find(const std::string &token, uint64_t now) const;  // serial, or 0
    bool valid(uint32_t serial, uint64_t now) const;
    bool csrfMatches(uint32_t serial, const std::string &csrf, uint64_t now) const;
    std::string csrf(uint32_t serial) const;
    uint32_t remainingSeconds(uint32_t serial, uint64_t now) const;  // rounded up
    bool revoke(uint32_t serial);
    void revokeAll();
    // Ends every session whose time is over and writes their serials to out.
    size_t expire(uint64_t now, uint32_t out[kSessionSlots]);
    size_t count() const;

private:
    struct Slot {
        char token[33] = {};
        char csrf[33] = {};
        uint64_t start = 0;
        uint32_t serial = 0;
    };
    static bool live(const Slot &s, uint64_t now) {
        return s.serial && now >= s.start && now - s.start < kSessionLifetimeMs;
    }
    const Slot *slot(uint32_t serial) const;
    static void clear(Slot &s);
    Crypto &crypto_;
    Slot slots_[kSessionSlots];
    uint32_t nextSerial_ = 1;
};

// Successful logins waiting for the console. Bounded: when full, the oldest event is dropped
// and the next line taken says how many were lost.
enum class LoginKind : uint8_t { kWeb, kNetConsole, kOta };
void formatLogin(LoginKind kind, const char *peer, char *out, size_t cap);
// A client's address as "a.b.c.d": `addr` is 4 bytes (IPv4) or 16 (IPv6, as the web
// server's socket reports IPv4 clients: ::ffff:a.b.c.d). Anything else gives "" ("unknown"
// in the login line); the modem does not use IPv6 on its network.
void formatPeer(const uint8_t *addr, size_t len, char out[16]);
class LoginEvents {
public:
    static constexpr unsigned kCapacity = 8;
    void push(LoginKind kind, const char *peer);
    bool take(char *out, size_t cap);
    bool empty() const { return !count_ && !dropped_; }

private:
    struct Event {
        LoginKind kind = LoginKind::kWeb;
        char peer[16] = {};
    };
    Event events_[kCapacity];
    unsigned head_ = 0, count_ = 0, dropped_ = 0;
};
}  // namespace webinterface
