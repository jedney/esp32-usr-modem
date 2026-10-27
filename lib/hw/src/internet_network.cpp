#include "internet_network.h"
#include <WiFi.h>
#include <lwip/ip4.h>
#include <lwip/lwip_napt.h>
#include <lwip/tcpip.h>
#include <cstring>

static_assert(IP_FORWARD && IP_NAPT && CONFIG_LWIP_L2_TO_L3_COPY,
              "Internet dial-up requires the source-built forwarding/NAPT SDK");
static_assert(CONFIG_SPIRAM_MODE_OCT && CONFIG_SPIRAM_BOOT_INIT &&
                  CONFIG_ESP32S3_DEFAULT_CPU_FREQ_MHZ == 240 &&
                  CONFIG_LWIP_TCPIP_TASK_STACK_SIZE >= 6144,
              "SDK must preserve N16R8 memory/CPU and the bounded network task stack");
static_assert(IP_NAPT_PORT_RANGE_START == 32768 && IP_NAPT_PORT_RANGE_END < 49152,
              "NAPT must not reuse the modem's local ephemeral socket ports");

namespace hw {
bool InternetNetwork::begin(const dialup::Options &options, uint16_t mtu) {
    if (!idle() || WiFi.status() != WL_CONNECTED || !dialup::validOptions(options) || mtu < 128 ||
        mtu > dialup::kMtu)
        return false;
    if (!input_)
        input_ = xQueueCreate(4, sizeof(Packet));
    if (!output_)
        output_ = xQueueCreate(4, sizeof(Packet));
    if (!input_ || !output_)
        return false;
    xQueueReset(input_);
    xQueueReset(output_);
    options_ = options;
    mtu_ = mtu;
    stationIp_ = uint32_t(WiFi.localIP());
    drops_ = 0;
    failed_ = false;
    wanted_ = true;
    poll();
    return true;
}
void InternetNetwork::end() {
    wanted_ = false;
    poll();
}
bool InternetNetwork::up() const {
    return wanted_ && !failed_ && WiFi.status() == WL_CONNECTED &&
           uint32_t(WiFi.localIP()) == stationIp_;
}
void InternetNetwork::poll() {
    if (!wanted_ && !online_)
        return;
    if (wanted_ && online_ && uxQueueMessagesWaiting(input_) == 0)
        return;
    bool expected = false;
    if (scheduled_.compare_exchange_strong(expected, true)) {
        if (tcpip_try_callback(work, this) != ERR_OK)
            scheduled_ = false;
    }
}
void InternetNetwork::work(void *self) {
    auto &network = *static_cast<InternetNetwork *>(self);
    network.process();
    network.scheduled_ = false;
}
err_t InternetNetwork::init(netif *interface) {
    auto &network = *static_cast<InternetNetwork *>(interface->state);
    interface->name[0] = 'd';
    interface->name[1] = 'u';
    interface->mtu = network.mtu_;
    interface->output = output;
    interface->flags = 0;  // point-to-point, no ARP or broadcast
    return ERR_OK;
}
err_t InternetNetwork::output(netif *interface, pbuf *packet, const ip4_addr_t *destination) {
    auto &network = *static_cast<InternetNetwork *>(interface->state);
    if (!network.wanted_ || lwip_ntohl(destination->addr) != network.options_.peer ||
        packet->tot_len > network.mtu_)
        return ERR_RTE;
    Packet copy;
    copy.length = packet->tot_len;
    if (pbuf_copy_partial(packet, copy.bytes, copy.length, 0) != copy.length)
        return ERR_BUF;
    // Fragmented datagrams are outside this bounded 1500-byte serial interface.
    if (copy.length < 20 || (copy.bytes[6] & 0x3f) || copy.bytes[7]) {
        ++network.drops_;
        return ERR_VAL;
    }
    if (xQueueSend(network.output_, &copy, 0) != pdTRUE) {
        ++network.drops_;
        return ERR_MEM;
    }
    return ERR_OK;
}
void InternetNetwork::process() {
    if (!wanted_) {
        if (online_) {
            // This modem owns the only NAPT interface. Disabling it destroys the
            // table and timer, so the next call cannot inherit old translations.
            ip_napt_enable_no(interface_.num, 0);
            netif_set_link_down(&interface_);
            netif_set_down(&interface_);
            netif_remove(&interface_);
            online_ = false;
        }
        if (input_)
            xQueueReset(input_);
        if (output_)
            xQueueReset(output_);
        return;
    }
    if (!online_) {
        ip4_addr_t local{lwip_htonl(options_.local)}, mask{lwip_htonl(0xfffffffcu)}, gateway{0};
        memset(&interface_, 0, sizeof interface_);
        if (!netif_add(&interface_, &local, &mask, &gateway, this, init, ip4_input)) {
            failed_ = true;
            wanted_ = false;
            return;
        }
        netif_set_up(&interface_);
        netif_set_link_up(&interface_);
        ip_napt_enable_no(interface_.num, 1);
        online_ = true;
    }
    Packet packet;
    for (unsigned i = 0; wanted_ && i < 4 && xQueueReceive(input_, &packet, 0) == pdTRUE; ++i) {
        // These IP packets are forwarded to Ethernet/Wi-Fi, whose output adds a
        // link header. RAW has no headroom and etharp_output silently rejects it.
        pbuf *buffer = pbuf_alloc(PBUF_LINK, packet.length, PBUF_RAM);
        if (!buffer) {
            ++drops_;
            continue;
        }
        if (pbuf_take(buffer, packet.bytes, packet.length) != ERR_OK ||
            interface_.input(buffer, &interface_) != ERR_OK) {
            pbuf_free(buffer);
            ++drops_;
        }
    }
}
bool InternetNetwork::send(const uint8_t *packet, size_t length) {
    if (!up() || !length || length > dialup::kMtu)
        return false;
    Packet copy;
    copy.length = length;
    memcpy(copy.bytes, packet, length);
    if (xQueueSend(input_, &copy, 0) != pdTRUE) {
        ++drops_;
        return false;
    }
    poll();
    return true;
}
size_t InternetNetwork::receive(uint8_t *packet, size_t capacity) {
    if (!up())
        return 0;
    Packet copy;
    if (xQueueReceive(output_, &copy, 0) != pdTRUE)
        return 0;
    if (copy.length > capacity) {
        ++drops_;
        return 0;
    }
    memcpy(packet, copy.bytes, copy.length);
    return copy.length;
}
}  // namespace hw
