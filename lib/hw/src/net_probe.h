#pragma once
#include <freertos/FreeRTOS.h>
#include <lwip/ip_addr.h>
#include <ping/ping_sock.h>
#include <atomic>
namespace hw {
class NetProbe {
public:
    bool start(const char *host, bool ping, uint32_t now);
    void poll(uint32_t now, bool network);
    void cancel();
    bool done() const { return done_; }
    bool ok() const { return ok_; }
    uint32_t address() const { return address_.load(); }  // network byte order
    unsigned replies() const { return replies_.load(); }

private:
    struct Request;
    static void resolved(const char *, const ip_addr_t *, void *);
    static void pingReply(esp_ping_handle_t, void *);
    static void pingEnd(esp_ping_handle_t, void *);
    std::atomic<uint32_t> address_{0}, generation_{0};
    std::atomic<unsigned> dns_{0}, pending_{0}, replies_{0};
    std::atomic<bool> ended_{false};
    portMUX_TYPE lock_ = portMUX_INITIALIZER_UNLOCKED;
    esp_ping_handle_t ping_ = nullptr;
    bool active_ = false, doPing_ = false, done_ = false, ok_ = false;
    uint32_t since_ = 0;
};
}  // namespace hw
