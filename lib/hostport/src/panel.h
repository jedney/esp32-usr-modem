// Front-panel LED rules (project brief 5.4). Returns the LED mask in hwtest::Led bit
// order (AA bit 0 ... ARQ bit 6, as in web/PROTOCOL.md); the board shows it with
// hw::ledsSet().
//   AA   on when S0 >= 1; flashes while a call rings (answer mode, v0.9)
// Two host ports drive CD, RD, SD and TR: RS232 and, from v2.0, the USB modem port
// (addHostPort). CS is RS232 only: USB has no CTS line.
//   CD   mirrors DCD (&C0: always on), or a call on the USB console
//   RD   modem -> computer data, SD computer -> modem data; each byte keeps the LED lit
//        for kStretchMs so single characters are visible
//   TR   mirrors DTR (&D0: always on)
//   CS   mirrors the CTS output
//   ARQ  Wi-Fi: solid when connected, blinking while connecting (v0.6)
//
// No Arduino dependencies: unit-tested on the PC.

#pragma once

#include <cstdint>

namespace hostport {

enum class WifiLed : uint8_t { kOff, kConnecting, kConnected };

struct PanelInput {
    bool autoAnswer;  // S0 >= 1
    bool ringing;
    bool cd;
    bool tr;
    bool cs;
    WifiLed wifi;
    uint32_t toComputer;    // running count of bytes sent to the computer (RD)
    uint32_t fromComputer;  // running count of bytes received from it (SD)
};

// One host port's part of the panel input.
struct HostPortState {
    bool dcd;
    bool dtr;
    uint32_t toComputer;
    uint32_t fromComputer;
};

// Adds a host port to `in`: CD and TR light for either port, and RD/SD count the bytes of
// both. The sums of running counts still change whenever either port moves data.
inline void addHostPort(PanelInput &in, const HostPortState &port) {
    in.cd = in.cd || port.dcd;
    in.tr = in.tr || port.dtr;
    in.toComputer += port.toComputer;
    in.fromComputer += port.fromComputer;
}

class Panel {
public:
    static constexpr uint32_t kStretchMs = 50;
    static constexpr uint32_t kRingFlashMs = 250;  // AA on/off while ringing
    static constexpr uint32_t kWifiBlinkMs = 500;  // ARQ on/off while connecting

    uint8_t update(const PanelInput &in, uint32_t nowMs);

private:
    bool started_ = false;
    uint32_t lastTo_ = 0;
    uint32_t lastFrom_ = 0;
    uint32_t rdSince_ = 0;
    uint32_t sdSince_ = 0;
    bool rd_ = false;
    bool sd_ = false;
};

}  // namespace hostport
