#include "wav.h"
#include <cstring>
#include "catalog.h"

namespace sounds {
namespace {
uint32_t u32(const uint8_t *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint16_t u16(const uint8_t *p) {
    return p[0] | uint16_t(p[1]) << 8;
}
}  // namespace
bool inspectWav(Reader &reader, WavInfo &info) {
    info = WavInfo{};
    uint8_t header[16];
    if (!reader.seek(0) || reader.read(header, 12) != 12 || memcmp(header, "RIFF", 4) ||
        memcmp(header + 8, "WAVE", 4)) {
        return false;
    }
    uint32_t length = u32(header + 4);
    if (reader.size() < 12 || length < 4 || length > reader.size() - 8) {
        return false;
    }
    uint32_t end = length + 8, offset = 12;
    bool format = false, data = false;
    while (offset <= end && end - offset >= 8) {
        if (!reader.seek(offset) || reader.read(header, 8) != 8) {
            return false;
        }
        uint32_t size = u32(header + 4);
        offset += 8;
        if (size > end - offset) {
            return false;
        }
        if (memcmp(header, "fmt ", 4) == 0) {
            if (format || size < 16 || reader.read(header, 16) != 16) {
                return false;
            }
            if (u16(header) != 1 || u16(header + 2) != 1 || u32(header + 4) != 8000 ||
                u32(header + 8) != 16000 || u16(header + 12) != 2 || u16(header + 14) != 16) {
                return false;
            }
            format = true;
        } else if (memcmp(header, "data", 4) == 0) {
            if (data || !size || size % 2) {
                return false;
            }
            info.offset = offset;
            info.bytes = size;
            data = true;
        }
        if (format && data) {
            return true;
        }
        if (size == end - offset) {
            break;
        }
        offset += size + (size & 1);
    }
    return false;
}
uint32_t handshakeSpeed(uint32_t speed) {
    return handshakeAsset(speed).speed;
}
bool WavSource::begin() {
    WavInfo info;
    failed_ = !inspectWav(reader_, info) || !reader_.seek(info.offset);
    left_ = failed_ ? 0 : info.bytes;
    pos_ = count_ = 0;
    repeat_ = false;
    return !failed_;
}
size_t WavSource::fill(int16_t *out, size_t n) {
    size_t done = 0;
    while (done < n) {
        if (repeat_) {
            out[done++] = last_;
            repeat_ = false;
            continue;
        }
        if (pos_ == count_) {
            if (!left_) {
                break;
            }
            count_ = left_ < sizeof cache_ ? left_ : sizeof cache_;
            if (reader_.read(cache_, count_) != count_) {
                failed_ = true;
                left_ = 0;
                break;
            }
            left_ -= count_;
            pos_ = 0;
        }
        last_ = static_cast<int16_t>(u16(cache_ + pos_));
        pos_ += 2;
        out[done++] = last_;
        repeat_ = true;
    }
    return done;
}
}  // namespace sounds
