#include "console_log.h"

#include <cstring>

namespace netconsole {

void ConsoleLog::begin(uint8_t *storage, size_t size, Lock *lock) {
    lock_ = lock;
    Hold hold(lock_);
    bytes_ = storage;
    size_ = storage ? size : 0;
    total_ = 0;
}

void ConsoleLog::append(const void *data, size_t len) {
    Hold hold(lock_);
    if (!size_ || !len) {
        total_ += len;
        return;
    }
    const uint8_t *in = static_cast<const uint8_t *>(data);
    if (len > size_) {  // only the newest size_ bytes can be kept
        total_ += len - size_;
        in += len - size_;
        len = size_;
    }
    size_t at = static_cast<size_t>(total_ % size_);
    size_t first = len < size_ - at ? len : size_ - at;
    memcpy(bytes_ + at, in, first);
    memcpy(bytes_, in + first, len - first);
    total_ += len;
}

uint64_t ConsoleLog::end() const {
    Hold hold(lock_);
    return total_;
}

uint64_t ConsoleLog::start() const {
    Hold hold(lock_);
    return startLocked();
}

size_t ConsoleLog::read(uint64_t &from, uint8_t *out, size_t cap, uint64_t &skipped) const {
    Hold hold(lock_);
    skipped = 0;
    const uint64_t oldest = startLocked();
    if (from < oldest) {
        skipped = oldest - from;
        from = oldest;
    }
    if (!size_ || from >= total_) {
        from = total_;
        return 0;
    }
    uint64_t available = total_ - from;
    size_t n = available < cap ? static_cast<size_t>(available) : cap;
    size_t at = static_cast<size_t>(from % size_);
    size_t first = n < size_ - at ? n : size_ - at;
    memcpy(out, bytes_ + at, first);
    memcpy(out + first, bytes_, n - first);
    from += n;
    return n;
}

}  // namespace netconsole
