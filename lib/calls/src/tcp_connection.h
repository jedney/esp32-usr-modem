#pragma once
#include "connection.h"
#include "telnet.h"

namespace calls {
enum class SocketState : uint8_t {
    kClosed,
    kResolving,
    kPending,
    kOpen,
    kDnsError,
    kRefused,
    kFailed
};
// A reset while connect is pending is an active rejection on lwIP. This mapping
// is not used for established connections, whose reset is a carrier loss.
SocketState connectErrorState(int error);
class Socket {
public:
    virtual ~Socket() = default;
    virtual void begin(const char *host, uint16_t port) = 0;
    virtual void poll() = 0;
    virtual SocketState state() const = 0;
    virtual size_t write(const uint8_t *data, size_t len) = 0;
    virtual size_t read(uint8_t *data, size_t len) = 0;
    virtual void close() = 0;
    virtual const char *peer() const { return "unknown"; }
};
class TcpConnection : public hostport::Connection {
public:
    explicit TcpConnection(Socket &socket) : socket_(socket) {}
    void begin(bool raw);
    void holdPayload(uint32_t nowMs, uint32_t durationMs);
    size_t space() const override;
    size_t write(const uint8_t *data, size_t len) override;
    size_t read(uint8_t *buf, size_t cap) override;
    bool isOpen() const override;
    void close() override;
    void poll(uint32_t nowMs) override;

private:
    Socket &socket_;
    Telnet telnet_;
    bool raw_ = false;
    uint32_t holdSince_ = 0, holdMs_ = 0;
};
}  // namespace calls
