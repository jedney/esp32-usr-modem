#pragma once
#include "modemcfg.h"

namespace dialup {
// Network byte order expressed as an integer: 10.64.64.1 == 0x0a404001.
struct Options {
    uint32_t local = 0x0a404001, peer = 0x0a404002, dns = 0;
};
bool validOptions(const Options &options);
bool overlaps(const Options &options, uint32_t address, uint32_t mask);
class Settings {
public:
    explicit Settings(modemcfg::Store &store) : store_(store) {}
    bool begin();
    bool save(const Options &options);
    const Options &get() const { return options_; }

private:
    modemcfg::Store &store_;
    Options options_;
};
}  // namespace dialup
