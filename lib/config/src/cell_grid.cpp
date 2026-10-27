#include "cell_grid.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include "config.h"
#include "ui_generated.h"

namespace config {
namespace {
unsigned rgb(Ink ink, unsigned model) {
    return ink == Ink::Model ? model : kUiColors[static_cast<unsigned>(ink)];
}
unsigned ansiColor(unsigned value) {
    unsigned r = value >> 16, g = (value >> 8) & 255, b = value & 255;
    if (r > 120 && r > g * 3 / 2 && r > b * 3 / 2)
        return 91;
    if (g > r * 3 / 2 && g > b)
        return 92;
    if (b > r * 3 / 2 && b > g)
        return 94;
    if (r > 100 && b > g * 3 / 2)
        return 35;
    if (r > 150 && g > 130 && b < 150)
        return 93;
    if (g > r * 3 / 2 && b > r * 3 / 2)
        return 96;
    return r + g + b > 480 ? 97 : 37;
}
uint8_t ascii(uint8_t c) {
    if (c < 128)
        return c;
    if (c == 219)
        return '#';
    if (c == 196 || c == 205)
        return '-';
    if (c == 179 || c == 186)
        return '|';
    return '+';
}
}  // namespace
void CellGrid::clear(unsigned cols, unsigned rows) {
    cols_ = std::min(kMaxCols, std::max(1u, cols));
    rows_ = std::min(kMaxRows, std::max(1u, rows));
    for (auto &cell : cells_)
        cell = {' ', Ink::Body};
}
void CellGrid::put(unsigned x, unsigned y, uint8_t glyph, Ink ink) {
    if (x < cols_ && y < rows_)
        cells_[y * kMaxCols + x] = {glyph, ink};
}
void CellGrid::text(unsigned x, unsigned y, const char *text, Ink ink, unsigned width) {
    if (!text || x >= cols_ || y >= rows_)
        return;
    unsigned end = std::min(cols_, x + std::min(width, kMaxCols));
    // All output glyphs occupy exactly one cell. Unsupported UTF-8 scalars are
    // replaced once, combining marks are omitted, and controls cannot escape.
    while (*text && x < end) {
        uint8_t c = static_cast<uint8_t>(*text++);
        if (c >= 128) {
            uint32_t cp = c;
            unsigned n = c >= 240 ? 3 : c >= 224 ? 2 : c >= 192 ? 1 : 0;
            if (n) {
                cp &= (1u << (6 - n)) - 1;
                while (n-- && (static_cast<uint8_t>(*text) & 192) == 128)
                    cp = (cp << 6) | (static_cast<uint8_t>(*text++) & 63);
            }
            if ((cp >= 0x300 && cp <= 0x36f) || cp == 0x200d || (cp >= 0xfe00 && cp <= 0xfe0f))
                continue;
            c = '?';
        }
        put(x++, y, c >= 32 && c != 127 ? c : '?', ink);
    }
}
void CellGrid::center(unsigned y, const char *text, Ink ink) {
    unsigned n = std::min<unsigned>(strlen(text), cols_ - 2);
    this->text((cols_ - n) / 2, y, text, ink, n);
}
void CellGrid::box(unsigned x, unsigned y, unsigned width, unsigned height) {
    if (width < 2 || height < 2)
        return;
    for (unsigned i = 1; i + 1 < width; ++i) {
        put(x + i, y, 205, Ink::Frame);
        put(x + i, y + height - 1, 205, Ink::Frame);
    }
    for (unsigned i = 1; i + 1 < height; ++i) {
        put(x, y + i, 186, Ink::Frame);
        put(x + width - 1, y + i, 186, Ink::Frame);
    }
    put(x, y, 201, Ink::Frame);
    put(x + width - 1, y, 187, Ink::Frame);
    put(x, y + height - 1, 200, Ink::Frame);
    put(x + width - 1, y + height - 1, 188, Ink::Frame);
}
void CellGrid::rule(unsigned y, const char *title) {
    for (unsigned x = 1; x + 1 < cols_; ++x)
        put(x, y, 205, Ink::Frame);
    put(0, y, 204, Ink::Frame);
    put(cols_ - 1, y, 185, Ink::Frame);
    if (title)
        center(y, title, Ink::Title);
}
const char *CellGrid::modelLabel(unsigned speed) {
    for (auto &m : kModelThemes)
        if (m.speed == speed)
            return m.label;
    return "Sportster model unknown";
}
const char *CellGrid::modelShort(unsigned speed) {
    for (auto &m : kModelThemes)
        if (m.speed == speed)
            return m.shortLabel;
    return "Unknown";
}
unsigned CellGrid::modelRgb(unsigned speed) {
    for (auto &m : kModelThemes)
        if (m.speed == speed)
            return m.rgb;
    return 0x8b92a2;
}
size_t CellGrid::row(unsigned y, char *out, size_t cap, bool cp437, bool plain, Color color,
                     unsigned modelRgb) const {
    if (y >= rows_ || cap < 64)
        return 0;
    size_t n = 0;
    if (!plain)
        n = snprintf(out, cap, "\x1b[%u;1H", y + 1);
    Ink previous = Ink::Model;
    bool first = true;
    for (unsigned x = 0; x < cols_; ++x) {
        const auto &cell = cells_[y * kMaxCols + x];
        if (!plain && (first || previous != cell.ink)) {
            unsigned value = rgb(cell.ink, modelRgb);
            unsigned r = value >> 16, g = (value >> 8) & 255, b = value & 255;
            char sgr[40];
            if (color == Color::Rgb)
                snprintf(sgr, sizeof sgr, "\x1b[38;2;%u;%u;%um", r, g, b);
            else if (color == Color::Ansi256)
                snprintf(sgr, sizeof sgr, "\x1b[38;5;%um",
                         16 + 36 * ((r * 5 + 127) / 255) + 6 * ((g * 5 + 127) / 255) +
                             (b * 5 + 127) / 255);
            else
                snprintf(sgr, sizeof sgr, "\x1b[%um", ansiColor(value));
            size_t len = strlen(sgr);
            if (n + len + 4 >= cap)
                return 0;
            memcpy(out + n, sgr, len);
            n += len;
            previous = cell.ink;
            first = false;
        }
        if (n + 4 >= cap)
            return 0;
        out[n++] = cp437 && !plain ? cell.glyph : ascii(cell.glyph);
    }
    if (plain && y + 1 < rows_) {
        out[n++] = '\r';
        out[n++] = '\n';
    }
    return n;
}
}  // namespace config
