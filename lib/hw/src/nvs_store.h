// A saved settings blob in NVS: the modem settings (namespace "modem", see modemcfg.h)
// and the Wi-Fi settings (namespace "net", see netcfg's net_settings.h). One key each.

#pragma once

#include "modemcfg.h"

namespace hw {

class NvsStore : public modemcfg::Store {
public:
    // Both strings must stay valid (use literals). NVS limits each to 15 characters.
    explicit NvsStore(const char *nameSpace = "modem", const char *key = "cfg")
        : ns_(nameSpace), key_(key) {}

    size_t load(uint8_t *buf, size_t cap) override;
    bool save(const uint8_t *data, size_t len) override;
    bool erase() override;  // clears the whole namespace

private:
    const char *ns_;
    const char *key_;
};

}  // namespace hw
