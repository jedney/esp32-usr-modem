// AT*TESTCALL echo call: see connection.h.
#include "connection.h"

namespace hostport {

void EchoConnection::open(uint32_t nowMs, uint32_t dropAfterMs) {
    head_ = 0;
    count_ = 0;
    open_ = true;
    timed_ = dropAfterMs != 0;
    dropAt_ = nowMs + dropAfterMs;
}

size_t EchoConnection::write(const uint8_t *data, size_t len) {
    size_t n = 0;
    while (open_ && n < len && count_ < kSize) {
        buf_[(head_ + count_) % kSize] = data[n++];
        ++count_;
    }
    return n;
}

size_t EchoConnection::read(uint8_t *buf, size_t cap) {
    size_t n = 0;
    while (n < cap && count_ > 0) {
        buf[n++] = buf_[head_];
        head_ = (head_ + 1) % kSize;
        --count_;
    }
    return n;
}

void EchoConnection::close() {
    open_ = false;
    count_ = 0;
}

void EchoConnection::poll(uint32_t nowMs) {
    if (open_ && timed_ && static_cast<int32_t>(nowMs - dropAt_) >= 0) {
        close();  // the far end hangs up
    }
}

}  // namespace hostport
