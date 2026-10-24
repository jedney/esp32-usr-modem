// The far end of a call, as the host port sees it. v0.5 has one kind: the AT*TESTCALL
// echo call. Telnet and raw TCP calls follow in v0.8, answered calls in v0.9.
//
// No Arduino dependencies: unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

namespace hostport {

class Connection {
public:
    virtual ~Connection() = default;
    virtual size_t space() const = 0;                           // bytes write() would take now
    virtual size_t write(const uint8_t *data, size_t len) = 0;  // returns bytes taken
    virtual size_t read(uint8_t *buf, size_t cap) = 0;          // returns bytes read
    virtual bool isOpen() const = 0;  // false once the far end has gone (NO CARRIER)
    virtual void close() = 0;         // hang up from this end
    virtual void poll(uint32_t nowMs) { (void)nowMs; }
};

// AT*TESTCALL: everything written comes back. Optionally the "far end" hangs up after a
// set time, to test NO CARRIER and DCD.
class EchoConnection : public Connection {
public:
    static constexpr size_t kSize = 4096;

    // dropAfterMs = 0: the call lasts until this end hangs up.
    void open(uint32_t nowMs, uint32_t dropAfterMs);

    size_t space() const override { return open_ ? kSize - count_ : 0; }
    size_t write(const uint8_t *data, size_t len) override;
    size_t read(uint8_t *buf, size_t cap) override;
    bool isOpen() const override { return open_; }
    void close() override;
    void poll(uint32_t nowMs) override;

    size_t waiting() const { return count_; }

private:
    uint8_t buf_[kSize] = {};
    size_t head_ = 0;  // next byte to read
    size_t count_ = 0;
    bool open_ = false;
    bool timed_ = false;
    uint32_t dropAt_ = 0;
};

}  // namespace hostport
