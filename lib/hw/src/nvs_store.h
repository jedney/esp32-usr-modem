// Saved modem settings in NVS (namespace "modem", one blob). See modemcfg.h for the
// format.

#pragma once

#include "modemcfg.h"

namespace hw {

class NvsStore : public modemcfg::Store {
public:
    size_t load(uint8_t *buf, size_t cap) override;
    bool save(const uint8_t *data, size_t len) override;
    bool erase() override;
};

}  // namespace hw
