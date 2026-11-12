// How a serial port's result sink writes: see sink_gate.h.
#include "sink_gate.h"

namespace hostport {

size_t SinkGate::write(Device &device, const char *data, size_t len, bool unsolicited) {
    if (len == 0) {
        return 0;
    }
    const auto *bytes = reinterpret_cast<const uint8_t *>(data);
    size_t room = device.room();
    if (unsolicited) {
        if (room < len) {
            return 0;  // whole or not at all, and never wait
        }
        stalled_ = false;
        return device.put(bytes, len);
    }
    if (stalled_ && room == 0) {
        return 0;  // the port has not taken anything since the last wait
    }
    stalled_ = false;
    size_t done = 0;
    const uint32_t start = device.now();
    uint32_t progress = start;
    for (;;) {
        const size_t want = len - done;
        const size_t taken = room ? device.put(bytes + done, room < want ? room : want) : 0;
        done += taken;
        if (done == len) {
            return done;
        }
        const uint32_t now = device.now();
        if (taken) {
            progress = now;
        } else if (now - progress >= kStallMs) {
            stalled_ = true;
            return done;
        }
        if (now - start >= kWriteMs) {
            return done;  // still reading, but too slowly for this write: drop the rest
        }
        if (!taken) {
            device.pause();
        }
        room = device.room();
    }
}

}  // namespace hostport
