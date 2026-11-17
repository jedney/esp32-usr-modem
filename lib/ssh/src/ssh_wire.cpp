#include "ssh_wire.h"
#include <cstring>

namespace ssh {
void wipe(void *data, size_t size) {
    volatile uint8_t *p = static_cast<volatile uint8_t *>(data);
    while (size--) {
        *p++ = 0;
    }
}
void putU32(uint8_t *p, uint32_t value) {
    p[0] = value >> 24;
    p[1] = value >> 16;
    p[2] = value >> 8;
    p[3] = value;
}
uint32_t getU32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}

bool Reader::take(size_t n) {
    if (!ok_ || size_ - pos_ < n) {
        ok_ = false;
        return false;
    }
    return true;
}
bool Reader::byte(uint8_t &value) {
    if (!take(1)) {
        return false;
    }
    value = data_[pos_++];
    return true;
}
bool Reader::boolean(bool &value) {
    uint8_t b = 0;
    if (!byte(b)) {
        return false;
    }
    value = b != 0;
    return true;
}
bool Reader::u32(uint32_t &value) {
    if (!take(4)) {
        return false;
    }
    value = getU32(data_ + pos_);
    pos_ += 4;
    return true;
}
bool Reader::string(const uint8_t *&data, size_t &size) {
    uint32_t n = 0;
    if (!u32(n) || !take(n)) {
        return false;
    }
    data = data_ + pos_;
    size = n;
    pos_ += n;
    return true;
}
bool Reader::skip(size_t size) {
    if (!take(size)) {
        return false;
    }
    pos_ += size;
    return true;
}
bool Reader::skipString() {
    const uint8_t *data;
    size_t size;
    return string(data, size);
}

Writer &Writer::raw(const void *data, size_t size) {
    if (!ok_ || cap_ - pos_ < size) {
        ok_ = false;
        return *this;
    }
    if (size) {
        memcpy(data_ + pos_, data, size);
    }
    pos_ += size;
    return *this;
}
Writer &Writer::byte(uint8_t value) {
    return raw(&value, 1);
}
Writer &Writer::u32(uint32_t value) {
    uint8_t b[4];
    putU32(b, value);
    return raw(b, 4);
}
Writer &Writer::string(const void *data, size_t size) {
    return u32(uint32_t(size)).raw(data, size);
}
Writer &Writer::string(const char *text) {
    return string(text, strlen(text));
}
Writer &Writer::mpint(const uint8_t *bigEndian, size_t size) {
    while (size && *bigEndian == 0) {
        ++bigEndian;
        --size;
    }
    const bool pad = size && (*bigEndian & 0x80);
    u32(uint32_t(size + (pad ? 1 : 0)));
    if (pad) {
        byte(0);
    }
    return raw(bigEndian, size);
}

bool listHas(const uint8_t *list, size_t size, const char *name) {
    const size_t n = strlen(name);
    size_t start = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || list[i] == ',') {
            if (i - start == n && memcmp(list + start, name, n) == 0) {
                return true;
            }
            start = i + 1;
        }
    }
    return false;
}
int choose(const char *const *client, size_t count, const uint8_t *server, size_t size) {
    for (size_t i = 0; i < count; ++i) {
        if (listHas(server, size, client[i])) {
            return int(i);
        }
    }
    return -1;
}
bool firstName(const uint8_t *list, size_t size, const uint8_t *&name, size_t &length) {
    size_t i = 0;
    while (i < size && list[i] != ',') {
        ++i;
    }
    name = list;
    length = i;
    return i != 0;
}
}  // namespace ssh
