// ESP32 entropy and hash adapter for the shared device password.
#pragma once

#include <mbedtls/ctr_drbg.h>

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
    static int entropy(void *ctx, unsigned char *out, size_t size);
    mbedtls_ctr_drbg_context rng_;
    bool seeding_ = false;
    bool seeded_ = false;
};
}  // namespace hw
