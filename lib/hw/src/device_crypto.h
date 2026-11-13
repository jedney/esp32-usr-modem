// ESP32 entropy and hash adapter for the shared device password.
#pragma once

#include "devicecfg.h"

namespace hw {
class EspCrypto : public devicecfg::Crypto {
public:
    EspCrypto();
    ~EspCrypto() override;
    bool begin();  // before Wi-Fi, ADC or I2S initialization
    uint32_t randomWord() override;
    bool hashes(const char *password, char ota[33], char digest[33]) override;

private:
    bool seeded_ = false;
};
}  // namespace hw
