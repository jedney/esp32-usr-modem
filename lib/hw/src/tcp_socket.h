#pragma once
#include <freertos/FreeRTOS.h>
#include <lwip/ip_addr.h>
#include <atomic>
#include "tcp_connection.h"

namespace hw {
// Nonblocking lwIP socket and asynchronous DNS. The object outlives DNS requests.
class TcpSocket : public calls::Socket {
public:
    void begin(const char *host, uint16_t port) override;
    void poll() override;
    calls::SocketState state() const override { return state_; }
    size_t write(const uint8_t *data, size_t len) override;
    size_t read(uint8_t *data, size_t len) override;
    void close() override;
    bool adopt(int fd);  // takes ownership, including on failure

private:
    struct Request;
    static void resolved(const char *name, const ip_addr_t *address, void *context);
    void connectAddress(uint32_t address);
    int fd_ = -1;
    uint16_t port_ = 0;
    calls::SocketState state_ = calls::SocketState::kClosed;
    std::atomic<uint32_t> generation_{0}, address_{0};
    std::atomic<unsigned> dnsState_{0}, pending_{0};
    portMUX_TYPE dnsLock_ = portMUX_INITIALIZER_UNLOCKED;
};
}  // namespace hw
