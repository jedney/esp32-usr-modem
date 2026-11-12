// Browser trust boundaries. No Arduino, sockets or filesystem dependencies.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "modemcfg.h"

namespace webinterface {
class Crypto {
public:
    virtual ~Crypto() = default;
    virtual bool random(uint8_t *out, size_t len) = 0;
    virtual bool md5(const std::string &text, char out[33]) = 0;
};
bool equalSecret(const std::string &a, const std::string &b);
bool allowedHost(const std::string &value, const std::string &name, const std::string &ip);
bool allowedOrigin(const std::string &value, const std::string &name, const std::string &ip);
std::string jsonString(const char *value);
std::string terminalUtf8(const uint8_t *bytes, size_t len, bool cp437);
// Strict bounded geometry envelope, distinct from terminal input.
bool terminalSize(const std::string &message, unsigned &columns, unsigned &rows);
int compareVersions(const std::string &a, const std::string &b);  // -2 = invalid
// One HTTP request per connection (WebSocket upgrade is the exception). Reject
// duplicate security headers before IDF's first-value-only header lookup.
class HeaderGate {
public:
    bool accept(const uint8_t *data, size_t len);
    bool complete() const { return complete_; }
    bool websocket() const { return websocket_; }
    const std::string &host() const { return host_; }
    const std::string &origin() const { return origin_; }
    const std::string &target() const { return target_; }
    const std::string &filtered() const { return filtered_; }
    size_t headerLength() const { return headerLength_; }

private:
    std::string head_;
    std::string host_, origin_, target_;
    std::string filtered_;
    size_t headerLength_ = 0;
    bool complete_ = false, websocket_ = false, bad_ = false;
    size_t remaining_ = 0;
};

// Result of checking an Authorization header.
//   kMissing: no usable Digest header (first request, or malformed)
//   kStale:   correct password, but the nonce expired, was replaced or was replayed;
//             the next challenge says stale=true so the browser retries silently
//   kWrong:   wrong password or response, or any Digest attempt in the second after one
//             (the lock-out; answered at once, the password is not checked)
enum class Check : uint8_t { kOk, kMissing, kStale, kWrong };

class Auth {
public:
    explicit Auth(Crypto &crypto) : crypto_(crypto) {}
    void reset();
    std::string challenge(uint32_t now, bool stale = false);
    Check check(const std::string &header, const std::string &method, const std::string &uri,
                const char *ha1, uint32_t now);
    bool verify(const std::string &header, const std::string &method, const std::string &uri,
                const char *ha1, uint32_t now) {
        return check(header, method, uri, ha1, now) == Check::kOk;
    }
    bool token(char out[33]);
    // True for kLockOutMs after a wrong response. Attempts in that time are refused
    // without extending it, so a guesser gets one try a second and the server task
    // never sleeps (SEC-02; replaces the old one-second delay).
    bool lockedOut(uint32_t now) const { return wrong_ && now - wrongAt_ < kLockOutMs; }
    static constexpr uint32_t kLockOutMs = 1000;

private:
    struct Nonce {
        std::string value;
        uint32_t at = 0, count = 0;
    };
    Crypto &crypto_;
    Nonce nonce_[8];
    unsigned next_ = 0;
    bool wrong_ = false;
    uint32_t wrongAt_ = 0;
};

class Tickets {
public:
    explicit Tickets(Auth &auth) : auth_(auth) {}
    void reset();
    std::string issue(uint32_t now);
    bool consume(const std::string &token, uint32_t now);

private:
    Auth &auth_;
    struct Ticket {
        std::string value;
        uint32_t at = 0;
    };
    Ticket ticket_[4];
    unsigned next_ = 0;
};

// A separate namespace avoids migrating Sportster profiles for a web-only flag.
class Settings {
public:
    explicit Settings(modemcfg::Store &store) : store_(store) {}
    bool begin();
    bool enabled() const { return enabled_; }
    bool setEnabled(bool on);

private:
    modemcfg::Store &store_;
    bool enabled_ = true;
};

// Bounded byte queues shared by the transport and modem owners under their mutex.
template <size_t N>
class Queue {
public:
    size_t space() const { return N - count_; }
    size_t size() const { return count_; }
    void clear() {
        for (auto &b : bytes_) {
            b = 0;
        }
        head_ = count_ = 0;
    }
    bool push(const uint8_t *data, size_t len) {
        if (len > space()) {
            return false;
        }
        for (size_t i = 0; i < len; ++i) {
            bytes_[(head_ + count_++) % N] = data[i];
        }
        return true;
    }
    size_t pop(uint8_t *data, size_t cap) {
        size_t n = count_ < cap ? count_ : cap;
        for (size_t i = 0; i < n; ++i) {
            data[i] = bytes_[head_];
            bytes_[head_] = 0;
            head_ = (head_ + 1) % N;
        }
        count_ -= n;
        return n;
    }

private:
    uint8_t bytes_[N] = {};
    size_t head_ = 0, count_ = 0;
};
}  // namespace webinterface
