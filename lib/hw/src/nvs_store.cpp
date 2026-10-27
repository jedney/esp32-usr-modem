// Saved settings in NVS: see nvs_store.h.
#include "nvs_store.h"

#include <Preferences.h>
#include <cstring>

namespace hw {

size_t NvsStore::load(uint8_t *buf, size_t cap) {
    Preferences prefs;
    memset(buf, 0, cap);
    if (!prefs.begin(ns_, false)) {
        return modemcfg::kLoadError;
    }
    size_t len = 0;
    if (prefs.isKey(key_)) {
        len = prefs.getBytesLength(key_);
        if (!len || len > cap || prefs.getBytes(key_, buf, cap) != len) {
            memset(buf, 0, cap);
            len = modemcfg::kLoadError;
        }
    }
    prefs.end();
    return len;
}

bool NvsStore::save(const uint8_t *data, size_t len) {
    Preferences prefs;
    if (!prefs.begin(ns_, false)) {
        return false;
    }
    bool ok = prefs.putBytes(key_, data, len) == len;
    prefs.end();
    return ok;
}

bool NvsStore::erase() {
    Preferences prefs;
    if (!prefs.begin(ns_, false)) {
        return false;
    }
    bool ok = prefs.clear();
    prefs.end();
    return ok;
}

}  // namespace hw
