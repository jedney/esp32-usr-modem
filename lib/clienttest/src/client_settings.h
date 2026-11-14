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
// Local serial ports with their own client record (the browser has none).
constexpr unsigned kConsolePort = 0, kRs232Port = 1, kUsbPort = 2, kPortCount = 3;
const char *portName(unsigned port);  // "CONSOLE", "RS232", "USB"
// Saved as "USRC", layout 1, three 8-byte records, CRC-32. Any other layout loads defaults.
class Settings {
public:
    explicit Settings(modemcfg::Store &store) : store_(store) {}
    bool begin();
    const ClientSettings &get(unsigned port) const {
        return records_[port < kPortCount ? port : kConsolePort];
    }
    bool save(unsigned port, const ClientSettings &settings);

private:
    modemcfg::Store &store_;
    ClientSettings records_[kPortCount];
};
}  // namespace clienttest
