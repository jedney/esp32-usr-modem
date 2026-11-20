#pragma once
#include <freertos/FreeRTOS.h>
#include <lwip/ip_addr.h>
#include <atomic>
#include "tcp_connection.h"

namespace hw {
// Nonblocking lwIP socket and asynchronous DNS. The object outlives DNS requests.
class TcpSocket : public calls::Socket {
public:
    // A peer that vanishes without a FIN or RST (cable pulled, router crash, or a
    // lost RST) must not hold the connection for ever. Calls: idle 60 s, then 6 probes
    // 10 s apart, so a dead peer ends the call (NO CARRIER) after about 2 minutes. lwIP
    // counts these in seconds. Applies to connections opened or adopted after the change.
    struct KeepAlive {
        int idleSeconds = 60;
        int intervalSeconds = 10;
        int count = 6;
    };
    void setKeepAlive(const KeepAlive &keep) { keepAlive_ = keep; }
    void begin(const char *host, uint16_t port) override;
    void resolve(const char *host) override;
    const char *address() const override { return resolved_; }
    void poll() override;
    calls::SocketState state() const override { return state_; }
    size_t write(const uint8_t *data, size_t len) override;
    size_t read(uint8_t *data, size_t len) override;
    void close() override;
    bool adopt(int fd);  // takes ownership, including on failure
    const char *peer() const override { return peer_; }

private:
    struct Request;
    static void resolved(const char *name, const ip_addr_t *address, void *context);
    void lookup(const char *host);
    void found(uint32_t address);
    void connectAddress(uint32_t address);
    int fd_ = -1;
    KeepAlive keepAlive_;
    bool resolveOnly_ = false;
    char peer_[16] = {}, resolved_[16] = {};
    uint16_t port_ = 0;
    calls::SocketState state_ = calls::SocketState::kClosed;
    std::atomic<uint32_t> generation_{0}, address_{0};
    std::atomic<unsigned> dnsState_{0}, pending_{0};
    portMUX_TYPE dnsLock_ = portMUX_INITIALIZER_UNLOCKED;
};
}  // namespace hw
