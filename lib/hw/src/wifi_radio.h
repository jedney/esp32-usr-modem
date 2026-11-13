// The S3's Wi-Fi, mDNS and NTP behind netcfg::Radio (project brief 6.6), using the
// Arduino core's WiFi, ESPmDNS and SNTP. All decisions (when to join, retry, scan) are
// made by netcfg::Net; this only does what it is told and reports what happened.
//
// The ESP-IDF's own copy of the network settings is switched off (WiFi.persistent(false)),
// so the password lives only in netcfg's NVS blob.

#pragma once

#include "netcfg.h"

namespace hw {

class EspRadio : public netcfg::Radio {
public:
    void connect(const netcfg::Settings &s) override;
    void leave() override;
    void off() override;
    netcfg::LinkInfo link() override;
    bool scanStart() override;
    int scanResult(netcfg::ScanEntry *out, int max) override;
    void startServices(const netcfg::Settings &s) override;
    int64_t utcNow() override;
    int64_t lastSync() override;
    void mac(uint8_t out[6]) override;
};

}  // namespace hw
