#include "device_crypto.h"

#include <WiFi.h>
#include <bootloader_random.h>
#include <esp_system.h>
#include <mbedtls/md5.h>

#include <cstdio>
#include <cstring>

namespace hw {

EspCrypto::EspCrypto() {
    mbedtls_ctr_drbg_init(&rng_);
}

EspCrypto::~EspCrypto() {
    mbedtls_ctr_drbg_free(&rng_);
}

int EspCrypto::entropy(void *ctx, unsigned char *out, size_t size) {
    const EspCrypto &self = *static_cast<EspCrypto *>(ctx);
    if (!self.seeding_ && WiFi.getMode() == WIFI_OFF) {
        return -1;  // never reseed from a weak source or disturb an initialized ADC
    }
    esp_fill_random(out, size);
    return 0;
}

bool EspCrypto::begin() {
    if (seeded_) {
        return true;
    }
    // Seed a cryptographic generator early, before the ADC or RF is initialized.
    // Later offline password changes use it without reconfiguring the pot's ADC.
    seeding_ = true;
    bootloader_random_enable();
    const unsigned char label[] = "usr-modem-device-password";
    seeded_ = mbedtls_ctr_drbg_seed(&rng_, entropy, this, label, sizeof label - 1) == 0;
    bootloader_random_disable();
    seeding_ = false;
    return seeded_;
}

uint32_t EspCrypto::randomWord() {
    uint32_t value = UINT32_MAX;
    if (!seeded_ ||
        mbedtls_ctr_drbg_random(&rng_, reinterpret_cast<uint8_t *>(&value), sizeof value) != 0) {
        return UINT32_MAX;  // rejected by the bounded generator in devicecfg
    }
    return value;
}

namespace {
bool md5(const char *text, char out[33]) {
    uint8_t hash[16] = {};
    bool ok = mbedtls_md5_ret(reinterpret_cast<const uint8_t *>(text), strlen(text), hash) == 0;
    const char *hex = "0123456789abcdef";
    for (size_t i = 0; i < sizeof hash; ++i) {
        out[2 * i] = hex[hash[i] >> 4];
        out[2 * i + 1] = hex[hash[i] & 15];
    }
    out[32] = '\0';
    devicecfg::wipe(hash, sizeof hash);
    return ok;
}
}  // namespace

bool EspCrypto::hashes(const char *password, char ota[33], char digest[33]) {
    char text[sizeof "admin:usr-modem:" + devicecfg::kPasswordMax] = {};
    snprintf(text, sizeof text, "admin:usr-modem:%s", password);
    bool ok = md5(password, ota) && md5(text, digest);
    devicecfg::wipe(text, sizeof text);
    return ok;
}

}  // namespace hw
