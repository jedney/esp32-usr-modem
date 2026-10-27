#include "client_settings.h"
#include <cstdio>
#include <cstring>
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
bool Settings::begin() {
    uint8_t blob[26] = {};
    records_[0] = records_[1] = {};
    if (store_.load(blob, sizeof blob) != sizeof blob || memcmp(blob, "USRC\1", 5) ||
        modemcfg::crc32(blob, 22) != (uint32_t(blob[22]) | uint32_t(blob[23]) << 8 |
                                      uint32_t(blob[24]) << 16 | uint32_t(blob[25]) << 24))
        return false;
    for (unsigned i = 0; i < 2; ++i) {
        const uint8_t *p = blob + 6 + i * 8;
        if (p[0] > 3 || (p[5] != 8 && p[5] != 127))
            return false;
        unsigned cols = p[1] | p[2] << 8, rows = p[3] | p[4] << 8;
        if (!cols || cols > 9999 || !rows || rows > 9999)
            return false;
    }
    for (unsigned i = 0; i < 2; ++i) {
        const uint8_t *p = blob + 6 + i * 8;
        records_[i] = {Charset(p[0]), uint16_t(p[1] | p[2] << 8), uint16_t(p[3] | p[4] << 8), p[5]};
    }
    return true;
}
bool Settings::save(unsigned port, const ClientSettings &settings) {
    if (port > 1 || unsigned(settings.charset) > 3 || !settings.columns ||
        settings.columns > 9999 || !settings.rows || settings.rows > 9999 ||
        (settings.backspace != 8 && settings.backspace != 127))
        return false;
    ClientSettings next[2] = {records_[0], records_[1]};
    next[port] = settings;
    uint8_t blob[26] = {'U', 'S', 'R', 'C', 1, 0};
    for (unsigned i = 0; i < 2; ++i) {
        auto &s = next[i];
        uint8_t *p = blob + 6 + i * 8;
        p[0] = uint8_t(s.charset);
        p[1] = s.columns;
        p[2] = s.columns >> 8;
        p[3] = s.rows;
        p[4] = s.rows >> 8;
        p[5] = s.backspace;
    }
    uint32_t crc = modemcfg::crc32(blob, 22);
    for (unsigned i = 0; i < 4; ++i)
        blob[22 + i] = crc >> (i * 8);
    if (!store_.save(blob, sizeof blob))
        return false;
    records_[port] = settings;
    return true;
}
}  // namespace clienttest
