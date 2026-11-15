// Browser trust boundaries. No Arduino, sockets or filesystem dependencies.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include "modemcfg.h"

namespace webinterface {
constexpr size_t kTerminalInputBytes = 2048;
constexpr size_t kTerminalOutputBytes = 16384;
constexpr size_t kTerminalFrameBytes = 1024 * 1024;
class Crypto {
public:
    virtual ~Crypto() = default;
    virtual bool random(uint8_t *out, size_t len) = 0;
    virtual bool sha256(const uint8_t *data, size_t len, uint8_t out[32]) = 0;
};
// 16 random bytes as 32 lower-case hex digits: session ids, CSRF tokens and tickets.
bool makeToken(Crypto &crypto, char out[33]);
bool equalSecret(const std::string &a, const std::string &b);
bool allowedHost(const std::string &value, const std::string &name, const std::string &ip);
bool allowedOrigin(const std::string &value, const std::string &name, const std::string &ip);
std::string jsonString(const char *value);
std::string terminalUtf8(const uint8_t *bytes, size_t len, bool cp437);
// Strict bounded geometry envelope, distinct from terminal input.
bool terminalSize(const std::string &message, unsigned &columns, unsigned &rows);
int compareVersions(const std::string &a, const std::string &b);  // -2 = invalid
// Opening a socket does not own the terminal. Only an explicit open may claim it.
class TerminalOwner {
public:
    bool open(uint32_t id, bool available) {
        if (!id || (!owns(id) && (owner_ || !available)))
            return false;
        owner_ = id;
        return true;
    }
    bool owns(uint32_t id) const { return id && id == owner_; }
    void close(uint32_t id) {
        if (owns(id)) {
            owner_ = 0;
            hangup_ = false;
        }
    }
    bool requestHangup(uint32_t id) {
        if (!owns(id))
            return false;
        hangup_ = true;
        return true;
    }
    bool takeHangup(uint32_t id) {
        if (!owns(id) || !hangup_)
            return false;
        hangup_ = false;
        return true;
    }

private:
    uint32_t owner_ = 0;
    bool hangup_ = false;
};
// Largest request header (request line and fields) the web server takes. The HTTP server's
// own header limit (httpd_config_t::max_req_hdr_len) is set to the same value.
constexpr size_t kMaxHeader = 4096;
// Checks the raw bytes of a connection before the HTTP server parses them; the bytes pass
// through unchanged. One HTTP request per connection (a WebSocket upgrade is the exception:
// its frames follow). Duplicate security headers (Host, Origin, Cookie, Content-Length,
// Upgrade, X-*) are refused because the server's header lookup returns only the first value;
// Transfer-Encoding and a body longer than its Content-Length are refused too.
class HeaderGate {
public:
    bool accept(const uint8_t *data, size_t len);
    bool complete() const { return complete_; }
    bool websocket() const { return websocket_; }

private:
    std::string head_;
    bool complete_ = false, websocket_ = false, bad_ = false;
    size_t remaining_ = 0;
};

// Single-use WebSocket tickets, each bound to the login session that asked for it.
class Tickets {
public:
    explicit Tickets(Crypto &crypto) : crypto_(crypto) {}
    void reset();
    std::string issue(uint32_t session, uint32_t now);
    uint32_t consume(const std::string &token, uint32_t now);  // its session, or 0
    void revoke(uint32_t session);

private:
    Crypto &crypto_;
    struct Ticket {
        std::string value;
        uint32_t at = 0, session = 0;
    };
    Ticket ticket_[4];
    unsigned next_ = 0;
};

// The web-only flag, saved apart from the Sportster profiles.
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
    size_t append(const uint8_t *data, size_t len) {
        size_t n = len < space() ? len : space();
        push(data, n);
        return n;
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
