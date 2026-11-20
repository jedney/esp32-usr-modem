#include "diagnostics.h"
#include <cstdio>
#include <cstring>

namespace config {
void Log::begin(char *storage, size_t capacity) {
    data_ = storage;
    cap_ = storage ? capacity : 0;
    clear();
}
void Log::event(uint32_t ms, Event event) {
    static const char *const names[] = {"boot",          "wifi up",       "wifi down",
                                        "dial",          "connected",     "call ended",
                                        "configuration", "storage error", "status read",
                                        "sound preview", "network probe"};
    unsigned id = static_cast<unsigned>(event);
    if (!level || !cap_ || id >= sizeof names / sizeof names[0]) {
        return;
    }
    if (id >= static_cast<unsigned>(Event::StatusRead) && level < 2) {
        return;
    }
    char text[64];
    int n = snprintf(text, sizeof text, "%lu %s\r\n", static_cast<unsigned long>(ms), names[id]);
    for (int i = 0; i < n; ++i) {
        if (count_ == cap_) {
            head_ = (head_ + 1) % cap_;
            --count_;
        }
        data_[(head_ + count_++) % cap_] = text[i];
    }
}
char Log::at(size_t index) const {
    return index < count_ ? data_[(head_ + index) % cap_] : 0;
}
namespace {
void put(uint8_t *&p, uint64_t value, unsigned n) {
    while (n--) {
        *p++ = static_cast<uint8_t>(value);
        value >>= 8;
    }
}
uint64_t get(const uint8_t *&p, unsigned n) {
    uint64_t value = 0;
    for (unsigned i = 0; i < n; ++i) {
        value |= uint64_t(*p++) << (8 * i);
    }
    return value;
}
}  // namespace
bool History::save() {
    uint8_t *p = blob_;
    memcpy(p, "USRC", 4);
    p += 4;
    *p++ = 1;
    *p++ = static_cast<uint8_t>(count_);
    for (size_t i = 0; i < count_; ++i) {
        const auto &r = records_[i];
        put(p, static_cast<uint64_t>(r.utc), 8);
        put(p, r.duration, 4);
        put(p, r.sent, 4);
        put(p, r.received, 4);
        put(p, r.result, 2);
        *p++ = (r.incoming ? 1 : 0) | (uint8_t(r.kind) << 1);
        memcpy(p, r.host, 64);
        p += 64;
        memcpy(p, r.number, 12);
        p += 12;
    }
    size_t n = static_cast<size_t>(p - blob_);
    put(p, modemcfg::crc32(blob_, n), 4);
    return store_.save(blob_, n + 4);
}
bool History::begin() {
    count_ = 0;
    size_t n = store_.load(blob_, sizeof blob_);
    if (!n) {
        return true;
    }
    if (n < 10 || memcmp(blob_, "USRC", 4) || blob_[4] != 1 || blob_[5] > kCount ||
        n != 10 + size_t(blob_[5]) * kRecordSize) {
        return false;
    }
    const uint8_t *crc = blob_ + n - 4;
    if (get(crc, 4) != modemcfg::crc32(blob_, n - 4)) {
        return false;
    }
    const uint8_t *p = blob_ + 6;
    for (size_t i = 0; i < blob_[5]; ++i) {
        auto &r = records_[i];
        r.utc = static_cast<int64_t>(get(p, 8));
        r.duration = get(p, 4);
        r.sent = get(p, 4);
        r.received = get(p, 4);
        r.result = get(p, 2);
        if ((*p >> 1) > uint8_t(calls::Kind::Ssh)) {
            return false;
        }
        r.incoming = (*p & 1) != 0;
        r.kind = calls::Kind(*p++ >> 1);
        memcpy(r.host, p, 64);
        p += 64;
        memcpy(r.number, p, 12);
        p += 12;
        if (r.host[63] || r.number[11]) {
            return false;
        }
    }
    count_ = blob_[5];
    return true;
}
bool History::append(const CallRecord &record) {
    if (record.host[63] || record.number[11]) {
        return false;
    }
    CallRecord oldest = records_[0];
    size_t before = count_;
    if (count_ == kCount) {
        memmove(records_, records_ + 1, (kCount - 1) * sizeof(CallRecord));
        --count_;
    }
    records_[count_++] = record;
    if (save()) {
        return true;
    }
    if (before == kCount) {
        memmove(records_ + 1, records_, (kCount - 1) * sizeof(CallRecord));
        records_[0] = oldest;
    }
    count_ = before;
    return false;
}
bool History::clear() {
    if (!store_.erase()) {
        return false;
    }
    count_ = 0;
    return true;
}
}  // namespace config
