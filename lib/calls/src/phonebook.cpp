#include "phonebook.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iterator>

namespace calls {
namespace {
void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i) {
        p[i] = n >> (i * 8);
    }
}
uint32_t get32(const uint8_t *p) {
    uint32_t n = 0;
    for (unsigned i = 0; i < 4; ++i) {
        n |= uint32_t(p[i]) << (i * 8);
    }
    return n;
}
bool decimal(const char *text, uint32_t max, uint32_t &number) {
    number = 0;
    if (!*text) {
        return false;
    }
    for (; *text; ++text) {
        if (*text < '0' || *text > '9') {
            return false;
        }
        number = number * 10 + (*text - '0');
        if (number > max) {
            return false;
        }
    }
    return true;
}
}  // namespace
bool validSpeed(uint32_t speed) {
    if (speed == 0) {
        return true;
    }
    for (unsigned n = 1; n <= modemcfg::kMaxLineSpeedIndex; ++n) {
        if (atcmd::lineSpeedForN(n) == speed) {
            return true;
        }
    }
    return false;
}
bool Phonebook::valid(const Entry &e) {
    if (!memchr(e.number, 0, sizeof e.number) || !memchr(e.host, 0, sizeof e.host)) {
        return false;
    }
    dialstring::Result key = dialstring::classify(e.number);
    if (key.kind != dialstring::Kind::Phonebook || strcmp(key.number, e.number) != 0) {
        return false;
    }
    char quoted[68];
    snprintf(quoted, sizeof quoted, "\"%s\"", e.host);
    dialstring::Result host = dialstring::classify(quoted);
    return host.kind == dialstring::Kind::Host && strcmp(host.host, e.host) == 0 && e.port != 0 &&
           unsigned(e.protocol) < kProtocolCount && validSpeed(e.speed);
}
bool Phonebook::begin() {
    loaded_ = true;
    healthy_ = false;
    std::fill(std::begin(entries_), std::end(entries_), Entry{});
    size_t size = store_.load(blob_, sizeof blob_);
    if (size) {
        if (size < 9 || size > kSize || (size - 9) % kRecordSize != 0 ||
            memcmp(blob_, "USRP\1", 5) != 0 ||
            modemcfg::crc32(blob_, size - 4) != get32(blob_ + size - 4)) {
            size = 1;
        } else {
            const uint8_t *p = blob_ + 5;
            bool ok = true;
            for (size_t i = 0; i < (size - 9) / kRecordSize; ++i) {
                Entry &e = entries_[i];
                memcpy(e.number, p, 12);
                p += 12;
                memcpy(e.host, p, 64);
                p += 64;
                e.port = p[0] | (uint16_t(p[1]) << 8);
                p += 2;
                if (*p >= kProtocolCount) {
                    ok = false;
                }
                e.protocol = static_cast<Protocol>(*p++);
                e.speed = get32(p);
                p += 4;
                if (*e.number && !valid(e)) {
                    ok = false;
                }
                if (*e.number) {
                    for (size_t j = 0; j < i; ++j) {
                        if (memcmp(entries_[j].number, e.number, 12) == 0) {
                            ok = false;
                        }
                    }
                }
            }
            if (ok) {
                healthy_ = true;
                return true;
            }
        }
        std::fill(std::begin(entries_), std::end(entries_), Entry{});
    }
    strcpy(entries_[0].number, "1");
    strcpy(entries_[0].host, "www.amigaretro.com");
    healthy_ = size == 0 && save();
    return healthy_;
}
const Entry *Phonebook::find(const char *number) const {
    for (const auto &e : entries_) {
        if (*e.number && strcmp(e.number, number) == 0) {
            return &e;
        }
    }
    return nullptr;
}
bool Phonebook::save() {
    memcpy(blob_, "USRP\1", 5);
    uint8_t *p = blob_ + 5;
    for (const auto &e : entries_) {
        if (!*e.number) {
            continue;  // NVS must have space for both old and replacement blobs.
        }
        memcpy(p, e.number, 12);
        p += 12;
        memcpy(p, e.host, 64);
        p += 64;
        *p++ = e.port & 255;
        *p++ = e.port >> 8;
        *p++ = static_cast<uint8_t>(e.protocol);
        put32(p, e.speed);
        p += 4;
    }
    size_t used = static_cast<size_t>(p - blob_);
    put32(p, modemcfg::crc32(blob_, used));
    return store_.save(blob_, used + 4);
}
bool Phonebook::put(const Entry &entry) {
    if (!healthy_ || !valid(entry)) {  // a damaged phonebook is never saved over
        return false;
    }
    Entry *slot = nullptr;
    for (auto &e : entries_) {
        if (strcmp(e.number, entry.number) == 0) {
            slot = &e;
            break;
        }
    }
    if (!slot) {
        for (auto &e : entries_) {
            if (!*e.number) {
                slot = &e;
                break;
            }
        }
    }
    if (!slot) {
        return false;
    }
    Entry before = *slot;
    *slot = entry;
    if (save()) {
        return true;
    }
    *slot = before;
    return false;
}
bool Phonebook::remove(const char *number) {
    if (!healthy_) {
        return false;
    }
    for (auto &e : entries_) {
        if (*e.number && strcmp(e.number, number) == 0) {
            Entry before = e;
            e = Entry{};
            if (save()) {
                return true;
            }
            e = before;
            return false;
        }
    }
    return false;
}
bool Phonebook::clear() {
    memcpy(blob_, "USRP\1", 5);
    put32(blob_ + 5, modemcfg::crc32(blob_, 5));
    if (!store_.save(blob_, 9)) {
        return false;
    }
    std::fill(std::begin(entries_), std::end(entries_), Entry{});
    healthy_ = true;
    return true;
}
atcmd::Reply Phonebook::command(atcmd::Port &port, const char *args, bool deleting) {
    using atcmd::Reply;
    if (!deleting && (*args == 0 || strcmp(args, "?") == 0)) {
        port.beginInfo();
        for (const auto &e : entries_) {
            if (*e.number) {
                port.printf("%s,%s,%u,%s,%u", e.number, e.host, unsigned(e.port),
                            protocolName(e.protocol), unsigned(e.speed));
                port.newline();
            }
        }
        return Reply::kOk;
    }
    if (*args++ != '=') {
        return Reply::kError;
    }
    if (!healthy_) {
        port.beginInfo();
        port.printf("Phonebook is damaged and is not changed. AT*BOOKCLEAR erases it.");
        port.newline();
        return Reply::kError;
    }
    if (deleting) {
        return remove(args) ? Reply::kOk : Reply::kError;
    }
    if (strlen(args) > atcmd::kLineMax) {
        return Reply::kError;
    }
    char text[atcmd::kLineMax + 1];
    strcpy(text, args);
    char *fields[6] = {};
    size_t count = 0;
    char *field = text;
    for (;;) {
        if (count == 6) {
            return Reply::kError;
        }
        fields[count++] = field;
        char *comma = strchr(field, ',');
        if (!comma) {
            break;
        }
        *comma = 0;
        field = comma + 1;
    }
    if (count != 4 && count != 5) {
        return Reply::kError;
    }
    Entry e;
    if (strlen(fields[0]) >= sizeof e.number || strlen(fields[1]) >= sizeof e.host) {
        return Reply::kError;
    }
    strcpy(e.number, fields[0]);
    strcpy(e.host, fields[1]);
    uint32_t value;
    if (!decimal(fields[2], 65535, value)) {
        return Reply::kError;
    }
    e.port = value;
    for (char *p = fields[3]; *p; ++p) {
        if (*p >= 'a' && *p <= 'z') {
            *p -= 'a' - 'A';
        }
    }
    bool known = false;
    for (unsigned i = 0; i < kProtocolCount && !known; ++i) {
        if (strcmp(fields[3], protocolName(static_cast<Protocol>(i))) == 0) {
            e.protocol = static_cast<Protocol>(i);
            known = true;
        }
    }
    if (!known) {
        return Reply::kError;
    }
    if (count == 5 && !decimal(fields[4], 56000, e.speed)) {
        return Reply::kError;
    }
    return put(e) ? Reply::kOk : Reply::kError;
}
}  // namespace calls
