#include "devicecfg.h"

#include <cstring>

namespace devicecfg {

void wipe(void *data, size_t size) {
    volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
    while (size-- > 0) {
        *p++ = 0;
    }
}

bool validPassword(const char *text) {
    size_t n = strlen(text);
    if (n < 8 || n > kPasswordMax) {
        return false;
    }
    for (size_t i = 0; i < n; ++i) {
        if (text[i] < 32 || text[i] > 126) {
            return false;
        }
    }
    return true;
}

namespace {
bool validHash(const char *hash) {
    for (size_t i = 0; i < 32; ++i) {
        if (!((hash[i] >= '0' && hash[i] <= '9') || (hash[i] >= 'a' && hash[i] <= 'f'))) {
            return false;
        }
    }
    return hash[32] == '\0';
}
}  // namespace

Device::~Device() {
    wipe(password_, sizeof password_);
    wipe(ota_, sizeof ota_);
    wipe(digest_, sizeof digest_);
}

bool Device::save(Policy policy, bool reset, const char *password, const char *ota,
                  const char *digest) {
    // USRA, layout 1, policy, reset marker, password (or zeroes), two hex hashes, CRC.
    uint8_t blob[kBlobSize] = {
        'U', 'S', 'R', 'A', 1, static_cast<uint8_t>(policy), static_cast<uint8_t>(reset)};
    memcpy(blob + 7, password, strlen(password));
    memcpy(blob + 40, ota, 32);
    memcpy(blob + 73, digest, 32);
    uint32_t crc = modemcfg::crc32(blob, kBlobSize - 4);
    for (size_t i = 0; i < 4; ++i) {
        blob[kBlobSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    bool ok = store_.save(blob, sizeof blob);
    wipe(blob, sizeof blob);
    return ok;
}

bool Device::begin() {
    ready_ = false;
    resetPending_ = false;
    windowOpen_ = false;
    wipe(password_, sizeof password_);
    wipe(ota_, sizeof ota_);
    wipe(digest_, sizeof digest_);
    uint8_t blob[kBlobSize + 1] = {};
    size_t n = store_.load(blob, sizeof blob);
    if (n == 0) {
        policy_ = Policy::kAlways;
        char password[kGeneratedLength + 1] = {};
        bool ok = generate(password) && replace(password, true);
        wipe(password, sizeof password);
        return ok;
    }
    uint32_t crc = 0;
    for (size_t i = 0; i < 4; ++i) {
        crc |= static_cast<uint32_t>(blob[kBlobSize - 4 + i]) << (8 * i);
    }
    const char *pass = reinterpret_cast<const char *>(blob + 7);
    const char *ota = reinterpret_cast<const char *>(blob + 40);
    const char *digest = reinterpret_cast<const char *>(blob + 73);
    bool ok = n == kBlobSize && memcmp(blob, "USRA\1", 5) == 0 && blob[5] <= 1 && blob[6] <= 1 &&
              blob[39] == 0 && crc == modemcfg::crc32(blob, kBlobSize - 4) && validHash(ota) &&
              validHash(digest) &&
              (*pass == '\0' || (strlen(pass) == kGeneratedLength && validPassword(pass)));
    if (ok) {
        policy_ = static_cast<Policy>(blob[5]);
        resetPending_ = blob[6] != 0;
        memcpy(password_, pass, sizeof password_);
        memcpy(ota_, ota, sizeof ota_);
        memcpy(digest_, digest, sizeof digest_);
        ready_ = true;
    }
    wipe(blob, sizeof blob);
    return ok;
}

bool Device::generate(char out[kGeneratedLength + 1]) {
    const uint32_t symbols = static_cast<uint32_t>(strlen(kAlphabet));
    const uint32_t limit = UINT32_MAX - (UINT32_MAX % symbols);
    for (size_t i = 0; i < kGeneratedLength; ++i) {
        // Bound retries so a broken entropy adapter cannot hang the command loop.
        uint32_t value = 0;
        unsigned tries = 0;
        do {
            value = crypto_.randomWord();
            if (++tries > 128) {
                wipe(out, kGeneratedLength + 1);
                return false;
            }
        } while (value >= limit);
        out[i] = kAlphabet[value % symbols];
    }
    out[kGeneratedLength] = '\0';
    return true;
}

bool Device::replace(const char *password, bool showAtBoot) {
    if (resetPending_ || !validPassword(password)) {
        return false;
    }
    char ota[33] = {};
    char digest[33] = {};
    bool ok = crypto_.hashes(password, ota, digest) && validHash(ota) && validHash(digest) &&
              save(policy_, false, showAtBoot ? password : "", ota, digest);
    if (ok) {
        wipe(password_, sizeof password_);
        if (showAtBoot) {
            memcpy(password_, password, strlen(password));
        }
        memcpy(ota_, ota, sizeof ota_);
        memcpy(digest_, digest, sizeof digest_);
        ready_ = true;
    }
    wipe(ota, sizeof ota);
    wipe(digest, sizeof digest);
    return ok;
}

bool Device::change(const char *password) {
    return replace(password, false);
}

bool Device::markUsed() {
    if (!ready()) {
        return false;
    }
    if (*password_ == '\0') {
        return true;
    }
    // If persistence fails, the OTA adapter aborts that upload and can retry later.
    bool ok = save(policy_, false, "", ota_, digest_);
    if (ok) {
        wipe(password_, sizeof password_);
    }
    return ok;
}

bool Device::setPolicy(Policy policy) {
    if (!ready() || (policy != Policy::kAlways && policy != Policy::kDemand) ||
        !save(policy, false, password_, ota_, digest_)) {
        return false;
    }
    policy_ = policy;
    closeWindow();
    return true;
}

bool Device::openWindow(uint32_t now) {
    if (!ready() || policy_ != Policy::kDemand) {
        return false;
    }
    windowOpen_ = true;
    openedAt_ = now;
    return true;
}

uint32_t Device::remainingMs(uint32_t now) const {
    uint32_t elapsed = now - openedAt_;
    return windowOpen_ && elapsed < kWindowMs ? kWindowMs - elapsed : 0;
}

bool Device::wantsListener(uint32_t now, bool wifiUp, bool idle) const {
    return ready() && wifiUp && idle && (policy_ == Policy::kAlways || remainingMs(now) != 0);
}

bool Device::factoryReset(modemcfg::Store *const *otherStores, size_t count) {
    if (!resetPending_) {
        // A damaged record can still be explicitly reset. Fixed non-secret hashes
        // make the marker readable, but ready() stays false throughout the reset.
        const char *emptyHash = "00000000000000000000000000000000";
        if (!save(Policy::kAlways, true, "", emptyHash, emptyHash)) {
            return false;
        }
        resetPending_ = true;
        wipe(password_, sizeof password_);
    }
    for (size_t i = 0; i < count; ++i) {
        if (!otherStores[i]->erase()) {
            return false;
        }
    }
    if (!store_.erase()) {
        return false;
    }
    ready_ = false;
    return true;
}

}  // namespace devicecfg
