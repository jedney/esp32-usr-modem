#include "device_crypto.h"

#include <bootloader_random.h>
#include <esp_random.h>
#include <psa/crypto.h>

#include <cstdio>
#include <cstring>

namespace hw {

static_assert(sizeof(psa_key_id_t) == sizeof(uint32_t), "device_crypto.h stores the key id");

// Random numbers for the device password.
//
// ESP-IDF builds Mbed TLS with an external generator, so psa_generate_random() is just
// esp_fill_random(): hardware noise only while the radio runs or bootloader_random_enable() is
// on, a pseudo-random sequence otherwise. The first password is made at boot, before Wi-Fi, and
// the noise source uses the SAR ADC that the volume pot needs from then on. So begin() draws a
// 256-bit seed once, with the noise source on, and keeps it as a PSA HMAC key. randomWord() is
// HMAC-SHA-256 of a counter under that key. Nothing is ever drawn from the weak source, and
// every boot has a new seed. IDF itself calls psa_crypto_init() at start-up, before app_main().
EspCrypto::~EspCrypto() {
    if (key_ != PSA_KEY_ID_NULL) {
        psa_destroy_key(key_);
    }
}

bool EspCrypto::begin() {
    if (key_ != PSA_KEY_ID_NULL) {
        return true;
    }
    uint8_t seed[32];
    bootloader_random_enable();
    esp_fill_random(seed, sizeof seed);
    bootloader_random_disable();
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_HMAC);
    psa_set_key_bits(&attributes, sizeof seed * 8);
    psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_SIGN_MESSAGE);
    psa_set_key_algorithm(&attributes, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    psa_key_id_t key = PSA_KEY_ID_NULL;
    const bool ok = psa_crypto_init() == PSA_SUCCESS &&
                    psa_import_key(&attributes, seed, sizeof seed, &key) == PSA_SUCCESS;
    psa_reset_key_attributes(&attributes);
    devicecfg::wipe(seed, sizeof seed);
    key_ = ok ? key : PSA_KEY_ID_NULL;
    return ok;
}

uint32_t EspCrypto::randomWord() {
    uint32_t value = UINT32_MAX;  // rejected by the bounded generator in devicecfg
    if (key_ == PSA_KEY_ID_NULL) {
        return value;
    }
    uint8_t counter[8];
    for (int i = 0; i < 8; ++i) {
        counter[i] = static_cast<uint8_t>(counter_ >> (56 - 8 * i));
    }
    ++counter_;
    uint8_t mac[PSA_HASH_LENGTH(PSA_ALG_SHA_256)];
    size_t length = 0;
    if (psa_mac_compute(key_, PSA_ALG_HMAC(PSA_ALG_SHA_256), counter, sizeof counter, mac,
                        sizeof mac, &length) == PSA_SUCCESS &&
        length == sizeof mac) {
        memcpy(&value, mac, sizeof value);
    }
    devicecfg::wipe(mac, sizeof mac);
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
