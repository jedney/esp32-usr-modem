#pragma once
#include "atcmd.h"
#include "dialstring.h"

namespace calls {
constexpr size_t kPhonebookCount = 64;
enum class Kind : uint8_t { Telnet, Raw, Ppp, Slip };
inline const char *kindName(Kind kind) {
    static const char *const names[] = {"TELNET", "RAW", "PPP", "SLIP"};
    return unsigned(kind) < 4 ? names[unsigned(kind)] : "Unknown";
}
struct Entry {
    char number[12] = {};
    char host[64] = {};
    uint16_t port = 23;
    bool raw = false;
    uint32_t speed = 0;        // 0: no per-entry override
    Kind kind = Kind::Telnet;  // runtime call metadata; not a phonebook storage field
};
bool validSpeed(uint32_t speed);
class Phonebook {
public:
    static constexpr size_t kRecordSize = 12 + 64 + 2 + 1 + 4;
    static constexpr size_t kSize = 5 + kPhonebookCount * kRecordSize + 4;
    explicit Phonebook(modemcfg::Store &store) : store_(store) {}
    bool begin();  // false: damaged record; factory entry used, not saved over it
    bool loaded() const { return loaded_; }
    bool healthy() const { return healthy_; }
    const Entry *find(const char *number) const;
    const Entry &at(size_t index) const { return entries_[index]; }
    bool put(const Entry &entry);
    bool remove(const char *number);
    bool clear();
    atcmd::Reply command(atcmd::Port &port, const char *args, bool remove);

private:
    bool save();
    static bool valid(const Entry &entry);
    modemcfg::Store &store_;
    Entry entries_[kPhonebookCount] = {};
    uint8_t blob_[kSize + 1] = {};
    bool loaded_ = false, healthy_ = false;
};
}  // namespace calls
