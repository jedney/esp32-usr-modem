// The cryptography the SSH client needs, supplied by the platform (PSA Crypto on the
// modem, a simple stand-in in the PC tests). Host keys and signatures are never checked:
// the SSH support is basic and for hobby use only.
//
// No Arduino dependencies: unit-tested on the PC.
#pragma once
#include <cstddef>
#include <cstdint>

namespace ssh {
enum class Exchange : uint8_t { kX25519, kP256 };
enum class Cipher : uint8_t { kNone, kAes128Ctr, kAes256Ctr, kAes128Gcm, kAes256Gcm };
enum class Work : uint8_t { kIdle, kBusy, kDone, kFailed };
struct Slice {
    const uint8_t *data;
    size_t size;
};
constexpr size_t kHashSize = 32;  // SHA-256
constexpr size_t kMacSize = 32;   // HMAC-SHA-256
constexpr size_t kTagSize = 16;   // AES-GCM
constexpr size_t kNonceSize = 12;

class Crypto {
public:
    virtual ~Crypto() = default;
    virtual void random(uint8_t *out, size_t size) = 0;

    // One SHA-256 at a time.
    virtual bool hashBegin() = 0;
    virtual bool hashUpdate(const uint8_t *data, size_t size) = 0;
    virtual bool hashFinish(uint8_t out[kHashSize]) = 0;

    // Key agreement. The slow steps may run elsewhere: agreeBegin() makes an ephemeral key
    // pair and agreeFinish() starts the shared secret; agreeState() is kBusy until each one
    // is done. agreeSecret() hands over the secret and erases its copy; agreeEnd() destroys
    // the key pair.
    virtual bool agreeBegin(Exchange kind) = 0;
    virtual bool agreeFinish(const uint8_t *peer, size_t size) = 0;
    virtual Work agreeState() = 0;
    virtual size_t agreePublic(uint8_t *out, size_t capacity) = 0;
    virtual size_t agreeSecret(uint8_t *out, size_t capacity) = 0;
    virtual void agreeEnd() = 0;

    // Packet protection. Slot 0 protects what this side sends, slot 1 what it receives.
    // macKey is null for GCM. ctr() takes whole 16-byte blocks (SSH packets always are) and
    // continues the slot's key stream across calls.
    virtual bool keys(unsigned slot, Cipher cipher, const uint8_t *key, const uint8_t *iv,
                      const uint8_t *macKey) = 0;
    virtual bool ctr(unsigned slot, uint8_t *data, size_t size) = 0;
    // In place. Sealing writes the tag; opening returns false when the tag is wrong.
    virtual bool gcm(unsigned slot, bool seal, const uint8_t nonce[kNonceSize], const uint8_t *aad,
                     size_t aadSize, uint8_t *data, size_t size, uint8_t tag[kTagSize]) = 0;
    virtual bool mac(unsigned slot, const Slice *parts, size_t count, uint8_t out[kMacSize]) = 0;
    virtual void forget(unsigned slot) = 0;
};
}  // namespace ssh
