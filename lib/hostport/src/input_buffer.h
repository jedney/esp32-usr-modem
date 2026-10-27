// Retain UART bytes when a service changes its capacity while consuming a chunk.
#pragma once
#include <cstring>
#include "session.h"

namespace hostport {
class InputBuffer {
public:
    size_t space() const { return sizeof bytes_ - size_; }
    size_t size() const { return size_; }
    void clear() {
        size_ = 0;
        wipeTail();
    }
    bool append(const uint8_t *data, size_t length) {
        if (length > space())
            return false;
        memcpy(bytes_ + size_, data, length);
        size_ += length;
        return true;
    }
    void poll(Session &session, uint32_t now) {
        size_t used = session.fromHost(bytes_, size_, now);
        size_ -= used;
        memmove(bytes_, bytes_ + used, size_);
        // Input can contain passwords; erase consumed copies too.
        wipeTail();
    }

private:
    void wipeTail() {
        volatile uint8_t *p = bytes_ + size_;
        for (size_t i = size_; i < sizeof bytes_; ++i)
            *p++ = 0;
    }
    uint8_t bytes_[512] = {};
    size_t size_ = 0;
};
}  // namespace hostport
