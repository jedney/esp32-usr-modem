#include "ota_service.h"

#include <Update.h>

#include <cstdio>
#include <cstring>

#include "hw.h"

namespace hw {

void OtaService::stop() {
    if (ota_) {
        ota_->end();
        ota_.reset();
    }
    devicecfg::wipe(hash_, sizeof hash_);
}

void OtaService::poll(uint32_t now, bool wifiUp, bool idle, const char *hostname, uint32_t ip) {
    device_.tick(now);
    if (!device_.wantsListener(now, wifiUp, idle)) {
        stop();
        return;
    }
    if (ota_ &&
        (strcmp(hash_, device_.otaHash()) != 0 || strcmp(hostname_, hostname) != 0 || ip_ != ip)) {
        stop();
    }
    if (!ota_) {
        // The core accepts setPasswordHash only once per instance, even after end().
        ota_.reset(new ArduinoOTAClass());
        memcpy(hash_, device_.otaHash(), sizeof hash_);
        snprintf(hostname_, sizeof hostname_, "%s", hostname);
        ip_ = ip;
        ota_->setMdnsEnabled(false);  // never stop/restart the Wi-Fi responder
        ota_->setHostname(hostname);
        ota_->setPasswordHash(hash_);
        ota_->onStart([this]() {
            // onStart is reached only after password authentication. Persist removal
            // before writing the upload; an NVS failure must prevent installation.
            if (!device_.markUsed()) {
                Update.abort();
            }
        });
        // ArduinoOTA receives the whole image inside handle(), in the loop task: feed the
        // watchdog after every chunk (STA-06).
        ota_->onProgress([](unsigned int, unsigned int) { hw::feedWatchdog(); });
        ota_->begin();
    }
    ota_->handle();
}

}  // namespace hw
