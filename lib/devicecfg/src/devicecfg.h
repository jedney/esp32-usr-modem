// Device password and OTA policy (spec 3.6). No hardware calls.
#pragma once

#include <cstddef>
#include <cstdint>

#include "modemcfg.h"

namespace devicecfg {

constexpr size_t kPasswordMax = 32;
constexpr size_t kGeneratedLength = 12;
constexpr uint32_t kWindowMs = 10 * 60 * 1000;
constexpr size_t kOtaHashLength = 64;     // SHA-256 of the password, lower-case hex (ArduinoOTA)
constexpr size_t kDigestHashLength = 32;  // MD5 HA1, lower-case hex: kept in the record, unused
// USRA, layout, policy, reset marker, then the password and both hashes with their
// terminators, then a CRC-32.
constexpr size_t kBlobSize =
    7 + (kPasswordMax + 1) + (kOtaHashLength + 1) + (kDigestHashLength + 1) + 4;
constexpr const char *kAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";

// Hardware implementation uses the ESP entropy source and PSA hashes.
class Crypto {
public:
    virtual ~Crypto() = default;
    virtual uint32_t randomWord() = 0;
    virtual bool hashes(const char *password, char ota[kOtaHashLength + 1],
                        char digest[kDigestHashLength + 1]) = 0;
};

void wipe(void *data, size_t size);
bool validPassword(const char *text);

enum class Policy : uint8_t { kAlways, kDemand };

class Device {
public:
    Device(modemcfg::Store &store, Crypto &crypto) : store_(store), crypto_(crypto) {}
    ~Device();
    // Empty storage generates credentials; damaged storage disables OTA until PASS
    // or FACTORY repairs it. A reset marker is recovered before any other settings load.
    bool begin();
    bool ready() const { return ready_ && !resetPending_; }
    bool needsReset() const { return resetPending_; }
    const char *bootPassword() const { return ready() ? password_ : ""; }
    const char *otaHash() const { return ready() ? ota_ : ""; }
    const char *digestHash() const { return ready() ? digest_ : ""; }
    Policy policy() const { return policy_; }
    // A user replacement never remains available for subsequent boot display.
    bool change(const char *password);
    bool generate(char out[kGeneratedLength + 1]);
    bool markUsed();  // called by an authenticated upload, and later by web login
    // True if `password` is the device password: its hash is compared in constant time.
    bool matches(const char *password);
    bool setPolicy(Policy policy);
    bool openWindow(uint32_t now);
    void closeWindow() { windowOpen_ = false; }
    uint32_t remainingMs(uint32_t now) const;
    void tick(uint32_t now) {
        if (windowOpen_ && remainingMs(now) == 0) {
            closeWindow();
        }
    }
    bool wantsListener(uint32_t now, bool wifiUp, bool idle) const;
    // Erase only project namespaces, keeping ESP calibration data. The durable marker
    // makes an interrupted reset resume at next boot; credential storage is erased last.
    bool factoryReset(modemcfg::Store *const *otherStores, size_t count);

private:
    bool replace(const char *password, bool showAtBoot);
    bool save(Policy policy, bool reset, const char *password, const char *ota, const char *digest);
    modemcfg::Store &store_;
    Crypto &crypto_;
    Policy policy_ = Policy::kAlways;
    bool ready_ = false;
    bool resetPending_ = false;
    bool windowOpen_ = false;
    uint32_t openedAt_ = 0;
    char password_[kPasswordMax + 1] = {};
    char ota_[kOtaHashLength + 1] = {};
    char digest_[kDigestHashLength + 1] = {};
};

}  // namespace devicecfg
