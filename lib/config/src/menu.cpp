#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "config.h"
#include "ui_generated.h"

namespace config {
namespace {
using S = Screen;
constexpr unsigned kUp = 256, kDown = 257, kLeft = 258, kRight = 259, kPageUp = 260,
                   kPageDown = 261, kHome = 262, kEnd = 263, kDelete = 264, kF1 = 271, kF2 = 272,
                   kF3 = 273, kF4 = 274, kF10 = 280;
constexpr const char *kKeys = "123456789ABCDEF";
const Action *findAction(const char *id) {
    for (const auto &a : kActions)
        if (!strcmp(a.id, id))
            return &a;
    return nullptr;
}
bool same(const char *a, const char *b) {
    while (*a && *b &&
           std::toupper(static_cast<unsigned char>(*a)) ==
               std::toupper(static_cast<unsigned char>(*b))) {
        ++a;
        ++b;
    }
    return !*a && !*b;
}
bool confirmation(const Action &a, bool browser, const char *command) {
    // The network service owns the browser's disconnect warning and timeout.
    // Serial uses the UI confirmation because its AT handler has no prompt.
    if (browser && (!strcmp(a.command, "*WIFIFORGET") || same(command, "*WIFI=OFF")))
        return false;
    if (a.secret || !strcmp(a.command, "*LOG=CLEAR"))
        return true;
    if (a.screen == S::Settings)
        return *a.command && strcmp(a.command, "*SETTING?") && strcmp(a.command, "*STYLE=");
    if (a.screen == S::Phonebook)
        return strcmp(a.command, "*PHONEBOOK?") != 0;
    if (a.screen == S::Updates)
        return a.input;
    if (a.screen == S::Wifi)
        return !strcmp(a.command, "*WIFIJOIN=") || !strcmp(a.command, "*WIFI=") ||
               !strcmp(a.command, "*WIFIFORGET") || !strcmp(a.command, "*WIFIIP=") ||
               !strcmp(a.command, "*HOSTNAME=") || !strcmp(a.command, "*TZ=") ||
               !strcmp(a.command, "*NTP=");
    return false;
}
uint32_t entryHash(const calls::Entry &e) {
    // Compare semantic fields, never struct padding. A changed entry must be
    // reviewed again before a destructive operation or external connection.
    char text[144];
    snprintf(text, sizeof text, "%s,%s,%u,%u,%lu", e.number, e.host, e.port, e.raw,
             static_cast<unsigned long>(e.speed));
    return modemcfg::crc32(reinterpret_cast<const uint8_t *>(text), strlen(text));
}
}  // namespace
const Action *actions(size_t &count) {
    count = sizeof kActions / sizeof kActions[0];
    return kActions;
}
const char *screenName(Screen screen) {
    static const char *const names[] = {
        "Configuration",     "Settings", "Profile", "Phonebook",      "Wi-Fi and Clock",
        "Updates and Reset", "Sounds",   "Debug",   "Hardware Tests", "Client Tests"};
    auto i = static_cast<unsigned>(screen);
    return i < sizeof names / sizeof names[0] ? names[i] : "Unknown";
}
bool Terminal::open() {
    if (open_)
        return false;
    open_ = true;
    querySize_ = !session_.port().browser() && style_ != Style::Plain;
    screen_ = S::Main;
    depth_ = 0;
    for (auto &location : locations_)
        location = {};
    focus_ = Focus::Menu;
    capturing_ = response_ = more_ = skipLf_ = departure_ = liveStatus_ = exporting_ = false;
    clearOutput();
    clearInput();
    notice_[0] = selectedNumber_[0] = 0;
    escapeLen_ = 0;
    escapeDiscard_ = false;
    swallowEnter_ = false;
    drawing_ = false;
    styled_ = false;
    stagePos_ = stageLen_ = 0;
    model_ = session_.port().modem().model();
    activityAt_ = now_;
    render();
    return true;
}
void Terminal::clearInput() {
    devicecfg::wipe(input_, sizeof input_);
    devicecfg::wipe(pending_, sizeof pending_);
    inputLen_ = cursor_ = 0;
    inputOverflow_ = false;
    prompt_ = nullptr;
    confirmedNumber_[0] = 0;
}
void Terminal::clearOutput() {
    devicecfg::wipe(output_, sizeof output_);
    length_ = pos_ = 0;
    lines_ = column_ = 0;
    responseLine_ = 0;
    responseLines_ = 0;
    more_ = overflow_ = false;
}
void Terminal::line(const char *text) {
    append(text);
    append("\r\n");
}
void Terminal::append(const char *text) {
    write(text, strlen(text));
}
void Terminal::write(const char *data, size_t len) {
    // This sink accepts only untrusted service output. Rendering escapes have a
    // separate path and cannot be injected through names, logs or command results.
    for (size_t i = 0; i < len; ++i) {
        if (length_ >= sizeof output_ - 128) {
            overflow_ = true;
            break;
        }
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c == '\r')
            continue;
        if (c == '\n') {
            column_ = 0;
            ++responseLines_;
        } else {
            if (c < 32 || c == 127 || (c >= 128 && style_ != Style::Cp437))
                c = '?';
            if (!exporting_ && column_ >= cols_ - 4) {
                output_[length_++] = '\n';
                column_ = 0;
                ++responseLines_;
            }
            ++column_;
        }
        output_[length_++] = c;
    }
    if (capturing_ && !exporting_)
        responseLine_ = responseLines_ >= visibleItems() ? responseLines_ - visibleItems() + 1 : 0;
    if (!capturing_ && !response_)
        response_ = true;
    render();
}
bool Terminal::resize(unsigned columns, unsigned rows) {
    if (columns < 1 || columns > CellGrid::kMaxCols || rows < 1 || rows > CellGrid::kMaxRows)
        return false;
    if (cols_ != columns || rows_ != rows) {
        cols_ = columns;
        rows_ = rows;
        // Abort an old-size frame even when the last UART chunk ended inside a
        // CSI. CAN is a terminal parser cancellation, followed by a fresh frame.
        if (open_ && !exporting_) {
            drawing_ = false;
            stagePos_ = 0;
            const char *abort =
                style_ != Style::Plain || styled_ ? "\x18\x1b[0m\x1b[?7h\x1b[?25h" : "";
            stageLen_ = strlen(abort);
            memcpy(staging_, abort, stageLen_);
        }
        render();
    }
    return true;
}
bool Terminal::setStyle(const char *text) {
    if (!strcmp(text, "ANSI"))
        style_ = Style::Ansi;
    else if (!strcmp(text, "CP437"))
        style_ = Style::Cp437;
    else if (!strcmp(text, "PLAIN"))
        style_ = Style::Plain;
    else
        return false;
    render();
    return true;
}
bool Terminal::setSize(const char *text) {
    auto number = [&](unsigned &value, unsigned maximum) {
        value = 0;
        bool digit = false;
        while (*text >= '0' && *text <= '9') {
            digit = true;
            value = value * 10 + *text++ - '0';
            if (value > maximum)
                return false;
        }
        return digit;
    };
    unsigned cols, rows;
    return number(cols, CellGrid::kMaxCols) && *text++ == ',' && number(rows, CellGrid::kMaxRows) &&
           !*text && resize(cols, rows);
}
void Terminal::close() {
    if (diagnostic_) {
        diagnostic_->close();
        diagnostic_ = nullptr;
    }
    if (!open_)
        return;
    if (capturing_)
        session_.port().cancelPrompt();
    session_.port().redirect(nullptr);
    // CAN also terminates a partially transmitted escape sequence on abrupt
    // disconnect/DTR drop. Restore the terminal even when Session stops pulling.
    if (styled_)
        session_.port().print("\x18\x1b[0m\x1b[?7h\x1b[?25h\r\n");
    styled_ = false;
    open_ = capturing_ = drawing_ = dirty_ = false;
    querySize_ = false;
    stageLen_ = stagePos_ = 0;
    escapeLen_ = 0;
    clearInput();
    clearOutput();
}
void Terminal::escaped() {
    if (diagnostic_) {
        diagnostic_->close();
        diagnostic_ = nullptr;
    }
    if (capturing_) {
        session_.port().cancelPrompt();
        session_.port().redirect(nullptr);
        capturing_ = false;
    }
    if (styled_)
        session_.port().print("\x18\x1b[0m\x1b[?7h\x1b[?25h\r\n");
    styled_ = false;
    drawing_ = false;
    stageLen_ = stagePos_ = 0;
    clearInput();
    clearOutput();
    response_ = liveStatus_ = exporting_ = false;
    focus_ = Focus::Menu;
    render();
}
bool Terminal::takeDeparture(char *command, size_t cap) {
    if (!departure_ || open_ || strlen(departureCommand_) >= cap)
        return false;
    strcpy(command, departureCommand_);
    devicecfg::wipe(departureCommand_, sizeof departureCommand_);
    departure_ = false;
    return true;
}
void Terminal::feedback(const char *text) {
    snprintf(notice_, sizeof notice_, "%s", text);
    render();
}
void Terminal::render() {
    if (open_)
        dirty_ = true;
}
size_t Terminal::visibleItems() const {
    return std::min(15, std::max(1, int(rows_) - 10));
}
size_t Terminal::phonebookPageSize() const {
    return rows_ >= 25 ? 9 : std::min(9, std::max(1, int(rows_) - 12));
}
const Action *Terminal::item(size_t index) const {
    for (const auto &a : kActions)
        if (a.screen == screen_ && index-- == 0)
            return &a;
    return nullptr;
}
size_t Terminal::itemCount() const {
    if (screen_ == S::Profile)
        return modemcfg::kSettingCount + modemcfg::kSRegCount;
    size_t n = 0;
    for (const auto &a : kActions)
        if (a.screen == screen_)
            ++n;
    return n;
}
bool Terminal::enabled(size_t index) const {
    if (screen_ == S::Profile)
        return index != modemcfg::kSettingCount + 1;
    auto *a = item(index);
    return a && a->available && !(rs232_ && a->rs232Unavailable);
}
void Terminal::changeScreen(Screen screen) {
    if (screen_ != screen && depth_ < sizeof stack_ / sizeof stack_[0])
        stack_[depth_++] = screen_;
    screen_ = screen;
    focus_ = Focus::Menu;
    response_ = liveStatus_ = exporting_ = false;
    clearInput();
    clearOutput();
    notice_[0] = 0;
    if (screen_ == S::Phonebook)
        syncPhonebook();
    render();
}
void Terminal::back() {
    if (focus_ == Focus::Help) {
        focus_ = helpReturn_;
        responseLine_ = helpReturnLine_;
        render();
        return;
    }
    clearInput();
    if (focus_ != Focus::Menu || response_) {
        focus_ = Focus::Menu;
        response_ = liveStatus_ = exporting_ = false;
        clearOutput();
        if (screen_ == S::Phonebook)
            syncPhonebook();
    } else if (depth_)
        screen_ = stack_[--depth_];
    else if (screen_ != S::Main)
        screen_ = S::Main;
    else {
        close();
        return;
    }
    notice_[0] = 0;
    render();
}
void Terminal::syncPhonebook() {
    bookCount_ = 0;
    auto *book = services_ ? services_->phonebook() : nullptr;
    if (!book || !book->loaded()) {
        selectedNumber_[0] = 0;
        return;
    }
    auto &location = locations_[static_cast<unsigned>(S::Phonebook)];
    size_t selected = location.selected;
    for (unsigned i = 0; i < calls::kPhonebookCount; ++i) {
        const auto &e = book->at(i);
        if (!*e.number)
            continue;
        if (!strcmp(e.number, selectedNumber_))
            selected = bookCount_;
        bookIndices_[bookCount_++] = i;
    }
    location.selected = bookCount_ ? std::min(selected, bookCount_ - 1) : 0;
    if (bookCount_)
        snprintf(selectedNumber_, sizeof selectedNumber_, "%s",
                 book->at(bookIndices_[location.selected]).number);
    else
        selectedNumber_[0] = 0;
}
const calls::Entry *Terminal::entry() const {
    auto *book = services_ ? services_->phonebook() : nullptr;
    return book && book->loaded() && *selectedNumber_ ? book->find(selectedNumber_) : nullptr;
}
void Terminal::buildFrame() {
    model_ = session_.port().modem().model();
    dirty_ = false;
    drawing_ = true;
    drawRow_ = 0;
    grid_.clear(cols_, rows_);
    drawStyle_ = style_;
    drawColor_ = color_;
    drawModelRgb_ = CellGrid::modelRgb(model_);
    drawCursorRow_ = rows_ - 1;
    drawCursorCol_ = std::min(13u, cols_ - 1);
    if (cols_ < 20 || rows_ < 8) {
        // Even pathological viewports get bounded output. Keep the same router
        // and keyboard actions; omit frames and multi-line chrome entirely.
        drawCursorRow_ = rows_;
        drawCursorCol_ = 1;
        grid_.text(0, 0, "Small terminal", Ink::Warning, cols_);
        unsigned y = rows_ > 1 ? 1 : 0;
        if (focus_ == Focus::Form) {
            if (prompt_ && prompt_->secret)
                grid_.text(0, y, "Password input", Ink::Muted, cols_);
            else
                grid_.text(0, y, input_, Ink::Body, cols_);
        } else if (focus_ == Focus::Confirm)
            grid_.text(0, y, "Y confirm N cancel", Ink::Warning, cols_);
        else if (response_ || focus_ == Focus::Help || focus_ == Focus::Details) {
            const char *text = focus_ == Focus::Help ? "Esc/Q Back; keys open; Y confirm" : output_;
            for (size_t line = 0; *text && line < responseLine_; ++text)
                if (*text == '\n')
                    ++line;
            grid_.text(0, y, text, Ink::Body, cols_);
            more_ = *text != 0;
        } else if (screen_ == S::Phonebook) {
            syncPhonebook();
            const auto *e = entry();
            grid_.text(0, y, e ? e->host : "No entries", Ink::Body, cols_);
        } else {
            auto *a = item(locations_[static_cast<unsigned>(screen_)].selected);
            grid_.text(0, y, a ? a->label : screenName(screen_), Ink::Body, cols_);
        }
        if (rows_ > 2)
            grid_.text(0, rows_ - 1, "Esc/Q Back", Ink::Function, cols_);
        return;
    }
    grid_.box(0, 0, cols_, rows_);
    bool main =
        screen_ == S::Main && focus_ == Focus::Menu && !response_ && cols_ >= 80 && rows_ >= 25;
    if (main) {
        // The exact 59-cell five-row wordmark from the measured design.
        for (unsigned y = 0; y < 5; ++y)
            for (unsigned x = 0; kWordmark[y][x]; ++x)
                grid_.put((cols_ - 59) / 2 + x, y + 1, kWordmark[y][x] == '#' ? 219 : ' ',
                          Ink::Logo);
        grid_.center(6, CellGrid::modelLabel(model_), Ink::Model);
        unsigned modelX = (cols_ - strlen(CellGrid::modelLabel(model_))) / 2;
        for (unsigned i = 0; i < 3; ++i) {
            grid_.put(modelX - 4 + i, 6, 196, Ink::Model);
            grid_.put(modelX + strlen(CellGrid::modelLabel(model_)) + 1 + i, 6, 196, Ink::Model);
        }
        grid_.rule(7, "[ MODEM CONFIGURATION - MAIN MENU ]");
    } else {
        grid_.text(3, 1, "USRobotics", Ink::Logo, cols_ - 5);
        if (cols_ >= 50)
            grid_.text(23, 1, CellGrid::modelLabel(model_), Ink::Model, cols_ - 25);
        char title[112];
        snprintf(title, sizeof title, "[ MAIN > %s ]",
                 response_ ? resultTitle_ : screenName(screen_));
        grid_.rule(2, title);
    }
    if (focus_ == Focus::Form || focus_ == Focus::Confirm)
        drawForm();
    else if (response_ || focus_ == Focus::Details || focus_ == Focus::Help)
        drawResponse();
    else if (screen_ == S::Phonebook)
        drawPhonebook();
    else
        drawMenu();

    UiState state;
    if (services_)
        state = services_->state();
    char status[176];
    snprintf(status, sizeof status, "Model: %s | Profile: Active | Link: %s",
             CellGrid::modelShort(model_), state.link);
    if (cols_ >= 120) {
        char host[16] = "--", rate[16] = "--";
        if (state.hostBaud)
            snprintf(host, sizeof host, "%lu", static_cast<unsigned long>(state.hostBaud));
        if (state.reportedRate)
            snprintf(rate, sizeof rate, "%lu", static_cast<unsigned long>(state.reportedRate));
        snprintf(status, sizeof status,
                 "Model: %s | Profile: Active | Link: %s | Host: %s | Reported rate: %s",
                 CellGrid::modelShort(model_), state.link, host, rate);
    }
    if (rows_ >= 25) {
        if (main)
            grid_.rule(rows_ - 6, "[ HELP / DEVICE STATUS ]");
        if (main)
            grid_.text(3, rows_ - 5, "F1 Help  F2 Status  F3 Phonebook  F4 Settings  F10 Exit",
                       Ink::Function, cols_ - 5);
        else
            grid_.text(
                3, rows_ - 5,
                capturing_
                    ? "Working / input requested - Esc cancel, Ctrl-C exit"
                    : (focus_ == Focus::Help
                           ? "PGUP/PGDN Pages  ENTER Continue  ESC/Q Back  F1 Close Help"
                           : (response_ ? "PGUP/PGDN Pages  ENTER Continue  ESC/Q Back  F1 Help"
                                        : (focus_ == Focus::Form
                                               ? "TAB Next  Arrows Edit  ENTER Review  ESC Cancel  "
                                                 "F1 Help"
                                               : (focus_ == Focus::Confirm
                                                      ? "Y Confirm  N/ESC Cancel"
                                                      : (screen_ == S::Phonebook
                                                             ? "UP/DOWN Select  PGUP/PGDN Pages  "
                                                               "ENTER/I Details  ESC/Q Back"
                                                             : "ARROWS Select  ENTER Open  ESC/Q "
                                                               "Back  F1 Help  F2 Main"))))),
                Ink::Function, cols_ - 5);
        grid_.text(3, rows_ - 4, status, Ink::Body, cols_ - 5);
    }
    const char *message = more_ && (response_ || focus_ == Focus::Help)
                              ? "More: Enter/PgDn; PgUp previous; Esc/Q Back"
                          : *notice_   ? notice_
                          : capturing_ ? "Working / waiting for input"
                                       : "Ready";
    if (cols_ < 80 || rows_ < 25)
        message = "Small terminal: paged view";
    grid_.text(2, rows_ - 3, message, Ink::Muted, cols_ - 4);
    grid_.text(2, rows_ - 2, focus_ == Focus::Confirm ? "Command > Y / N" : "Command >", Ink::Body,
               cols_ - 4);
}
void Terminal::drawMenu() {
    auto &location = locations_[static_cast<unsigned>(screen_)];
    bool compactMain = screen_ == S::Main && cols_ >= 80 && rows_ >= 25;
    bool wide = compactMain && cols_ >= 120 && rows_ >= 45;
    if (compactMain) {
        unsigned divider = cols_ / 2 - 1;
        for (unsigned i = 0; i < 13; ++i) {
            const auto *a = item(i);
            unsigned x = i < 6 ? 3 : divider + 3, y = 8 + (i < 6 ? i : i - 6);
            if (wide) {
                y = 11 + 4 * (i < 6 ? i : i - 6);
                grid_.box(i < 6 ? 2 : divider + 2, y, divider - 2, 4);
                x += 2;
                ++y;
            }
            char row[100];
            snprintf(row, sizeof row, "%c [%c] %s%s", location.selected == i ? '>' : ' ', a->key,
                     a->label,
                     a->available ? ""
                     : wide       ? " [X] UNAVAILABLE"
                                  : " [X]");
            grid_.text(x - 1, y, row, a->available ? Ink::Title : Ink::Disabled, divider - 3);
            grid_.put(x + 2, y, a->key, a->available ? Ink::Key : Ink::Disabled);
            if (wide)
                grid_.text(x, y + 1, a->summary, a->available ? Ink::Body : Ink::Disabled,
                           divider - 7);
        }
        if (!wide) {
            for (unsigned y = 8; y <= 14; ++y)
                grid_.put(divider, y, 179, Ink::Frame);
            grid_.rule(16);
            grid_.text(3, 17, "ARROWS: navigate / switch column   ENTER: select   Q: quit",
                       Ink::Body, cols_ - 5);
            grid_.text(3, 18, "[D] Client Tests - serial client diagnostics", Ink::Muted,
                       cols_ - 5);
        } else {
            grid_.text(3, 8, "   ______________", Ink::Title);
            grid_.text(3, 9, "  / USRobotics /|", Ink::Title);
            grid_.text(3, 10, " /___________/ |", Ink::Frame);
            grid_.text(cols_ - 22, 8, "______________", Ink::Title);
            grid_.text(cols_ - 22, 9, "| USRobotics \\", Ink::Title);
            grid_.text(cols_ - 22, 10, "| \\___________\\", Ink::Frame);
            grid_.text(3, 36, "ARROWS Navigate  ENTER Select  Q Quit", Ink::Body, divider - 4);
            grid_.text(3, 37, "[D] Client Tests - serial only", Ink::Muted, divider - 4);
        }
        return;
    }
    size_t count = itemCount(), visible = visibleItems();
    if (location.selected >= count)
        location.selected = count ? count - 1 : 0;
    if (location.selected < location.offset)
        location.offset = location.selected;
    if (location.selected >= location.offset + visible)
        location.offset = location.selected - visible + 1;
    grid_.text(3, 3, "KEY   OPTION", Ink::Frame, cols_ - 5);
    for (size_t i = location.offset; i < count && i < location.offset + visible; ++i) {
        char text[176];
        char key = screen_ == S::Profile ? kKeys[(i - location.offset) % 15] : item(i)->key;
        if (screen_ == S::Profile) {
            if (i < modemcfg::kSettingCount) {
                const auto &info = modemcfg::settingInfo(static_cast<modemcfg::Setting>(i));
                snprintf(text, sizeof text, "%c [%c] %s = %u", location.selected == i ? '>' : ' ',
                         key, info.name, session_.port().modem().active().v[i]);
            } else {
                unsigned reg = i - modemcfg::kSettingCount;
                snprintf(text, sizeof text, "%c [%c] S%u = %u%s",
                         location.selected == i ? '>' : ' ', key, reg,
                         session_.port().modem().active().s[reg], reg == 1 ? " (read only)" : "");
            }
        } else {
            const auto &a = *item(i);
            snprintf(text, sizeof text, "%c [%c] %s%s", location.selected == i ? '>' : ' ', key,
                     a.label,
                     rs232_ && a.rs232Unavailable ? " [unavailable: takes over RS232]"
                     : !a.available               ? " [UNAVAILABLE]"
                                                  : "");
        }
        unsigned y = 5 + i - location.offset;
        if (rows_ < 12)
            y = 3 + i - location.offset;
        grid_.text(2, y, text, enabled(i) ? Ink::Body : Ink::Disabled, cols_ - 4);
        grid_.put(5, y, key, enabled(i) ? Ink::Key : Ink::Disabled);
    }
    if (count > visible) {
        char page[64];
        snprintf(page, sizeof page, "Items %u-%u/%u  PgUp/PgDn", unsigned(location.offset + 1),
                 unsigned(std::min(count, location.offset + visible)), unsigned(count));
        grid_.text(2, 4, page, Ink::Muted, cols_ - 4);
    }
}
void Terminal::drawPhonebook() {
    syncPhonebook();
    auto *book = services_ ? services_->phonebook() : nullptr;
    bool ready = book && book->loaded();
    bool wide = cols_ >= 120 && rows_ >= 45;
    auto &location = locations_[static_cast<unsigned>(screen_)];
    size_t visible = phonebookPageSize();
    unsigned listStart = rows_ < 16 ? 3 : 6;
    location.offset = location.selected / visible * visible;
    char heading[96];
    snprintf(heading, sizeof heading, "PHONEBOOK ENTRIES    PAGE %u/%u",
             unsigned(location.offset / visible + 1),
             unsigned(std::max<size_t>(1, (bookCount_ + visible - 1) / visible)));
    if (rows_ >= 16)
        grid_.text(3, 3, heading, Ink::Title, cols_ - 5);
    unsigned listWidth = wide ? cols_ / 2 - 4 : cols_ - 4;
    if (rows_ >= 16) {
        grid_.text(3, 4, "#    NAME (HOST)                 PHONE NUMBER    RATE", Ink::Frame,
                   listWidth);
        for (unsigned x = 2; x < listWidth + 2; ++x)
            grid_.put(x, 5, 196, Ink::Frame);
    }
    for (size_t i = location.offset; ready && i < bookCount_ && i < location.offset + visible;
         ++i) {
        auto &e = book->at(bookIndices_[i]);
        char text[128], speed[16];
        if (e.speed)
            snprintf(speed, sizeof speed, "%lu", static_cast<unsigned long>(e.speed));
        else
            strcpy(speed, "Auto");
        snprintf(text, sizeof text, "%c%2u  %-28.28s %-12s %s", location.selected == i ? '>' : ' ',
                 unsigned(i + 1), e.host, e.number, speed);
        grid_.text(2, listStart + i - location.offset, text,
                   location.selected == i ? Ink::Title : Ink::Body, listWidth);
    }
    if (!ready || !bookCount_)
        grid_.text(3, listStart,
                   !ready ? "Phonebook unavailable: UiServices::phonebook" : "No saved entries",
                   Ink::Muted, listWidth);
    const auto *selected = entry();
    if (rows_ < 25)
        grid_.text(2, rows_ - 4, "1L 2E 3X 4D I?", Ink::Key, cols_ - 4);
    if (wide) {
        unsigned x = cols_ / 2 + 2, width = cols_ - x - 2;
        grid_.text(x, 3, "PHONEBOOK COMMANDS", Ink::Title, width);
        for (unsigned i = 0; i < 4; ++i) {
            const auto *a = item(i);
            bool enabled = i < 2 || selected;
            unsigned y = 5 + i * 4;
            grid_.box(x - 1, y, width + 2, 4);
            char text[96];
            snprintf(text, sizeof text, "[%c] %s%s", a->key, a->label,
                     enabled ? "" : " [X] UNAVAILABLE");
            grid_.text(x + 1, y + 1, text, enabled ? Ink::Key : Ink::Disabled, width - 2);
            grid_.text(x + 1, y + 2,
                       i == 0   ? "Select saved entries with arrows and page keys"
                       : i == 1 ? "Add a number or edit the selected entry"
                       : i == 2 ? "Confirm before deleting the selected entry"
                                : "Confirm before dialling the selected entry",
                       Ink::Body, width - 2);
        }
        grid_.box(x - 1, 22, width + 2, 14);
        grid_.text(x + 1, 23, "SELECTED ENTRY DETAILS", Ink::Title, width - 2);
        if (selected) {
            grid_.text(x + 1, 25, "Host (NAME):", Ink::Muted, width - 2);
            grid_.text(x + 1, 26, selected->host, Ink::Body, width - 2);
            if (strlen(selected->host) > width - 2)
                grid_.text(x + 1, 27, selected->host + width - 2, Ink::Body, width - 2);
            char text[144];
            snprintf(text, sizeof text, "Number: %s  Port: %u  %s", selected->number,
                     selected->port, selected->raw ? "RAW" : "TELNET");
            grid_.text(x + 1, 29, text, Ink::Body, width - 2);
            if (selected->speed)
                snprintf(text, sizeof text, "Configured speed: %lu",
                         static_cast<unsigned long>(selected->speed));
            else
                strcpy(text, "Configured speed: Auto (no entry override)");
            grid_.text(x + 1, 31, text, Ink::Body, width - 2);
            grid_.text(x + 1, 33, "[I] Full details; configured rate is not live", Ink::Key,
                       width - 2);
        } else
            grid_.text(x + 1, 26, "No selected entry", Ink::Muted, width - 2);
    } else if (rows_ >= 25) {
        grid_.rule(15, "[ SELECTED ENTRY ]");
        char summary[144];
        snprintf(summary, sizeof summary, "Host: %.32s  Number: %s",
                 selected ? selected->host : "--", selected ? selected->number : "--");
        grid_.text(3, 16, summary, Ink::Body, cols_ - 5);
        grid_.text(3, 17, "[I] Details | [1] List | [2] Add / edit", Ink::Key, cols_ - 5);
        grid_.text(3, 18, selected ? "[3] Delete | [4] Dial" : "[3] Delete [X] | [4] Dial [X]",
                   selected ? Ink::Key : Ink::Disabled, cols_ - 5);
    }
    if (ready && !book->healthy() && !*notice_)
        snprintf(notice_, sizeof notice_,
                 "Phonebook storage damaged/unavailable; existing factory fallback shown");
}
void Terminal::drawForm() {
    grid_.text(3, 4, prompt_ ? prompt_->label : "Confirm", Ink::Title, cols_ - 5);
    const char *help = focus_ == Focus::Confirm ? "Apply this action? Y confirms; N or Esc cancels."
                       : prompt_                ? prompt_->help
                                                : "";
    unsigned y = 6, x = 3;
    for (const char *p = help; *p && y + 5 < rows_; ++p) {
        grid_.put(x++, y, static_cast<unsigned char>(*p), Ink::Body);
        if (x >= cols_ - 3) {
            x = 3;
            ++y;
        }
    }
    if (focus_ == Focus::Confirm) {
        if (prompt_ && !prompt_->secret && cols_ >= 50)
            grid_.text(3, y + 2, pending_, Ink::Muted, cols_ - 5);
        return;
    }
    unsigned fieldRow = std::min(y + 2, rows_ - 4);
    grid_.text(3, fieldRow, "Value > ", Ink::Key, cols_ - 5);
    size_t width = cols_ > 14 ? cols_ - 14 : 5;
    size_t start = cursor_ >= width ? cursor_ - width + 1 : 0;
    for (size_t i = start; i < inputLen_ && i < start + width; ++i)
        grid_.put(11 + i - start, fieldRow,
                  prompt_ && prompt_->secret ? '*' : static_cast<uint8_t>(input_[i]), Ink::Body);
    drawCursorRow_ = fieldRow + 1;
    drawCursorCol_ = 12 + cursor_ - start;
}
void Terminal::drawResponse() {
    const char *text = output_;
    size_t len = length_;
    const char *help =
        "ARROWS select; LEFT/RIGHT switch main columns.\nEnter opens selected item; hotkeys open "
        "immediately.\nEsc or Q: Back; Q/F10 on Main: exit. Ctrl-C: exit.\nF1: contextual help. "
        "F2: Main in children.\nMain F2/F3/F4: Status/Phonebook/Settings.\nPhonebook: PgUp/PgDn "
        "pages; I/Enter details.\nForms: arrows/Home/End edit; Backspace/Delete erase.\nTab moves "
        "through comma-separated fields; Ctrl-U clears.\nY confirms a reviewed change; N/Esc "
        "cancels.\nRATE is a configured override, never a live measurement.\nNAME is the saved "
        "host; no separate names are stored.\nUse AT*SIZE=columns,rows outside the menu for serial "
        "sizing.\n";
    if (focus_ == Focus::Help) {
        text = prompt_ ? prompt_->help : help;
        len = strlen(text);
    }
    size_t line = 0, offset = 0;
    unsigned width = cols_ - 4;
    // Reflow the captured single-cell transcript at the current geometry. This
    // keeps a resize safe even when a service wrote at the previous width.
    unsigned col = 0, y = rows_ < 12 ? 3 : 5;
    unsigned bottom = rows_ >= 25 ? rows_ - 5 : rows_ - 3;
    while (offset < len && y < bottom) {
        unsigned char c = text[offset++];
        if (c == '\r')
            continue;
        if (c == '\n' || col == width) {
            if (line++ >= responseLine_)
                ++y;
            col = 0;
            if (c == '\n')
                continue;
        }
        if (line >= responseLine_ && y < bottom)
            grid_.put(2 + col, y, c >= 32 && c != 127 ? c : '?', Ink::Body);
        ++col;
    }
    more_ = offset < len;
    grid_.text(3, 3,
               focus_ == Focus::Help ? "HELP"
               : capturing_          ? "WORKING / INPUT REQUESTED"
                                     : "COMMAND OUTPUT",
               Ink::Title, cols_ - 5);
    if (more_)
        grid_.text(2, rows_ - 3, "More: Enter / PgDn; PgUp previous; Esc/Q Back", Ink::Function,
                   cols_ - 4);
}
size_t Terminal::read(uint8_t *data, size_t cap) {
    if (diagnostic_) {
        if (stagePos_ < stageLen_) {
            size_t n = std::min(cap, stageLen_ - stagePos_);
            memcpy(data, staging_ + stagePos_, n);
            stagePos_ += n;
            return n;
        }
        return diagnostic_->read(data, cap);
    }
    size_t n = 0;
    while (open_ && n < cap) {
        if (stagePos_ < stageLen_) {
            size_t take = std::min(cap - n, stageLen_ - stagePos_);
            memcpy(data + n, staging_ + stagePos_, take);
            stagePos_ += take;
            n += take;
            continue;
        }
        stagePos_ = stageLen_ = 0;
        if (exporting_) {
            if (more_) {
                if (exportPrompt_)
                    break;
                const char *hint = "\r\n[More: Enter; Esc/Q returns]\r\n";
                stageLen_ = strlen(hint);
                memcpy(staging_, hint, stageLen_);
                exportPrompt_ = true;
            } else if (pos_ < length_) {
                char c = output_[pos_++];
                if (c == '\n') {
                    staging_[0] = '\r';
                    staging_[1] = '\n';
                    stageLen_ = 2;
                    if (++lines_ >= unsigned(std::max(1, int(rows_) - 3)) && pos_ < length_)
                        more_ = true;
                } else {
                    staging_[0] = c;
                    stageLen_ = 1;
                }
            } else if (!exportFinished_) {
                const char *hint = "\r\nEnter returns to menu.\r\n";
                stageLen_ = strlen(hint);
                memcpy(staging_, hint, stageLen_);
                exportFinished_ = true;
            } else
                break;
            continue;
        }
        if (!drawing_) {
            if (querySize_ && !capturing_ && !exporting_ && focus_ == Focus::Menu &&
                style_ != Style::Plain) {
                const char *query = "\x1b[18t";
                stageLen_ = strlen(query);
                memcpy(staging_, query, stageLen_);
                querySize_ = false;
                continue;
            }
            if (!dirty_)
                break;
            buildFrame();
        }
        if (drawRow_ == 0) {
            if (drawStyle_ != Style::Plain) {
                styled_ = true;
                const char *start = drawColor_ == Color::Rgb
                                        ? "\x1b[?25l\x1b[?7l\x1b[0m\x1b[48;2;3;5;7m\x1b[2J"
                                        : "\x1b[?25l\x1b[?7l\x1b[0m\x1b[40m\x1b[2J";
                stageLen_ = strlen(start);
                memcpy(staging_, start, stageLen_);
            }
        } else if (drawRow_ <= grid_.rows()) {
            stageLen_ =
                grid_.row(drawRow_ - 1, staging_, sizeof staging_, drawStyle_ == Style::Cp437,
                          drawStyle_ == Style::Plain, drawColor_, drawModelRgb_);
        } else {
            if (drawStyle_ != Style::Plain)
                stageLen_ =
                    snprintf(staging_, sizeof staging_, "\x1b[0m\x1b[%u;%uH\x1b[?7h\x1b[?25h",
                             drawCursorRow_, drawCursorCol_);
            drawing_ = false;
        }
        ++drawRow_;
    }
    return n;
}
void Terminal::navigate(int direction) {
    if (screen_ == S::Phonebook) {
        syncPhonebook();
        auto &location = locations_[static_cast<unsigned>(screen_)];
        if (bookCount_) {
            int selected = static_cast<int>(location.selected) + direction;
            location.selected = std::max(0, std::min(int(bookCount_) - 1, selected));
            auto *book = services_->phonebook();
            snprintf(selectedNumber_, sizeof selectedNumber_, "%s",
                     book->at(bookIndices_[location.selected]).number);
        }
        render();
        return;
    }
    auto &location = locations_[static_cast<unsigned>(screen_)];
    size_t count = itemCount();
    if (!count)
        return;
    int selected = static_cast<int>(location.selected);
    bool columns = screen_ == S::Main && cols_ >= 80 && rows_ >= 25;
    int first = columns && selected >= 6 ? 6 : 0;
    int end = columns && selected < 6 ? 6 : count;
    for (size_t attempt = 0; attempt < count; ++attempt) {
        selected += direction;
        if (selected < first)
            selected = end - 1;
        if (selected >= end)
            selected = first;
        if (enabled(selected)) {
            location.selected = selected;
            break;
        }
    }
    render();
}
void Terminal::help() {
    if (exporting_)
        return;
    if (focus_ == Focus::Help) {
        focus_ = helpReturn_;
        responseLine_ = helpReturnLine_;
    } else {
        helpReturn_ = focus_;
        helpReturnLine_ = responseLine_;
        focus_ = Focus::Help;
        responseLine_ = 0;
    }
    render();
}
void Terminal::profileAction(size_t index) {
    if (index >= itemCount() || !enabled(index)) {
        feedback("S1 is read only");
        return;
    }
    if (index < modemcfg::kSettingCount) {
        auto setting = static_cast<modemcfg::Setting>(index);
        const auto &info = modemcfg::settingInfo(setting);
        snprintf(profileCommand_, sizeof profileCommand_, "*SETTING=%s", info.name);
        char values[192] = {};
        size_t used = 0;
        for (unsigned v = 0; v < 64 && used + 5 < sizeof values; ++v)
            if (modemcfg::settingAllows(setting, v))
                used += snprintf(values + used, sizeof values - used, "%s%u", used ? "," : "", v);
        snprintf(profileHelp_, sizeof profileHelp_, "%s %s Allowed: %s. Active only; &W saves.",
                 info.name, settingHelp(setting), values);
    } else {
        unsigned reg = index - modemcfg::kSettingCount;
        snprintf(profileCommand_, sizeof profileCommand_, "*SETTING=S%u=", reg);
        snprintf(profileHelp_, sizeof profileHelp_, "S%u: %s Enter 0-255; active only, &W saves.",
                 reg, registerHelp(reg));
    }
    profileField_ = {S::Settings, "Profile field", profileCommand_, profileHelp_, S::Profile, true};
    activate(profileField_);
}
void Terminal::activate(const Action &action) {
    if (session_.port().browser() && action.screen == S::Client) {
        feedback("Serial client test: run on console COM15 or RS232 COM18");
        return;
    }
    if (!action.available) {
        feedback("Client Tests (v0.12) unavailable");
        return;
    }
    if (rs232_ && action.rs232Unavailable) {
        feedback("Unavailable: hardware hook takes over this RS232 port; use console or browser");
        return;
    }
    if (!*action.command) {
        changeScreen(action.target);
        return;
    }
    clearInput();
    prompt_ = &action;
    snprintf(resultTitle_, sizeof resultTitle_, "%s", action.label);
    if (action.screen == S::Phonebook) {
        syncPhonebook();
        auto *book = services_ ? services_->phonebook() : nullptr;
        if (!book || !book->loaded()) {
            prompt_ = nullptr;
            feedback("Phonebook unavailable: UiServices::phonebook hook");
            return;
        }
        if (!strcmp(action.id, "phonebook.list-entries")) {
            prompt_ = nullptr;
            render();
            return;
        }
        const auto *e = entry();
        if (!strcmp(action.id, "phonebook.delete-entry") || !strcmp(action.id, "phonebook.dial")) {
            if (!e) {
                prompt_ = nullptr;
                feedback("No selected entry; Delete and Dial unavailable");
                return;
            }
            snprintf(pending_, sizeof pending_, "%s%s", action.command, e->number);
            snprintf(confirmedNumber_, sizeof confirmedNumber_, "%s", e->number);
            confirmedEntryHash_ = entryHash(*e);
            confirmingLeave_ = action.leave;
            focus_ = Focus::Confirm;
            render();
            return;
        }
        if (e && !strcmp(action.id, "phonebook.add-edit-entry")) {
            snprintf(input_, sizeof input_, "%s,%s,%u,%s,%lu", e->number, e->host, e->port,
                     e->raw ? "RAW" : "TELNET", static_cast<unsigned long>(e->speed));
            inputLen_ = cursor_ = strlen(input_);
        }
    }
    if (action.input) {
        focus_ = Focus::Form;
        render();
        return;
    }
    snprintf(pending_, sizeof pending_, "%s", action.command);
    confirmingLeave_ = action.leave;
    if (confirmation(action, session_.port().browser(), pending_)) {
        focus_ = Focus::Confirm;
        render();
    } else
        dispatchPending();
}
void Terminal::submit() {
    if (!prompt_)
        return;
    if (inputOverflow_ || inputLen_ == 0) {
        feedback(inputOverflow_ ? "Input too long; clear or cancel the form"
                                : "A value is required");
        return;
    }
    if (snprintf(pending_, sizeof pending_, "%s%s", prompt_->command, input_) >=
        int(sizeof pending_)) {
        devicecfg::wipe(pending_, sizeof pending_);
        feedback("Value too long; action not submitted");
        return;
    }
    confirmingLeave_ = prompt_->leave;
    // Secret temporary input no longer lives in the editable buffer. The pending
    // command is wiped on confirmation, cancellation, timeout and disconnect.
    devicecfg::wipe(input_, sizeof input_);
    inputLen_ = cursor_ = 0;
    if (confirmation(*prompt_, session_.port().browser(), pending_)) {
        focus_ = Focus::Confirm;
        render();
    } else
        dispatchPending();
}
void Terminal::dispatchPending() {
    if (*confirmedNumber_) {
        auto *book = services_ ? services_->phonebook() : nullptr;
        auto *e = book ? book->find(confirmedNumber_) : nullptr;
        if (!e || entryHash(*e) != confirmedEntryHash_) {
            clearInput();
            focus_ = Focus::Menu;
            feedback("Entry changed; select it and review again");
            return;
        }
    }
    char command[atcmd::kLineMax + 1];
    snprintf(command, sizeof command, "%s", pending_);
    bool leave = confirmingLeave_;
    clearInput();
    focus_ = Focus::Menu;
    if (leave) {
        snprintf(departureCommand_, sizeof departureCommand_, "%s", command);
        departure_ = true;
        close();
    } else
        execute(command);
    devicecfg::wipe(command, sizeof command);
}
void Terminal::execute(const char *command) {
    clearOutput();
    response_ = capturing_ = true;
    exporting_ = !strcmp(command, "*EXPORT");
    exportPrompt_ = exportFinished_ = false;
    if (exporting_) {
        drawing_ = false;
        stagePos_ = 0;
        const char *prefix =
            style_ == Style::Plain ? "" : "\x18\x1b[0m\x1b[?7h\x1b[?25h\x1b[2J\x1b[H";
        stageLen_ = strlen(prefix);
        memcpy(staging_, prefix, stageLen_);
    }
    liveStatus_ = !strcmp(command, "*STATUS");
    focus_ = Focus::Menu;
    notice_[0] = 0;
    session_.port().redirect(this, true);
    session_.port().execute(command);
    if (diagnostic_) {
        session_.port().redirect(nullptr);
        capturing_ = response_ = false;
        return;
    }
    if (!session_.port().busy())
        finishResponse();
    render();
}
void Terminal::finishResponse() {
    session_.port().redirect(nullptr);
    capturing_ = false;
    if (overflow_) {
        const char *warning = "\n[Output limit reached; use a smaller query.]\n";
        memcpy(output_ + length_, warning, strlen(warning));
        length_ += strlen(warning);
        overflow_ = false;
    }
    // Service output contains the definitive success/failure/timeout. No
    // optimistic success is added by the presentation layer.
    render();
}
void Terminal::poll(uint32_t now) {
    now_ = now;
    if (!open_)
        return;
    if (diagnostic_) {
        diagnostic_->poll(now);
        if (!diagnostic_->isOpen()) {
            diagnostic_ = nullptr;
            clearOutput();
            feedback("Client test finished; D Summary shows findings");
        }
        return;
    }
    if (!session_.port().browser() && style_ != Style::Plain && now - sizeQueriedAt_ >= 5000) {
        sizeQueriedAt_ = now;
        querySize_ = true;
    }
    Focus activeFocus = focus_ == Focus::Help ? helpReturn_ : focus_;
    if ((activeFocus == Focus::Confirm && now - activityAt_ >= 30000) ||
        (activeFocus == Focus::Form && prompt_ && prompt_->secret && now - activityAt_ >= 120000)) {
        clearInput();
        focus_ = Focus::Menu;
        feedback("Action timed out; temporary input cleared");
    }
    if (escapeLen_ && now - escapeAt_ >= (escapeLen_ == 1 ? 60u : 250u)) {
        bool bare = escapeLen_ == 1 && !escapeDiscard_;
        escapeLen_ = 0;
        escapeDiscard_ = false;
        if (bare)
            key(27);
    }
    if (capturing_ && !session_.port().busy())
        finishResponse();
    if (now - refreshedAt_ >= 2000) {
        refreshedAt_ = now;
        unsigned current = session_.port().modem().model();
        if (current != model_) {
            model_ = current;
            render();
        }
        if (style_ != Style::Plain && focus_ == Focus::Menu && !capturing_ && !drawing_ &&
            stagePos_ == stageLen_) {
            if (liveStatus_ && response_ && !more_ && responseLine_ == 0)
                execute("*STATUS");
            else if (!response_)
                render();
        }
    }
}
size_t Terminal::write(const uint8_t *data, size_t len) {
    if (diagnostic_)
        return diagnostic_->write(data, len);
    if (!open_ || !len)
        return 0;
    input(static_cast<char>(data[0]));
    return 1;
}
void Terminal::sequence() {
    unsigned code = 0;
    const char *s = escape_ + 1;
    if (!strncmp(s, "[8;", 3)) {
        // Standard xterm window report. Check bounds before decimal arithmetic;
        // windows larger than the renderer use a bounded top-left viewport.
        const char *p = s + 3;
        auto number = [&](unsigned &n) {
            n = 0;
            bool digit = false;
            while (*p >= '0' && *p <= '9') {
                digit = true;
                n = n * 10 + *p++ - '0';
                if (n > 9999)
                    return false;
            }
            return digit;
        };
        unsigned rows, cols;
        if (number(rows) && *p++ == ';' && number(cols) && *p++ == 't' && !*p)
            resize(std::min(cols, CellGrid::kMaxCols), std::min(rows, CellGrid::kMaxRows));
        escapeLen_ = 0;
        return;
    }
    if (!strcmp(s, "[A") || !strcmp(s, "OA"))
        code = kUp;
    else if (!strcmp(s, "[B") || !strcmp(s, "OB"))
        code = kDown;
    else if (!strcmp(s, "[C") || !strcmp(s, "OC"))
        code = kRight;
    else if (!strcmp(s, "[D") || !strcmp(s, "OD"))
        code = kLeft;
    else if (!strcmp(s, "[H") || !strcmp(s, "OH") || !strcmp(s, "[1~"))
        code = kHome;
    else if (!strcmp(s, "[F") || !strcmp(s, "OF") || !strcmp(s, "[4~"))
        code = kEnd;
    else if (!strcmp(s, "[5~"))
        code = kPageUp;
    else if (!strcmp(s, "[6~"))
        code = kPageDown;
    else if (!strcmp(s, "[3~"))
        code = kDelete;
    else if (!strcmp(s, "OP") || !strcmp(s, "[11~") || !strcmp(s, "[[A"))
        code = kF1;
    else if (!strcmp(s, "OQ") || !strcmp(s, "[12~") || !strcmp(s, "[[B"))
        code = kF2;
    else if (!strcmp(s, "OR") || !strcmp(s, "[13~") || !strcmp(s, "[[C"))
        code = kF3;
    else if (!strcmp(s, "OS") || !strcmp(s, "[14~") || !strcmp(s, "[[D"))
        code = kF4;
    else if (!strcmp(s, "[21~"))
        code = kF10;
    escapeLen_ = 0;
    if (code)
        key(code);
}
void Terminal::input(char c) {
    activityAt_ = now_;
    if (capturing_ && !session_.port().busy())
        finishResponse();
    if (c == '\n' && skipLf_) {
        skipLf_ = false;
        return;
    }
    skipLf_ = c == '\r';
    if (c == 3) {
        close();
        return;
    }
    if (escapeLen_) {
        escapeAt_ = now_;
        if (escapeDiscard_) {
            if (c >= '@' && c <= '~') {
                escapeLen_ = 0;
                escapeDiscard_ = false;
            }
            return;
        }
        if (escapeLen_ == 1 && c != '[' && c != 'O') {
            escapeLen_ = 0;
            key(27);
            if (open_)
                input(c);
            return;
        }
        if (escapeLen_ + 1 >= sizeof escape_) {
            escapeDiscard_ = !(c >= '@' && c <= '~');
            if (!escapeDiscard_)
                escapeLen_ = 0;
            return;
        }
        escape_[escapeLen_++] = c;
        escape_[escapeLen_] = 0;
        if (escapeLen_ >= 3 && c >= '@' && c <= '~' && !(escapeLen_ == 3 && c == '['))
            sequence();
        return;
    }
    if (c == 27) {
        escape_[0] = c;
        escape_[1] = 0;
        escapeLen_ = 1;
        escapeAt_ = now_;
        return;
    }
    if (capturing_) {
        if (swallowEnter_ && (c == '\r' || c == '\n')) {
            swallowEnter_ = false;
            return;
        }
        swallowEnter_ = false;
        if (c == '\r' || c == '\n')
            c = static_cast<char>(session_.port().modem().active().s[3]);
        else if (c == '\b' || c == 127)
            c = static_cast<char>(session_.port().modem().active().s[5]);
        session_.port().feed(c);
        // A running test may be cancelled, a password prompt may still be busy.
        render();
        return;
    }
    if (swallowEnter_ && (c == '\r' || c == '\n')) {
        swallowEnter_ = false;
        return;
    }
    if (c >= 32 && c <= 126)
        swallowEnter_ = false;
    key(static_cast<unsigned char>(c));
}
void Terminal::key(unsigned key) {
    if (capturing_) {
        if (key == 27) {
            session_.port().cancelPrompt();
            finishResponse();
        }
        return;
    }
    if (key == kF1) {
        help();
        return;
    }
    if (focus_ == Focus::Help) {
        if (key == 27 || key == 'q' || key == 'Q') {
            back();
            return;
        }
        if (key == kPageDown || key == '\r' || key == '\n') {
            if (more_)
                responseLine_ += visibleItems();
            else
                back();
        } else if (key == kPageUp)
            responseLine_ = responseLine_ > visibleItems() ? responseLine_ - visibleItems() : 0;
        render();
        return;
    }
    if (focus_ == Focus::Form) {
        if (key == 27) {
            clearInput();
            focus_ = Focus::Menu;
            feedback("Edit cancelled");
            return;
        }
        if (key == kLeft && cursor_)
            --cursor_;
        else if (key == kRight && cursor_ < inputLen_)
            ++cursor_;
        else if (key == kHome)
            cursor_ = 0;
        else if (key == kEnd)
            cursor_ = inputLen_;
        else if (key == 21) {
            devicecfg::wipe(input_, sizeof input_);
            inputLen_ = cursor_ = 0;
            inputOverflow_ = false;
        } else if (key == '\t') {
            while (cursor_ < inputLen_ && input_[cursor_++] != ',') {
            }
            if (cursor_ == inputLen_)
                cursor_ = 0;
        } else if (key == backspace_ && cursor_) {
            memmove(input_ + cursor_ - 1, input_ + cursor_, inputLen_ - cursor_ + 1);
            --cursor_;
            --inputLen_;
        } else if (key == kDelete && cursor_ < inputLen_) {
            memmove(input_ + cursor_, input_ + cursor_ + 1, inputLen_ - cursor_);
            --inputLen_;
        } else if (key == '\r' || key == '\n') {
            submit();
            return;
        } else if (key >= 32 && key < 127) {
            if (inputLen_ + 1 >= sizeof input_)
                inputOverflow_ = true;
            else {
                memmove(input_ + cursor_ + 1, input_ + cursor_, inputLen_ - cursor_ + 1);
                input_[cursor_++] = key;
                ++inputLen_;
            }
        }
        render();
        return;
    }
    if (focus_ == Focus::Confirm) {
        if (key == 'y' || key == 'Y') {
            dispatchPending();
            swallowEnter_ = true;
        } else if (key == 'n' || key == 'N' || key == 27) {
            clearInput();
            focus_ = Focus::Menu;
            feedback("Action cancelled");
            swallowEnter_ = true;
        }
        return;
    }
    if (response_ || focus_ == Focus::Details) {
        if (exporting_) {
            if (key == 27 || key == 'q' || key == 'Q')
                back();
            else if (key == '\r' || key == '\n' || key == ' ' || key == kPageDown) {
                if (more_) {
                    more_ = false;
                    exportPrompt_ = false;
                    lines_ = 0;
                } else if (exportFinished_)
                    back();
            }
            return;
        }
        if (key == 27 || key == 'q' || key == 'Q') {
            back();
            return;
        }
        if (key == kPageDown || key == '\r' || key == '\n' || key == ' ') {
            if (more_)
                responseLine_ += visibleItems();
            else
                back();
        } else if (key == kPageUp)
            responseLine_ = responseLine_ > visibleItems() ? responseLine_ - visibleItems() : 0;
        else if ((key == 'r' || key == 'R') && liveStatus_)
            execute("*STATUS");
        render();
        return;
    }
    if (key == kF10 && screen_ == S::Main) {
        close();
        return;
    }
    if (key == 27 || key == 'q' || key == 'Q') {
        back();
        return;
    }
    if (key == kF2 && screen_ != S::Main) {
        depth_ = 0;
        screen_ = S::Main;
        clearInput();
        render();
        return;
    }
    if (screen_ == S::Main && (key == kF2 || key == kF3 || key == kF4)) {
        auto *a = findAction(key == kF2 ? "status" : key == kF3 ? "phonebook" : "settings");
        activate(*a);
        return;
    }
    auto &location = locations_[static_cast<unsigned>(screen_)];
    if (key == kUp || key == kDown) {
        navigate(key == kUp ? -1 : 1);
        return;
    }
    if (screen_ == S::Main && cols_ >= 80 && rows_ >= 25 && (key == kLeft || key == kRight)) {
        if (key == kRight && location.selected < 6)
            location.selected += 6;
        else if (key == kLeft && location.selected >= 6)
            location.selected = std::min<size_t>(5, location.selected - 6);
        render();
        return;
    }
    if (key == kPageUp || key == kPageDown) {
        if (screen_ == S::Phonebook)
            navigate((key == kPageUp ? -1 : 1) * int(phonebookPageSize()));
        else {
            int offset = int(location.selected) + (key == kPageUp ? -1 : 1) * int(visibleItems());
            location.selected = std::max(0, std::min(int(itemCount()) - 1, offset));
            if (!enabled(location.selected))
                navigate(key == kPageUp ? -1 : 1);
            render();
        }
        return;
    }
    if (screen_ == S::Phonebook && (key == 'i' || key == 'I' || key == '\r' || key == '\n')) {
        const auto *e = entry();
        if (!e) {
            feedback("No selected entry");
            return;
        }
        clearOutput();
        char text[256];
        snprintf(text, sizeof text, "Host: %s\nNumber: %s\nPort: %u\nProtocol: %s\n", e->host,
                 e->number, e->port, e->raw ? "RAW" : "TELNET");
        append(text);
        if (e->speed) {
            snprintf(text, sizeof text, "Configured speed: %lu\n",
                     static_cast<unsigned long>(e->speed));
            append(text);
        } else
            line("Configured speed: Auto (no entry override)");
        line("NAME uses the saved host. Rate is not a live measurement.");
        snprintf(resultTitle_, sizeof resultTitle_, "Phonebook / Details");
        focus_ = Focus::Details;
        render();
        return;
    }
    if (key == '\r' || key == '\n') {
        if (screen_ == S::Profile)
            profileAction(location.selected);
        else if (enabled(location.selected))
            activate(*item(location.selected));
        return;
    }
    unsigned upper = key < 128 ? std::toupper(static_cast<unsigned char>(key)) : key;
    if (screen_ == S::Profile) {
        const char *found = upper < 128 ? strchr(kKeys, upper) : nullptr;
        if (found && *found) {
            size_t index = location.offset + (found - kKeys);
            if (index < itemCount()) {
                location.selected = index;
                profileAction(index);
                swallowEnter_ = true;
            }
        }
        return;
    }
    for (size_t i = 0; i < itemCount(); ++i) {
        auto *a = item(i);
        if (upper == unsigned(a->key)) {
            if (enabled(i))
                location.selected = i;
            activate(*a);
            swallowEnter_ = true;
            return;
        }
    }
}
}  // namespace config
