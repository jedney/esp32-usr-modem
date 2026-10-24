// Device password and OTA policy (brief 6.6, Q15/Q62/Q69/Q70). No hardware calls.
#pragma once

#include <cstddef>
#include <cstdint>

#include "modemcfg.h"

namespace devicecfg {

constexpr size_t kPasswordMax = 32;
constexpr size_t kGeneratedLength = 12;
constexpr uint32_t kWindowMs = 10 * 60 * 1000;
constexpr size_t kBlobSize = 110;
constexpr const char *kAlphabet = "23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnpqrstuvwxyz";

// Hardware implementation uses the ESP entropy source and the core's MD5 routines.
class Crypto {
public:
    virtual ~Crypto() = default;
    virtual uint32_t randomWord() = 0;
    virtual bool hashes(const char *password, char ota[33], char digest[33]) = 0;
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
    char password_[33] = {};
    char ota_[33] = {};
    char digest_[33] = {};
};

}  // namespace devicecfg
