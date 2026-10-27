#pragma once
#include "modemcfg.h"

namespace clienttest {
enum class Charset : uint8_t { Ansi, Cp437, Plain, Latin1 };
struct ClientSettings {
    Charset charset = Charset::Ansi;
    uint16_t columns = 80, rows = 25;
    uint8_t backspace = 8;
};
const char *charsetName(Charset charset);
bool parseSettings(const char *text, ClientSettings &out);
class Settings {
public:
    explicit Settings(modemcfg::Store &store) : store_(store) {}
    bool begin();
    const ClientSettings &get(unsigned port) const { return records_[port == 1]; }
    bool save(unsigned port, const ClientSettings &settings);

private:
    modemcfg::Store &store_;
    ClientSettings records_[2];
};
}  // namespace clienttest
