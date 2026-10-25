#pragma once
#include <cstddef>
#include <cstdint>

namespace calls {
template <size_t N>
class ByteQueue {
public:
    size_t size() const { return count_; }
    size_t space() const { return N - count_; }
    void clear() { head_ = count_ = 0; }
    bool put(uint8_t b) {
        if (!space()) {
            return false;
        }
        data_[(head_ + count_++) % N] = b;
        return true;
    }
    size_t peek(uint8_t *out, size_t cap) const {
        size_t n = cap < count_ ? cap : count_;
        for (size_t i = 0; i < n; ++i) {
            out[i] = data_[(head_ + i) % N];
        }
        return n;
    }
    void drop(size_t n) {
        head_ = (head_ + n) % N;
        count_ -= n;
    }
    size_t read(uint8_t *out, size_t cap) {
        size_t n = peek(out, cap);
        drop(n);
        return n;
    }

private:
    uint8_t data_[N] = {};
    size_t head_ = 0, count_ = 0;
};
}  // namespace calls
