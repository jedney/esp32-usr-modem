// DCD, DSR and RI on the USB modem port. A CDC-ACM device has no wires for them: it sends
// the lines to the computer as a SERIAL_STATE notification on its interrupt endpoint, and
// the computer's driver shows them to programs as modem-status lines (pyserial .cd, .dsr,
// .ri).
//
// SerialStateSender decides when to send, so the rules can be unit-tested on the PC; the
// send itself is hw::usbSendSerialState(). It never waits:
//   - It sends when a line changes. A send that can't go now (endpoint busy, device not
//     configured) is tried again on the next poll, with the newest state.
//   - It sends the current state again when the computer configures the device and when a
//     program opens the port (DTR on): drivers don't keep a state that came while no
//     program had the port.
//   - An RI pulse shorter than a send still reaches the computer as RI on, then off.
#pragma once

#include <cstdint>

namespace hostport {

struct SerialState {
    bool dcd = false;
    bool dsr = false;
    bool ri = false;
};

inline bool operator==(const SerialState &a, const SerialState &b) {
    return a.dcd == b.dcd && a.dsr == b.dsr && a.ri == b.ri;
}
inline bool operator!=(const SerialState &a, const SerialState &b) {
    return !(a == b);
}

class SerialStateSender {
public:
    class Port {
    public:
        virtual ~Port() = default;
        // Sends one notification. False if it can't go now; nothing was sent then.
        virtual bool send(const SerialState &state) = 0;
    };

    // Every loop pass. `configured`: a computer has configured the device. `dtr`: a program
    // has the port open. `dcd` and `ri`: the lines as they should be now. DSR is always on
    // while the device is configured, as the RS232 port's DSR is wired on.
    void poll(Port &port, bool configured, bool dtr, bool dcd, bool ri);

    // What the computer was last sent (meaningful only while the device is configured).
    const SerialState &sent() const { return sent_; }

private:
    SerialState sent_;
    bool sentValid_ = false;  // the computer has sent_, so an equal state needn't go again
    bool dtr_ = false;
    bool ringOwed_ = false;  // RI came on and the computer hasn't been sent RI on yet
};

}  // namespace hostport
