#include "net_probe.h"
#include <lwip/dns.h>
#include <lwip/tcpip.h>
#include <new>
namespace hw {
struct NetProbe::Request {
    NetProbe *owner;
    uint32_t generation;
};
void NetProbe::resolved(const char *, const ip_addr_t *address, void *ctx) {
    auto *request = static_cast<Request *>(ctx);
    auto &self = *request->owner;
    portENTER_CRITICAL(&self.lock_);
    if (request->generation == self.generation_.load()) {
        if (address && IP_IS_V4(address)) {
            self.address_ = ip4_addr_get_u32(ip_2_ip4(address));
            self.dns_ = 1;
        } else {
            self.dns_ = 2;
        }
    }
    portEXIT_CRITICAL(&self.lock_);
    --self.pending_;
    delete request;
}
void NetProbe::pingReply(esp_ping_handle_t, void *ctx) {
    ++static_cast<NetProbe *>(ctx)->replies_;
}
void NetProbe::pingEnd(esp_ping_handle_t, void *ctx) {
    static_cast<NetProbe *>(ctx)->ended_ = true;
}
bool NetProbe::start(const char *host, bool ping, uint32_t now) {
    if (active_ || ping_ || pending_.load() >= 4) {
        return false;
    }
    cancel();
    done_ = ok_ = false;
    doPing_ = ping;
    since_ = now;
    active_ = true;
    replies_ = 0;
    ended_ = false;
    dns_ = 0;
    address_ = 0;
    ip_addr_t address;
    if (ipaddr_aton(host, &address) && IP_IS_V4(&address)) {
        address_ = ip4_addr_get_u32(ip_2_ip4(&address));
        dns_ = 1;
        return true;
    }
    auto *request = new (std::nothrow) Request{this, generation_.load()};
    if (!request) {
        active_ = false;
        return false;
    }
    ++pending_;
    LOCK_TCPIP_CORE();
    err_t result = dns_gethostbyname(host, &address, resolved, request);
    UNLOCK_TCPIP_CORE();
    if (result != ERR_INPROGRESS) {
        if (result == ERR_OK && IP_IS_V4(&address)) {
            address_ = ip4_addr_get_u32(ip_2_ip4(&address));
            dns_ = 1;
        } else {
            dns_ = 2;
        }
        --pending_;
        delete request;
    }
    return true;
}
void NetProbe::cancel() {
    portENTER_CRITICAL(&lock_);
    ++generation_;
    portEXIT_CRITICAL(&lock_);
    active_ = false;
    if (ping_) {
        esp_ping_stop(ping_);
    }
}
void NetProbe::poll(uint32_t now, bool network) {
    if (ping_ && ended_.load()) {
        esp_ping_delete_session(ping_);
        ping_ = nullptr;
        if (active_) {
            ok_ = replies_.load() > 0;
            done_ = true;
            active_ = false;
        }
    }
    if (!active_) {
        return;
    }
    // The 10 s limit is for the lookup. Once the ping has started it has its own 6 s (three
    // probes take about 3.5 s), so a slow lookup cannot cut the ping short, and
    // replies that did arrive count as a success even if the last probe was still out.
    const bool pinging = ping_ != nullptr;
    if (!network || dns_.load() == 2 || (!pinging && dns_.load() == 0 && now - since_ >= 10000) ||
        (pinging && now - pingSince_ >= 6000)) {
        const bool replied = network && pinging && replies_.load() > 0;
        cancel();
        done_ = true;
        ok_ = replied;
        return;
    }
    if (dns_.load() == 1 && !ping_) {
        if (!doPing_) {
            active_ = false;
            done_ = ok_ = true;
            return;
        }
        pingSince_ = now;
        esp_ping_config_t cfg = ESP_PING_DEFAULT_CONFIG();
        cfg.count = 3;
        cfg.interval_ms = 400;
        cfg.timeout_ms = 700;
        ip_addr_set_ip4_u32(&cfg.target_addr, address_.load());
        esp_ping_callbacks_t callbacks = {};
        callbacks.cb_args = this;
        callbacks.on_ping_success = pingReply;
        callbacks.on_ping_end = pingEnd;
        if (esp_ping_new_session(&cfg, &callbacks, &ping_) != ESP_OK ||
            esp_ping_start(ping_) != ESP_OK) {
            if (ping_) {
                esp_ping_delete_session(ping_);
                ping_ = nullptr;
            }
            active_ = false;
            done_ = true;
            ok_ = false;
        }
    }
}
}  // namespace hw
