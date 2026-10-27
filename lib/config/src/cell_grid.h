// Bounded, single-cell glyph repertoire. Never stores untrusted terminal controls.
#pragma once
#include <cstddef>
#include <cstdint>

namespace config {
enum class Ink : uint8_t {
    Body,
    Frame,
    Logo,
    Title,
    Key,
    Function,
    Disabled,
    Muted,
    Warning,
    Error,
    Model
};
enum class Color : uint8_t { Ansi16, Ansi256, Rgb };
class CellGrid {
public:
    static constexpr unsigned kMaxCols = 132, kMaxRows = 50;
    void clear(unsigned cols, unsigned rows);
    void text(unsigned x, unsigned y, const char *text, Ink ink = Ink::Body,
              unsigned width = kMaxCols);
    void center(unsigned y, const char *text, Ink ink);
    void put(unsigned x, unsigned y, uint8_t glyph, Ink ink);
    void rule(unsigned y, const char *title = nullptr);
    void box(unsigned x, unsigned y, unsigned width, unsigned height);
    // A row is emitted atomically into a small staging buffer, then pulled in
    // arbitrary UART chunks. Cursor addressing avoids CR/LF and delayed auto-wrap.
    size_t row(unsigned y, char *out, size_t cap, bool cp437, bool plain, Color color,
               unsigned modelRgb) const;
    unsigned cols() const { return cols_; }
    unsigned rows() const { return rows_; }
    static const char *modelLabel(unsigned speed);
    static const char *modelShort(unsigned speed);
    static unsigned modelRgb(unsigned speed);

private:
    struct Cell {
        uint8_t glyph;
        Ink ink;
    };
    Cell cells_[kMaxCols * kMaxRows] = {};
    unsigned cols_ = 80, rows_ = 25;
};
}  // namespace config
