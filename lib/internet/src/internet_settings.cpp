#include "internet_settings.h"
#include <cstring>

namespace dialup {
namespace {
bool privateIp(uint32_t ip) {
    return (ip >> 24) == 10 || (ip >> 20) == 0xac1 || (ip >> 16) == 0xc0a8;
}
void put(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = uint8_t(n >> (24 - 8 * i));
}
uint32_t read32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
}  // namespace
bool validOptions(const Options &o) {
    const unsigned a = o.local & 3, b = o.peer & 3;
    return privateIp(o.local) && (o.local & ~3u) == (o.peer & ~3u) && a != b && a && a < 3 && b &&
           b < 3 &&
           (!o.dns ||
            ((o.dns >> 24) && (o.dns >> 24) < 224 && (o.dns >> 24) != 127 && o.dns != 0xffffffff));
}
bool overlaps(const Options &o, uint32_t address, uint32_t mask) {
    return address && ((o.local & mask) == (address & mask) || (address & ~3u) == (o.local & ~3u));
}
bool Settings::begin() {
    options_ = {};
    uint8_t blob[24] = {};
    if (store_.load(blob, sizeof blob) != sizeof blob || memcmp(blob, "USRI\1\0\0\0", 8) ||
        read32(blob + 20) != modemcfg::crc32(blob, 20))
        return false;
    Options next{read32(blob + 8), read32(blob + 12), read32(blob + 16)};
    if (!validOptions(next))
        return false;
    options_ = next;
    return true;
}
bool Settings::save(const Options &o) {
    if (!validOptions(o))
        return false;
    uint8_t blob[24] = {'U', 'S', 'R', 'I', 1, 0, 0, 0};
    put(blob + 8, o.local);
    put(blob + 12, o.peer);
    put(blob + 16, o.dns);
    put(blob + 20, modemcfg::crc32(blob, 20));
    if (!store_.save(blob, sizeof blob))
        return false;
    options_ = o;
    return true;
}
}  // namespace dialup
