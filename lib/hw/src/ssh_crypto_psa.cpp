#include "ssh_crypto_psa.h"

#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstring>

#include "ssh_wire.h"

namespace hw {
namespace {
constexpr uint32_t kWorkerStack = 8192;

bool small(ssh::Cipher cipher) {
    return cipher == ssh::Cipher::kAes128Ctr || cipher == ssh::Cipher::kAes128Gcm;
}
bool isGcm(ssh::Cipher cipher) {
    return cipher == ssh::Cipher::kAes128Gcm || cipher == ssh::Cipher::kAes256Gcm;
}
bool import(psa_key_type_t type, size_t bits, psa_key_usage_t usage, psa_algorithm_t alg,
            const uint8_t *data, psa_key_id_t &id) {
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, type);
    psa_set_key_bits(&attributes, bits);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, alg);
    const bool ok = psa_import_key(&attributes, data, bits / 8, &id) == PSA_SUCCESS;
    psa_reset_key_attributes(&attributes);
    return ok;
}
uint32_t nowMs() {
    return uint32_t(esp_timer_get_time() / 1000);
}
}  // namespace

PsaSshCrypto::~PsaSshCrypto() {
    agreeEnd();
    forget(0);
    forget(1);
    psa_hash_abort(&hash_);
    if (task_) {
        vTaskDelete(static_cast<TaskHandle_t>(task_));
    }
}

// The radio is on during a call, so this is hardware noise (esp_fill_random()).
void PsaSshCrypto::random(uint8_t *out, size_t size) {
    psa_generate_random(out, size);
}

bool PsaSshCrypto::hashBegin() {
    psa_hash_abort(&hash_);
    return psa_hash_setup(&hash_, PSA_ALG_SHA_256) == PSA_SUCCESS;
}
bool PsaSshCrypto::hashUpdate(const uint8_t *data, size_t size) {
    return psa_hash_update(&hash_, data, size) == PSA_SUCCESS;
}
bool PsaSshCrypto::hashFinish(uint8_t out[ssh::kHashSize]) {
    size_t n = 0;
    return psa_hash_finish(&hash_, out, ssh::kHashSize, &n) == PSA_SUCCESS && n == ssh::kHashSize;
}

void PsaSshCrypto::worker(void *self) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        static_cast<PsaSshCrypto *>(self)->run();
    }
}
void PsaSshCrypto::run() {
    const uint32_t began = nowMs();
    bool ok = false;
    if (job_ == Job::kPair) {
        psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
        const bool x25519 = kind_ == ssh::Exchange::kX25519;
        psa_set_key_type(&attributes, PSA_KEY_TYPE_ECC_KEY_PAIR(x25519 ? PSA_ECC_FAMILY_MONTGOMERY
                                                                       : PSA_ECC_FAMILY_SECP_R1));
        psa_set_key_bits(&attributes, x25519 ? 255 : 256);
        psa_set_key_usage_flags(&attributes, PSA_KEY_USAGE_DERIVE);
        psa_set_key_algorithm(&attributes, PSA_ALG_ECDH);
        ok = psa_generate_key(&attributes, &pair_) == PSA_SUCCESS &&
             psa_export_public_key(pair_, public_, sizeof public_, &publicSize_) == PSA_SUCCESS;
        psa_reset_key_attributes(&attributes);
        pairMs_ = nowMs() - began;
    } else if (job_ == Job::kSecret) {
        ok = psa_raw_key_agreement(PSA_ALG_ECDH, pair_, peer_, peerSize_, secret_, sizeof secret_,
                                   &secretSize_) == PSA_SUCCESS;
        secretMs_ = nowMs() - began;
    }
    job_ = Job::kNone;
    state_.store(ok ? ssh::Work::kDone : ssh::Work::kFailed, std::memory_order_release);
}
bool PsaSshCrypto::start(Job job) {
    if (state_.load(std::memory_order_acquire) == ssh::Work::kBusy) {
        return false;
    }
    if (!task_) {
        TaskHandle_t task = nullptr;
        if (xTaskCreatePinnedToCore(worker, "sshkex", kWorkerStack, this, tskIDLE_PRIORITY + 1,
                                    &task, 0) != pdPASS) {
            return false;
        }
        task_ = task;
    }
    job_ = job;
    state_.store(ssh::Work::kBusy, std::memory_order_release);
    xTaskNotifyGive(static_cast<TaskHandle_t>(task_));
    return true;
}
// A hang-up during the key agreement waits for it (well under a second).
void PsaSshCrypto::waitIdle() {
    for (int i = 0; i < 300 && state_.load(std::memory_order_acquire) == ssh::Work::kBusy; ++i) {
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
bool PsaSshCrypto::agreeBegin(ssh::Exchange kind) {
    agreeEnd();
    kind_ = kind;
    return start(Job::kPair);
}
bool PsaSshCrypto::agreeFinish(const uint8_t *peer, size_t size) {
    if (pair_ == PSA_KEY_ID_NULL || size > sizeof peer_ ||
        state_.load(std::memory_order_acquire) != ssh::Work::kDone) {
        return false;
    }
    memcpy(peer_, peer, size);
    peerSize_ = size;
    return start(Job::kSecret);
}
ssh::Work PsaSshCrypto::agreeState() {
    return state_.load(std::memory_order_acquire);
}
size_t PsaSshCrypto::agreePublic(uint8_t *out, size_t capacity) {
    if (agreeState() != ssh::Work::kDone || capacity < publicSize_) {
        return 0;
    }
    memcpy(out, public_, publicSize_);
    return publicSize_;
}
size_t PsaSshCrypto::agreeSecret(uint8_t *out, size_t capacity) {
    if (agreeState() != ssh::Work::kDone || capacity < secretSize_) {
        return 0;
    }
    const size_t n = secretSize_;
    memcpy(out, secret_, n);
    ssh::wipe(secret_, sizeof secret_);
    secretSize_ = 0;
    return n;
}
void PsaSshCrypto::agreeEnd() {
    waitIdle();
    if (pair_ != PSA_KEY_ID_NULL) {
        psa_destroy_key(pair_);
        pair_ = PSA_KEY_ID_NULL;
    }
    ssh::wipe(secret_, sizeof secret_);
    secretSize_ = publicSize_ = peerSize_ = 0;
    state_.store(ssh::Work::kIdle, std::memory_order_release);
}

bool PsaSshCrypto::keys(unsigned slot, ssh::Cipher cipher, const uint8_t *key, const uint8_t *iv,
                        const uint8_t *macKey) {
    forget(slot);
    Slot &s = slots_[slot];
    s.cipher = cipher;
    if (cipher == ssh::Cipher::kNone) {
        return true;
    }
    const size_t bits = small(cipher) ? 128 : 256;
    const psa_algorithm_t alg = isGcm(cipher) ? PSA_ALG_GCM : PSA_ALG_CTR;
    bool ok = import(PSA_KEY_TYPE_AES, bits, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT, alg,
                     key, s.key);
    if (ok && !isGcm(cipher)) {
        // CTR is its own inverse, and the key stream runs on across packets.
        ok = psa_cipher_encrypt_setup(&s.stream, s.key, PSA_ALG_CTR) == PSA_SUCCESS &&
             psa_cipher_set_iv(&s.stream, iv, 16) == PSA_SUCCESS;
        s.streaming = ok;
    }
    if (ok && macKey) {
        ok = import(PSA_KEY_TYPE_HMAC, ssh::kMacSize * 8, PSA_KEY_USAGE_SIGN_MESSAGE,
                    PSA_ALG_HMAC(PSA_ALG_SHA_256), macKey, s.macKey);
    }
    if (!ok) {
        forget(slot);
    }
    return ok;
}
bool PsaSshCrypto::ctr(unsigned slot, uint8_t *data, size_t size) {
    Slot &s = slots_[slot];
    size_t n = 0;
    // Whole blocks only: the IDF driver does not hand back partial blocks consistently.
    return s.streaming && size % 16 == 0 &&
           psa_cipher_update(&s.stream, data, size, data, size, &n) == PSA_SUCCESS && n == size;
}
bool PsaSshCrypto::gcm(unsigned slot, bool seal, const uint8_t nonce[ssh::kNonceSize],
                       const uint8_t *aad, size_t aadSize, uint8_t *data, size_t size,
                       uint8_t tag[ssh::kTagSize]) {
    Slot &s = slots_[slot];
    if (s.key == PSA_KEY_ID_NULL) {
        return false;
    }
    psa_aead_operation_t op = PSA_AEAD_OPERATION_INIT;
    size_t n = 0, rest = 0, tagSize = 0;
    psa_status_t status = seal ? psa_aead_encrypt_setup(&op, s.key, PSA_ALG_GCM)
                               : psa_aead_decrypt_setup(&op, s.key, PSA_ALG_GCM);
    if (status == PSA_SUCCESS) {
        status = psa_aead_set_nonce(&op, nonce, ssh::kNonceSize);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_update_ad(&op, aad, aadSize);
    }
    if (status == PSA_SUCCESS) {
        status = psa_aead_update(&op, data, size, data, size, &n);
    }
    if (status == PSA_SUCCESS) {
        status = seal
                     ? psa_aead_finish(&op, data + n, size - n, &rest, tag, ssh::kTagSize, &tagSize)
                     : psa_aead_verify(&op, data + n, size - n, &rest, tag, ssh::kTagSize);
    }
    if (status != PSA_SUCCESS) {
        psa_aead_abort(&op);
        return false;
    }
    return n + rest == size;
}
bool PsaSshCrypto::mac(unsigned slot, const ssh::Slice *parts, size_t count,
                       uint8_t out[ssh::kMacSize]) {
    Slot &s = slots_[slot];
    if (s.macKey == PSA_KEY_ID_NULL) {
        return false;
    }
    psa_mac_operation_t op = PSA_MAC_OPERATION_INIT;
    psa_status_t status = psa_mac_sign_setup(&op, s.macKey, PSA_ALG_HMAC(PSA_ALG_SHA_256));
    for (size_t i = 0; i < count && status == PSA_SUCCESS; ++i) {
        status = psa_mac_update(&op, parts[i].data, parts[i].size);
    }
    size_t n = 0;
    if (status == PSA_SUCCESS) {
        status = psa_mac_sign_finish(&op, out, ssh::kMacSize, &n);
    }
    if (status != PSA_SUCCESS) {
        psa_mac_abort(&op);
        return false;
    }
    return n == ssh::kMacSize;
}
void PsaSshCrypto::forget(unsigned slot) {
    Slot &s = slots_[slot];
    if (s.streaming) {
        psa_cipher_abort(&s.stream);
        s.streaming = false;
    }
    s.stream = psa_cipher_operation_init();
    if (s.key != PSA_KEY_ID_NULL) {
        psa_destroy_key(s.key);
        s.key = PSA_KEY_ID_NULL;
    }
    if (s.macKey != PSA_KEY_ID_NULL) {
        psa_destroy_key(s.macKey);
        s.macKey = PSA_KEY_ID_NULL;
    }
    s.cipher = ssh::Cipher::kNone;
}
}  // namespace hw
