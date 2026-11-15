// ArduinoOTA adapter. Owns no mDNS responder: the existing Wi-Fi service owns it.
#pragma once

#include <ArduinoOTA.h>

#include <memory>

#include "devicecfg.h"

namespace hw {

class OtaService {
public:
    explicit OtaService(devicecfg::Device &device) : device_(device) {}
    // Called once for each authenticated upload, after its first use is saved and before
    // the image is written. The upload blocks the loop and then restarts the modem, so the
    // callback must report at once rather than queue for later.
    void onAuthenticated(void (*callback)()) { authenticated_ = callback; }
    void poll(uint32_t now, bool wifiUp, bool idle, const char *hostname, uint32_t ip);
    void stop();

private:
    devicecfg::Device &device_;
    void (*authenticated_)() = nullptr;
    std::unique_ptr<ArduinoOTAClass> ota_;
    char hash_[devicecfg::kOtaHashLength + 1] = {};  // SHA-256 hex, for setPasswordHash()
    char hostname_[33] = {};
    uint32_t ip_ = 0;
};

}  // namespace hw
