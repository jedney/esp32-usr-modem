// SSH wire encoding (RFC 4251 section 5): bounded readers and writers. Every read is
// checked; a failed read leaves the reader failed, so a message can be parsed in one go
// and checked once at the end.
//
// No Arduino dependencies: unit-tested on the PC.
#pragma once
#include <cstddef>
#include <cstdint>

namespace ssh {
// Clears memory that held a secret. Not removed by the optimiser.
void wipe(void *data, size_t size);

class Reader {
public:
    Reader(const uint8_t *data, size_t size) : data_(data), size_(size) {}
    bool byte(uint8_t &value);
    bool boolean(bool &value);
    bool u32(uint32_t &value);
    bool string(const uint8_t *&data, size_t &size);  // a view into the message
    bool skipString();
    bool skip(size_t size);
    bool ok() const { return ok_; }
    size_t left() const { return ok_ ? size_ - pos_ : 0; }

private:
    bool take(size_t n);
    const uint8_t *data_;
    size_t size_, pos_ = 0;
    bool ok_ = true;
};

class Writer {
public:
    Writer(uint8_t *data, size_t capacity) : data_(data), cap_(capacity) {}
    Writer &byte(uint8_t value);
    Writer &boolean(bool value) { return byte(value ? 1 : 0); }
    Writer &u32(uint32_t value);
    Writer &raw(const void *data, size_t size);
    Writer &string(const void *data, size_t size);
    Writer &string(const char *text);
    // An unsigned big-endian number as an mpint: leading zeros dropped, a zero byte added
    // when the top bit is set.
    Writer &mpint(const uint8_t *bigEndian, size_t size);
    bool ok() const { return ok_; }
    size_t size() const { return pos_; }
    uint8_t *data() { return data_; }

private:
    uint8_t *data_;
    size_t cap_, pos_ = 0;
    bool ok_ = true;
};

void putU32(uint8_t *p, uint32_t value);
uint32_t getU32(const uint8_t *p);

// True when the comma-separated name-list contains name exactly.
bool listHas(const uint8_t *list, size_t size, const char *name);
// RFC 4253 7.1: the first of the client's names that the server also lists, or -1.
int choose(const char *const *client, size_t count, const uint8_t *server, size_t size);
// The first name in a name-list; false when the list is empty.
bool firstName(const uint8_t *list, size_t size, const uint8_t *&name, size_t &length);
}  // namespace ssh
