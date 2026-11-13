#include "device_crypto.h"

#include <bootloader_random.h>
#include <psa/crypto.h>

#include <cstdio>
#include <cstring>

namespace hw {

// v3.0 SPIKE (WP-18): Mbed TLS 4 has no public CTR-DRBG; PSA keeps its own generator.
// psa_crypto_init() seeds it here, while the bootloader entropy source is on. WP-19 must
// check how PSA reseeds later (the old code refused to reseed with the radio off).
EspCrypto::EspCrypto() = default;

EspCrypto::~EspCrypto() = default;

bool EspCrypto::begin() {
    if (seeded_) {
        return true;
    }
    // Seed a cryptographic generator early, before the ADC or RF is initialized.
    // Later offline password changes use it without reconfiguring the pot's ADC.
    bootloader_random_enable();
    seeded_ = psa_crypto_init() == PSA_SUCCESS;
    bootloader_random_disable();
    return seeded_;
}

uint32_t EspCrypto::randomWord() {
    uint32_t value = UINT32_MAX;
    if (!seeded_ ||
        psa_generate_random(reinterpret_cast<uint8_t *>(&value), sizeof value) != PSA_SUCCESS) {
        return UINT32_MAX;  // rejected by the bounded generator in devicecfg
    }
    return value;
}

namespace {
bool md5(const char *text, char out[33]) {
    uint8_t hash[16] = {};
    size_t length = 0;
    bool ok = psa_hash_compute(PSA_ALG_MD5, reinterpret_cast<const uint8_t *>(text), strlen(text),
                               hash, sizeof hash, &length) == PSA_SUCCESS;
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
