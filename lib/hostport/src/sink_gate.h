// How a serial port's result sink writes: a port that the computer does not read
// (RTS held off, or a program that holds the USB port open and never reads) must never
// stop the modem.
//   - Unsolicited output (RING): written whole if the port has room for it now, otherwise
//     dropped at once. It never waits.
//   - A reply: waits for room while the computer keeps taking bytes. If no room appears
//     for kStallMs, the rest is dropped and the port is marked stalled. One write never
//     takes longer than kWriteMs.
//   - While the port is stalled, every write is dropped at once, until the port has room
//     again.
// Call data does not come here: the port pulls it with Session::toHost() only when there
// is room, and flow control holds the network side.
//
// No Arduino dependencies: the port, the clock and the 1 ms pause come through Device.
#pragma once

#include <cstddef>
#include <cstdint>

namespace hostport {

class SinkGate {
public:
    static constexpr uint32_t kStallMs = 250;   // no room for this long: the port is stalled
    static constexpr uint32_t kWriteMs = 2000;  // one write never takes longer than this

    class Device {
    public:
        virtual ~Device() = default;
        virtual size_t room() = 0;  // bytes the port takes now without waiting
        virtual size_t put(const uint8_t *data, size_t len) = 0;  // returns bytes taken
        virtual uint32_t now() = 0;                               // milliseconds
        virtual void pause() = 0;                                 // wait about 1 ms
    };

    // Returns how many bytes were written.
    size_t write(Device &device, const char *data, size_t len, bool unsolicited);
    bool stalled() const { return stalled_; }

private:
    bool stalled_ = false;
};

}  // namespace hostport
