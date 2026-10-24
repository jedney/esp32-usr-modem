// ArduinoOTA adapter. Owns no mDNS responder: the existing Wi-Fi service owns it.
#pragma once

#include <ArduinoOTA.h>

#include <memory>

#include "devicecfg.h"

namespace hw {

class OtaService {
public:
    explicit OtaService(devicecfg::Device &device) : device_(device) {}
    void poll(uint32_t now, bool wifiUp, bool idle, const char *hostname, uint32_t ip);
    void stop();

private:
    devicecfg::Device &device_;
    std::unique_ptr<ArduinoOTAClass> ota_;
    char hash_[33] = {};
    char hostname_[33] = {};
    uint32_t ip_ = 0;
};

}  // namespace hw
