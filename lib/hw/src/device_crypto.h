// ESP32 entropy and hash adapter for the shared device password.
#pragma once

#include "devicecfg.h"

namespace hw {
class EspCrypto : public devicecfg::Crypto {
public:
    EspCrypto() = default;
    EspCrypto(const EspCrypto &) = delete;
    EspCrypto &operator=(const EspCrypto &) = delete;
    ~EspCrypto() override;
    bool begin();                    // before Wi-Fi, ADC or I2S initialization
    uint32_t randomWord() override;  // from the main loop only
    bool hashes(const char *password, char ota[33], char digest[33]) override;

private:
    uint32_t key_ = 0;  // PSA key id (PSA_KEY_ID_NULL = 0) holding the boot seed
    uint64_t counter_ = 0;
};
}  // namespace hw
