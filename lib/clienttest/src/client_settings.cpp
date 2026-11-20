#include "client_settings.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
namespace clienttest {
const char *charsetName(Charset charset) {
    static const char *const names[] = {"ANSI", "CP437", "PLAIN", "LATIN1"};
    return unsigned(charset) < 4 ? names[unsigned(charset)] : "ANSI";
}
bool parseSettings(const char *text, ClientSettings &out) {
    char charset[8], tail;
    unsigned cols, rows, bs;
    if (sscanf(text, "%7[A-Z0-9],%u,%u,%u%c", charset, &cols, &rows, &bs, &tail) != 4 || !cols ||
        cols > 9999 || !rows || rows > 9999 || (bs != 8 && bs != 127))
        return false;
    for (unsigned i = 0; i < 4; ++i)
        if (!strcmp(charset, charsetName(Charset(i)))) {
            out = {Charset(i), uint16_t(cols), uint16_t(rows), uint8_t(bs)};
            return true;
        }
    return false;
}
const char *portName(unsigned port) {
    static const char *const names[kPortCount] = {"CONSOLE", "RS232", "USB"};
    return port < kPortCount ? names[port] : "CONSOLE";
}
namespace {
constexpr uint8_t kLayout = 1;
constexpr size_t kHeader = 6, kRecord = 8;
constexpr size_t kBody = kHeader + kPortCount * kRecord, kBlob = kBody + 4;
}  // namespace
bool Settings::begin() {
    uint8_t blob[kBlob] = {};
    std::fill(std::begin(records_), std::end(records_), ClientSettings{});
    if (store_.load(blob, sizeof blob) != sizeof blob || memcmp(blob, "USRC", 4) ||
        blob[4] != kLayout ||
        modemcfg::crc32(blob, kBody) !=
            (uint32_t(blob[kBody]) | uint32_t(blob[kBody + 1]) << 8 |
             uint32_t(blob[kBody + 2]) << 16 | uint32_t(blob[kBody + 3]) << 24))
        return false;
    for (unsigned i = 0; i < kPortCount; ++i) {
        const uint8_t *p = blob + kHeader + i * kRecord;
        if (p[0] > 3 || (p[5] != 8 && p[5] != 127))
            return false;
        unsigned cols = p[1] | p[2] << 8, rows = p[3] | p[4] << 8;
        if (!cols || cols > 9999 || !rows || rows > 9999)
            return false;
    }
    for (unsigned i = 0; i < kPortCount; ++i) {
        const uint8_t *p = blob + kHeader + i * kRecord;
        records_[i] = {Charset(p[0]), uint16_t(p[1] | p[2] << 8), uint16_t(p[3] | p[4] << 8), p[5]};
    }
    return true;
}
bool Settings::save(unsigned port, const ClientSettings &settings) {
    if (port >= kPortCount || unsigned(settings.charset) > 3 || !settings.columns ||
        settings.columns > 9999 || !settings.rows || settings.rows > 9999 ||
        (settings.backspace != 8 && settings.backspace != 127))
        return false;
    ClientSettings next[kPortCount];
    for (unsigned i = 0; i < kPortCount; ++i)
        next[i] = records_[i];
    next[port] = settings;
    uint8_t blob[kBlob] = {'U', 'S', 'R', 'C', kLayout, 0};
    for (unsigned i = 0; i < kPortCount; ++i) {
        auto &s = next[i];
        uint8_t *p = blob + kHeader + i * kRecord;
        p[0] = uint8_t(s.charset);
        p[1] = s.columns;
        p[2] = s.columns >> 8;
        p[3] = s.rows;
        p[4] = s.rows >> 8;
        p[5] = s.backspace;
    }
    uint32_t crc = modemcfg::crc32(blob, kBody);
    for (unsigned i = 0; i < 4; ++i)
        blob[kBody + i] = crc >> (i * 8);
    if (!store_.save(blob, sizeof blob))
        return false;
    records_[port] = settings;
    return true;
}
}  // namespace clienttest
