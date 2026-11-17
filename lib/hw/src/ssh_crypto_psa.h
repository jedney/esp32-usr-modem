// PSA Crypto (Mbed TLS 4 / TF-PSA-Crypto in ESP-IDF) for the basic SSH client (lib/ssh).
// The key agreement runs on a small worker task on core 0, so the main loop, the serial
// ports and the handshake sound keep going while it works.
#pragma once

#include <psa/crypto.h>

#include <atomic>

#include "ssh_crypto.h"

namespace hw {
class PsaSshCrypto final : public ssh::Crypto {
public:
    PsaSshCrypto() = default;
    PsaSshCrypto(const PsaSshCrypto &) = delete;
    PsaSshCrypto &operator=(const PsaSshCrypto &) = delete;
    ~PsaSshCrypto() override;

    void random(uint8_t *out, size_t size) override;
    bool hashBegin() override;
    bool hashUpdate(const uint8_t *data, size_t size) override;
    bool hashFinish(uint8_t out[ssh::kHashSize]) override;
    bool agreeBegin(ssh::Exchange kind) override;
    bool agreeFinish(const uint8_t *peer, size_t size) override;
    ssh::Work agreeState() override;
    size_t agreePublic(uint8_t *out, size_t capacity) override;
    size_t agreeSecret(uint8_t *out, size_t capacity) override;
    void agreeEnd() override;
    bool keys(unsigned slot, ssh::Cipher cipher, const uint8_t *key, const uint8_t *iv,
              const uint8_t *macKey) override;
    bool ctr(unsigned slot, uint8_t *data, size_t size) override;
    bool gcm(unsigned slot, bool seal, const uint8_t nonce[ssh::kNonceSize], const uint8_t *aad,
             size_t aadSize, uint8_t *data, size_t size, uint8_t tag[ssh::kTagSize]) override;
    bool mac(unsigned slot, const ssh::Slice *parts, size_t count,
             uint8_t out[ssh::kMacSize]) override;
    void forget(unsigned slot) override;

    // How long the last key pair and shared secret took on the worker (for the tests).
    uint32_t pairMs() const { return pairMs_; }
    uint32_t secretMs() const { return secretMs_; }

private:
    enum class Job : uint8_t { kNone, kPair, kSecret };
    struct Slot {
        ssh::Cipher cipher = ssh::Cipher::kNone;
        psa_key_id_t key = PSA_KEY_ID_NULL, macKey = PSA_KEY_ID_NULL;
        psa_cipher_operation_t stream = PSA_CIPHER_OPERATION_INIT;
        bool streaming = false;
    };
    static void worker(void *self);
    void run();
    bool start(Job job);
    void waitIdle();

    psa_hash_operation_t hash_ = PSA_HASH_OPERATION_INIT;
    Slot slots_[2];
    void *task_ = nullptr;  // TaskHandle_t
    std::atomic<ssh::Work> state_{ssh::Work::kIdle};
    Job job_ = Job::kNone;
    ssh::Exchange kind_ = ssh::Exchange::kX25519;
    psa_key_id_t pair_ = PSA_KEY_ID_NULL;
    uint8_t public_[65] = {}, peer_[65] = {}, secret_[32] = {};
    size_t publicSize_ = 0, peerSize_ = 0, secretSize_ = 0;
    uint32_t pairMs_ = 0, secretMs_ = 0;
};
}  // namespace hw
