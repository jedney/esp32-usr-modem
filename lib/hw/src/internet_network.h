#pragma once
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <lwip/netif.h>
#include <atomic>
#include "internet.h"

namespace hw {
// All netif/NAPT calls run on lwIP's TCP/IP task. Only bounded packet queues cross
// the task boundary; callbacks never touch UARTs, command parsers or menus.
class InternetNetwork : public dialup::Network {
public:
    bool begin(const dialup::Options &options, uint16_t mtu) override;
    void end() override;
    bool send(const uint8_t *packet, size_t length) override;
    size_t receive(uint8_t *packet, size_t capacity) override;
    bool up() const override;
    void poll();
    bool idle() const { return !wanted_ && !online_ && !scheduled_; }
    uint32_t drops() const { return drops_; }

private:
    struct Packet {
        uint16_t length;
        uint8_t bytes[dialup::kMtu];
    };
    static void work(void *self);
    static err_t init(netif *interface);
    static err_t output(netif *interface, pbuf *packet, const ip4_addr_t *destination);
    void process();
    netif interface_ = {};
    QueueHandle_t input_ = nullptr, output_ = nullptr;
    dialup::Options options_;
    uint16_t mtu_ = dialup::kMtu;
    uint32_t stationIp_ = 0;
    std::atomic<bool> wanted_{false}, online_{false}, scheduled_{false}, failed_{false};
    std::atomic<uint32_t> drops_{0};
};
}  // namespace hw
