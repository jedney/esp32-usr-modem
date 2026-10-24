// Saved settings in NVS: see nvs_store.h.
#include "nvs_store.h"

#include <Preferences.h>

namespace hw {

size_t NvsStore::load(uint8_t *buf, size_t cap) {
    Preferences prefs;
    if (!prefs.begin(ns_, false)) {
        return 0;
    }
    size_t len = prefs.isKey(key_) ? prefs.getBytesLength(key_) : 0;
    if (len > cap) {
        len = cap;  // too big to be ours; the caller reports it as damaged
    }
    if (len > 0) {
        len = prefs.getBytes(key_, buf, len);
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
