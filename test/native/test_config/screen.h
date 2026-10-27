// Small terminal emulator for the production byte stream, including fragmented
// CSI, SGR, delayed wrap, cursor addressing and the physical lower-right cell.
#pragma once
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

class ScreenView {
public:
    explicit ScreenView(unsigned columns = 80, unsigned rows = 25)
        : width(columns), height(rows), cells(columns * rows, ' '), colors(columns * rows, 0) {}
    unsigned width, height, outside = 0, scrolls = 0;
    bool wrap = true, cursor = true;
    void accept(const std::string &bytes) {
        for (unsigned char c : bytes)
            accept(c);
    }
    void accept(unsigned char c) {
        if (c == 24) {
            escape.clear();
            return;
        }
        if (c == 27) {
            escape = "\x1b";
            return;
        }
        if (!escape.empty()) {
            escape += c;
            if (escape.size() == 2 && c != '[') {
                escape.clear();
                return;
            }
            if (escape.size() > 2 && c >= '@' && c <= '~') {
                csi();
                escape.clear();
            }
            return;
        }
        if (c == '\r') {
            x = 0;
            pending = false;
            return;
        }
        if (c == '\n') {
            newline();
            return;
        }
        if (c < 32 || c == 127)
            return;
        if (pending) {
            x = 0;
            newline();
            pending = false;
        }
        if (x >= width || y >= height) {
            ++outside;
            return;
        }
        cells[y * width + x] = c;
        colors[y * width + x] = foreground;
        if (x + 1 < width)
            ++x;
        else
            pending = wrap;
    }
    std::string row(unsigned r) const {
        return std::string(cells.begin() + r * width, cells.begin() + (r + 1) * width);
    }
    std::string text() const {
        std::string result;
        for (unsigned r = 0; r < height; ++r)
            result += row(r) + '\n';
        return result;
    }
    unsigned color(unsigned c, unsigned r) const { return colors[r * width + c]; }
    bool has(const std::string &s) const { return text().find(s) != std::string::npos; }
    void snapshot(const char *name) const {
        if (!std::getenv("USR_UI_SNAPSHOTS"))
            return;
        std::string path = ".pio/ui-snapshots/" + std::string(name) + ".cells";
        auto *file = std::fopen(path.c_str(), "wb");
        if (!file)
            return;
        std::fprintf(file, "%u %u\n", width, height);
        for (size_t i = 0; i < cells.size(); ++i) {
            unsigned char bytes[] = {
                static_cast<unsigned char>(cells[i]), static_cast<unsigned char>(colors[i] >> 16),
                static_cast<unsigned char>(colors[i] >> 8), static_cast<unsigned char>(colors[i])};
            std::fwrite(bytes, 1, sizeof bytes, file);
        }
        std::fclose(file);
    }

private:
    std::vector<char> cells;
    std::vector<unsigned> colors;
    unsigned x = 0, y = 0, foreground = 0;
    bool pending = false;
    std::string escape;
    void newline() {
        if (++y >= height) {
            ++scrolls;
            y = height - 1;
        }
    }
    void csi() {
        char op = escape.back();
        std::string parameters = escape.substr(2, escape.size() - 3);
        bool mode = !parameters.empty() && parameters[0] == '?';
        if (mode)
            parameters.erase(0, 1);
        std::vector<unsigned> p;
        size_t start = 0;
        do {
            size_t end = parameters.find(';', start);
            p.push_back(std::strtoul(parameters.substr(start, end - start).c_str(), nullptr, 10));
            if (end == std::string::npos)
                break;
            start = end + 1;
        } while (true);
        if (op == 'H' || op == 'f') {
            y = p[0] ? p[0] - 1 : 0;
            x = p.size() > 1 && p[1] ? p[1] - 1 : 0;
            if (y >= height || x >= width)
                ++outside;
            pending = false;
        } else if (op == 'J' && p[0] == 2) {
            std::fill(cells.begin(), cells.end(), ' ');
            std::fill(colors.begin(), colors.end(), foreground);
        } else if (mode && (op == 'h' || op == 'l')) {
            if (p[0] == 7) {
                wrap = op == 'h';
                if (!wrap)
                    pending = false;
            }
            if (p[0] == 25)
                cursor = op == 'h';
        } else if (op == 'm') {
            for (size_t i = 0; i < p.size(); ++i) {
                if (p[i] == 0)
                    foreground = 0;
                else if (p[i] == 38 && i + 4 < p.size() && p[i + 1] == 2) {
                    foreground = (p[i + 2] << 16) | (p[i + 3] << 8) | p[i + 4];
                    i += 4;
                }
            }
        }
    }
};
