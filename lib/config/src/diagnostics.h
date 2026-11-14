// Local diagnostics. Only structured, credential-free events enter this buffer.
#pragma once
#include "modemcfg.h"
#include "phonebook.h"

namespace config {
enum class Event : uint8_t {
    Boot,
    WifiUp,
    WifiDown,
    Dial,
    Connected,
    Ended,
    Menu,
    StorageError,
    StatusRead,
    SoundPreview,
    NetworkProbe
};
class Log {
public:
    void begin(char *storage, size_t capacity);
    void event(uint32_t ms, Event event);
    size_t size() const { return count_; }
    size_t capacity() const { return cap_; }
    char at(size_t index) const;
    void clear() { head_ = count_ = 0; }
    unsigned level = 1;

private:
    char *data_ = nullptr;
    size_t cap_ = 0, head_ = 0, count_ = 0;
};

struct CallRecord {
    int64_t utc = 0;
    uint32_t duration = 0, sent = 0, received = 0;
    uint16_t result = 0;
    bool incoming = false;
    calls::Kind kind = calls::Kind::Telnet;
    char host[64] = {}, number[12] = {};
};
class History {
public:
    static constexpr size_t kCount = 50;
    static constexpr size_t kRecordSize = 8 + 12 + 2 + 1 + 64 + 12;
    static constexpr size_t kBlobSize = 6 + kCount * kRecordSize + 4;
    explicit History(modemcfg::Store &store) : store_(store) {}
    bool begin();
    bool append(const CallRecord &record);
    bool clear();
    size_t size() const { return count_; }
    const CallRecord &at(size_t n) const { return records_[n]; }

private:
    bool save();
    modemcfg::Store &store_;
    CallRecord records_[kCount] = {};
    size_t count_ = 0;
    uint8_t blob_[kBlobSize + 1] = {};
};
}  // namespace config
