// DCD, DSR and RI on the USB modem port: see serial_state.h.
#include "serial_state.h"

namespace hostport {

void SerialStateSender::poll(Port &port, bool configured, bool dtr, bool dcd, bool ri) {
    if (!configured) {
        sentValid_ = false;  // send again once a computer configures the device
        dtr_ = false;
        ringOwed_ = false;
        return;
    }
    if (dtr && !dtr_) {
        sentValid_ = false;  // a program opened the port
    }
    dtr_ = dtr;
    if (ri && !(sentValid_ && sent_.ri)) {
        ringOwed_ = true;
    }
    SerialState want;
    want.dcd = dcd;
    want.dsr = true;
    want.ri = ri || ringOwed_;
    if (sentValid_ && want == sent_) {
        return;
    }
    if (port.send(want)) {
        sent_ = want;
        sentValid_ = true;
        if (want.ri) {
            ringOwed_ = false;
        }
    }
}

}  // namespace hostport
