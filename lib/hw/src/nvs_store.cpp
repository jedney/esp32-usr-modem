// Saved modem settings in NVS: see nvs_store.h.
#include "nvs_store.h"

#include <Preferences.h>

namespace hw {

namespace {
constexpr const char *kNamespace = "modem";
constexpr const char *kKey = "cfg";
}  // namespace

size_t NvsStore::load(uint8_t *buf, size_t cap) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return 0;
    }
    size_t len = prefs.isKey(kKey) ? prefs.getBytesLength(kKey) : 0;
    if (len > cap) {
        len = cap;  // too big to be ours; modemcfg reports it as damaged
    }
    if (len > 0) {
        len = prefs.getBytes(kKey, buf, len);
    }
    prefs.end();
    return len;
}

bool NvsStore::save(const uint8_t *data, size_t len) {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    bool ok = prefs.putBytes(kKey, data, len) == len;
    prefs.end();
    return ok;
}

bool NvsStore::erase() {
    Preferences prefs;
    if (!prefs.begin(kNamespace, false)) {
        return false;
    }
    bool ok = prefs.clear();
    prefs.end();
    return ok;
}

}  // namespace hw
