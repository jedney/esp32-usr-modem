#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "config.h"
#include "ui_generated.h"

namespace config {
namespace {
using S = Screen;

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
    if (a.screen == S::Wifi || a.screen == S::Clock)
        return !strcmp(a.command, "*WIFIJOIN=") || !strcmp(a.command, "*WIFI=") ||
               !strcmp(a.command, "*WIFIFORGET") || !strcmp(a.command, "*WIFIIP=") ||
               !strcmp(a.command, "*HOSTNAME=") || !strcmp(a.command, "*TZ=") ||
               !strcmp(a.command, "*NTP=") || !strcmp(a.command, "*INTERNET=") ||
               !strcmp(a.command, "*DIAL=");
    return false;
}
uint32_t entryHash(const calls::Entry &e) {
    // Compare semantic fields, never struct padding. A changed entry must be
    // reviewed again before a destructive operation or external connection.
    char text[144];
    snprintf(text, sizeof text, "%s,%s,%u,%u,%lu", e.number, e.host, e.port, unsigned(e.protocol),
             static_cast<unsigned long>(e.speed));
    return modemcfg::crc32(reinterpret_cast<const uint8_t *>(text), strlen(text));
}
}  // namespace
const Action *actions(size_t &count) {
    count = sizeof kActions / sizeof kActions[0];
    return kActions;
}
const char *screenName(Screen screen) {
    if (screen == S::Main)
        return "Configuration";
    if (screen == S::Profile)
        return "Profile";
    for (const auto &action : kActions)
        if (!*action.command && action.target == screen)
            return action.metadata->friendly;
    return "Unknown";
}
bool Terminal::open() {
    if (open_)
        return false;
    open_ = true;
    screen_ = S::Main;
    depth_ = 0;
    for (auto &offset : offsets_)
        offset = 0;
    focus_ = Focus::Menu;
    capturing_ = response_ = skipLf_ = departure_ = false;
    clearInput();
    clearOutput();
    notice_[0] = 0;
    escapeLen_ = 0;
    escapeDiscard_ = false;
    swallowEnter_ = false;
    stageLen_ = stagePos_ = 0;
    activityAt_ = now_;
    render();
    return true;
}
void Terminal::clearInput() {
    devicecfg::wipe(input_, sizeof input_);
    devicecfg::wipe(pending_, sizeof pending_);
    devicecfg::wipe(fieldValues_, sizeof fieldValues_);
    inputLen_ = 0;
    inputOverflow_ = false;
    prompt_ = nullptr;
    editor_ = nullptr;
    choosingVariant_ = choiceSelected_ = custom_ = false;
    fieldIndex_ = choiceOffset_ = 0;
    confirmedNumber_[0] = 0;
}
void Terminal::clearOutput() {
    devicecfg::wipe(output_, sizeof output_);
    length_ = pos_ = responseLine_ = responseLines_ = 0;
    more_ = overflow_ = false;
}
void Terminal::line(const char *text) {
    append(text);
    append("\n");
}
void Terminal::append(const char *text) {
    write(text, strlen(text));
}
void Terminal::write(const char *data, size_t len) {
    // Only trusted renderer strings may contain controls. Service output cannot
    // clear the screen, move the cursor or inject terminal commands.
    for (size_t i = 0; i < len; ++i) {
        if (length_ >= sizeof output_ - 128) {
            overflow_ = true;
            break;
        }
        unsigned char c = data[i];
        if (c == '\r' || (c == '\n' && !length_))
            continue;
        if (c == '\n')
            ++responseLines_;
        else if (c < 32 || c >= 127)
            c = '?';
        output_[length_++] = c;
    }
    output_[length_] = 0;
    response_ = true;
    render();
}
bool Terminal::resize(unsigned columns, unsigned rows) {
    if (!columns || columns > 132 || !rows || rows > 50)
        return false;
    cols_ = columns;
    rows_ = rows;
    return true;  // Transport geometry is independent of the scrolling menu.
}
bool Terminal::setSize(const char *text) {
    auto number = [&](unsigned &n, unsigned max) {
        n = 0;
        bool digit = false;
        while (*text >= '0' && *text <= '9') {
            digit = true;
            n = n * 10 + *text++ - '0';
            if (n > max)
                return false;
        }
        return digit;
    };
    unsigned cols, rowCount;
    return number(cols, 132) && *text++ == ',' && number(rowCount, 50) && !*text &&
           resize(cols, rowCount);
}
bool Terminal::setStyle(const char *text) {
    if (!strcmp(text, "PLAIN"))
        style_ = Style::Plain;
    else if (!strcmp(text, "ANSI"))
        style_ = Style::Ansi;
    else
        return false;
    render();
    return true;
}
void Terminal::close() {
    if (diagnostic_) {
        diagnostic_->close();
        diagnostic_ = nullptr;
    }
    if (capturing_)
        session_.port().cancelPrompt();
    session_.port().redirect(nullptr);
    open_ = capturing_ = dirty_ = false;
    stageLen_ = stagePos_ = escapeLen_ = 0;
    clearInput();
    clearOutput();
}
void Terminal::escaped() {
    if (diagnostic_) {
        diagnostic_->close();
        diagnostic_ = nullptr;
    }
    if (capturing_)
        session_.port().cancelPrompt();
    session_.port().redirect(nullptr);
    capturing_ = response_ = false;
    stagePos_ = stageLen_ = 0;
    clearInput();
    clearOutput();
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
void Terminal::emit(const char *text) {
    if (stagePos_ == stageLen_)
        stagePos_ = stageLen_ = 0;
    size_t len = std::min(strlen(text), sizeof staging_ - stageLen_ - 1);
    memcpy(staging_ + stageLen_, text, len);
    stageLen_ += len;
    staging_[stageLen_] = 0;
}
void Terminal::emitLine(const char *text) {
    emit(text);
    emit("\r\n");
}
const Action *Terminal::item(size_t index) const {
    for (const auto &a : kActions)
        if (a.screen == screen_ && index-- == 0)
            return &a;
    return nullptr;
}
size_t Terminal::itemCount() const {
    if (screen_ == S::Profile)
        return findAction("settings.edit-profile-registers")->metadata->fieldCount;
    size_t n = 0;
    for (const auto &a : kActions)
        if (a.screen == screen_)
            ++n;
    return n;
}
void Terminal::changeScreen(Screen screen) {
    if (screen_ != screen && depth_ < 10)
        stack_[depth_++] = screen_;
    screen_ = screen;
    focus_ = Focus::Menu;
    response_ = false;
    clearInput();
    clearOutput();
    notice_[0] = 0;
    render();
}
void Terminal::back() {
    clearInput();
    notice_[0] = 0;
    if (focus_ != Focus::Menu || response_) {
        focus_ = Focus::Menu;
        response_ = false;
        clearOutput();
    } else if (depth_)
        screen_ = stack_[--depth_];
    else {
        close();
        return;
    }
    render();
}
void Terminal::currentValue(const Action &a, char *out, size_t cap) const {
    *out = 0;
    if (a.secret) {
        snprintf(out, cap, "hidden");
        return;
    }
    auto &m = session_.port().modem();
    const char *cmd = a.command;
    unsigned value = 0;
    bool numeric = true;
    if (!strcmp(cmd, "*MODEL="))
        value = m.model();
    else if (!strcmp(cmd, "*SETTING=S0="))
        value = m.active().s[0];
    else if (!strcmp(cmd, "*BAUD=")) {
        if (!m.hostBaud()) {
            snprintf(out, cap, "AUTO");
            return;
        }
        value = m.hostBaud();
    } else if (!strcmp(cmd, "*SETTING=Y"))
        value = m.nvram().y;
    else if (!strcmp(cmd, "*THROTTLE=")) {
        snprintf(out, cap, "%s", m.throttle() ? "ON" : "OFF");
        return;
    } else if (!strcmp(cmd, "*ANSWERPORT=")) {
        snprintf(out, cap, "%s", modemcfg::answerPortName(m.answerPort()));
        return;
    } else if (!strcmp(cmd, "*RINGDELAY="))
        value = m.ringDelayMs();
    else if (!strcmp(cmd, "*CONNECTDELAY="))
        value = m.connectDelayMs();
    else if (!strcmp(cmd, "*STYLE=")) {
        snprintf(out, cap, "%s", style_ == Style::Plain ? "PLAIN" : "ANSI");
        return;
    } else if (screen_ == S::Profile && a.metadata) {
        auto *fields = findAction("settings.edit-profile-registers")->metadata->fields;
        size_t i = a.metadata - fields;
        value = i < modemcfg::kSettingCount ? m.active().v[i]
                                            : m.active().s[i - modemcfg::kSettingCount];
    } else
        numeric = false;
    if (numeric)
        snprintf(out, cap, "%u", value);
    else if (services_)
        services_->value(a.id, out, cap);
    // Values are external data, including SSIDs and hostnames.
    for (char *p = out; *p; ++p)
        if (uint8_t(*p) < 32 || uint8_t(*p) >= 127)
            *p = '?';
}
void Terminal::describe(const UiMetadata &m) {
    char text[256];
    const char *unit = *m.displayUnit ? m.displayUnit : m.unit;
    if (m.min >= 0 && m.max >= 0) {
        snprintf(text, sizeof text, "   Range: %g-%g %s", m.min * m.displayScale,
                 m.max * m.displayScale, unit);
        emitLine(text);
    } else if (*unit) {
        emit("   Units: ");
        emitLine(unit);
    }
    if (m.maxLength >= 0) {
        snprintf(text, sizeof text, "   Length: %d-%d bytes", std::max(0, m.minLength),
                 m.maxLength);
        emitLine(text);
    }
}
void Terminal::drawMenu() {
    char text[512];
    size_t offset = offsets_[unsigned(screen_)], count = itemCount();
    for (size_t i = offset; i < count && i < offset + kPageItems; ++i) {
        const Action *a = item(i);
        Action profile{};
        if (screen_ == S::Profile) {
            profile.metadata = findAction("settings.edit-profile-registers")->metadata->fields + i;
            profile.command = "";
            profile.id = "";
            a = &profile;
        }
        const auto &m = *a->metadata;
        char value[256];
        currentValue(*a, value, sizeof value);
        const char *label = nullptr;
        for (size_t j = 0; j < m.valueCount; ++j)
            if (same(value, m.values[j].value))
                label = m.values[j].label;
        snprintf(text, sizeof text, "%c. %s", char('A' + i - offset), m.friendly);
        emit(text);
        if (*value) {
            emit(" = ");
            if (label) {
                emit(label);
                emit(" (");
                emit(value);
                emit(")");
            } else if (*m.unit && m.type == UiType::Int) {
                snprintf(text, sizeof text, "%g %s", atof(value) * m.displayScale,
                         *m.displayUnit ? m.displayUnit : m.unit);
                emit(text);
            } else
                emit(value);
        }
        if (m.readOnly)
            emit(" [read only]");
        if (rs232_ && a->rs232Unavailable)
            emit(" [unavailable: takes over RS232]");
        if (!a->available)
            emit(" [unavailable]");
        emit("\r\n");
    }
    if (screen_ == S::Phonebook && services_ && services_->phonebook()) {
        auto *book = services_->phonebook();
        size_t savedCount = 0;
        for (size_t i = 0; i < calls::kPhonebookCount; ++i)
            if (*book->at(i).number)
                ++savedCount;
        snprintf(text, sizeof text,
                 "Saved destinations: %u. A lists entries; enter a number to edit, delete or dial.",
                 unsigned(savedCount));
        emitLine(text);
    }
    if (screen_ == S::Client)
        emitLine("Client Tests run on serial only (USB console, USB modem or RS232).");
    if (offset)
        emitLine("P. Previous page");
    if (offset + kPageItems < count)
        emitLine("R. Next page");
    emitLine(screen_ == S::Main ? "Q. Exit" : "X. Back");
    emitLine("Choose a letter. Esc goes back; Ctrl-C exits; Ctrl-L prints this list again.");
}
const UiMetadata *Terminal::field() const {
    if (!editor_)
        return nullptr;
    return editor_->fieldCount ? editor_->fields + std::min(fieldIndex_, editor_->fieldCount - 1)
                               : editor_;
}
void Terminal::drawForm() {
    if (!editor_)
        return;
    char text[512];
    const UiMetadata &m = choosingVariant_ ? *editor_ : *field();
    emitLine(m.friendly);
    describe(m);
    size_t count = choosingVariant_ ? editor_->variantCount : m.valueCount;
    for (size_t i = choiceOffset_; i < count && i < choiceOffset_ + kPageItems; ++i) {
        const char *label = choosingVariant_ ? editor_->variants[i].friendly : m.values[i].label;
        snprintf(text, sizeof text, "%c. %s", char('A' + i - choiceOffset_), label);
        emitLine(text);
    }
    if (choiceOffset_)
        emitLine("P. Previous page");
    if (choiceOffset_ + kPageItems < count)
        emitLine("R. Next page");
    if (m.allowCustom)
        emitLine("S. Enter a custom value");
    emitLine(
        "Esc cancels. Backspace erases; Ctrl-U clears. Enter confirms text or a selected choice.");
    if (m.optional)
        emitLine("Optional: Enter uses the command default.");
    emit("Value > ");
    if (prompt_ && prompt_->secret)
        for (size_t i = 0; i < inputLen_; ++i)
            emit("*");
    else
        emit(input_);
}
void Terminal::drawResponse() {
    if (capturing_) {
        for (; pos_ < length_ && stageLen_ + 4 < sizeof staging_; ++pos_) {
            const char c[2] = {output_[pos_], 0};
            if (*c == '\n')
                emit("\r");
            emit(c);
        }
        if (pos_ < length_)
            dirty_ = true;
        return;
    }
    size_t offset = responseLine_, start = offset;
    size_t shown = 0;
    for (; offset < length_ && shown < kPageLines && offset - start < 4096; ++offset) {
        const char c[2] = {output_[offset], 0};
        if (*c == '\n') {
            emit("\r");
            ++shown;
        }
        emit(c);
    }
    more_ = offset < length_;
    if (overflow_)
        emitLine("\r\n[Output limit reached; use a smaller query.]");
    emitLine(more_ ? "\r\nMore: Enter/R next; P previous; X/Esc back"
                   : "\r\nEnter returns to menu. P previous; X/Esc back");
}
void Terminal::buildList() {
    dirty_ = false;
    if (capturing_) {
        drawResponse();
        return;
    }
    emit("\r\n");
    if (style_ == Style::Ansi) {
        if (color_ == Color::Rgb) {
            unsigned rgb = 0x20ddeb;
            for (const auto &theme : kModelThemes)
                if (theme.speed == session_.port().modem().model())
                    rgb = theme.rgb;
            char sgr[48];
            snprintf(sgr, sizeof sgr, "\x1b[1;38;2;%u;%u;%um", rgb >> 16, (rgb >> 8) & 255,
                     rgb & 255);
            emit(sgr);
        } else
            emit("\x1b[1;36m");
    }
    emit(modemcfg::productName(session_.port().modem().model()));
    emit(" - ");
    emitLine(response_ ? resultTitle_ : screenName(screen_));
    if (style_ == Style::Ansi)
        emit("\x1b[0m");
    if (response_)
        drawResponse();
    else if (focus_ == Focus::Confirm) {
        emitLine(prompt_ ? prompt_->metadata->friendly : "Confirm");
        if (!prompt_ || !prompt_->secret)
            emitLine(pending_);
        emitLine("Apply this action? Y confirms; N or Esc cancels.");
    } else if (focus_ == Focus::Form)
        drawForm();
    else
        drawMenu();
    if (*notice_) {
        emit("\r\n");
        emitLine(notice_);
        notice_[0] = 0;
    }
}
size_t Terminal::read(uint8_t *data, size_t cap) {
    if (diagnostic_)
        return diagnostic_->read(data, cap);
    size_t n = 0;
    while (open_ && n < cap) {
        if (stagePos_ < stageLen_) {
            size_t take = std::min(cap - n, stageLen_ - stagePos_);
            memcpy(data + n, staging_ + stagePos_, take);
            stagePos_ += take;
            n += take;
            continue;
        }
        stageLen_ = stagePos_ = 0;
        if (!dirty_)
            break;
        buildList();
    }
    return n;
}
void Terminal::profileAction(size_t index) {
    const auto &m = findAction("settings.edit-profile-registers")->metadata->fields[index];
    if (m.readOnly) {
        feedback("This setting is read only");
        return;
    }
    snprintf(profileCommand_, sizeof profileCommand_, "%s", m.commandTemplate);
    profileField_ = {S::Settings, m.friendly, profileCommand_, m.help, S::Profile, true};
    profileField_.metadata = &m;
    activate(profileField_);
}
void Terminal::activate(const Action &action) {
    if (session_.port().browser() && action.screen == S::Client) {
        feedback("Client Tests run on serial only");
        return;
    }
    if (!action.available) {
        feedback("Not available");
        return;
    }
    if (rs232_ && action.rs232Unavailable) {
        feedback("Unavailable: this test takes over RS232; use the console or browser");
        return;
    }
    if (!*action.command) {
        changeScreen(action.target);
        return;
    }
    clearInput();
    prompt_ = &action;
    editor_ = action.metadata;
    snprintf(resultTitle_, sizeof resultTitle_, "%s", editor_->friendly);
    if (action.input) {
        choosingVariant_ = editor_->variantCount != 0;
        focus_ = Focus::Form;
        render();
        return;
    }
    snprintf(pending_, sizeof pending_, "%s", action.command);
    confirmingLeave_ = action.leave;
    // Restart and factory reset own their Y/N prompt in the command handler.
    if (confirmation(action, session_.port().browser(), pending_)) {
        focus_ = Focus::Confirm;
        render();
    } else
        dispatchPending();
}
void Terminal::submit() {
    if (!prompt_ || !editor_)
        return;
    const auto &m = *field();
    if (inputOverflow_) {
        feedback("Input too long; clear (Ctrl-U) or cancel (Esc) the form");
        return;
    }
    if (!inputLen_ && !m.optional) {
        feedback("A value is required");
        return;
    }
    if (m.valueCount && inputLen_ && !choiceSelected_ && !custom_) {
        feedback("Choose a letter, then press Enter");
        return;
    }
    if (m.type == UiType::Int && *input_) {
        char *end = nullptr;
        double shown = strtod(input_, &end);
        double wire = shown / m.displayScale;
        if (*end || !std::isfinite(wire) || wire < m.min || wire > m.max ||
            fabs(wire - round(wire)) > 0.000001) {
            feedback("Enter a number within the displayed range");
            return;
        }
        snprintf(input_, sizeof input_, "%.0f", wire);
        inputLen_ = strlen(input_);
    }
    if ((m.minLength >= 0 && inputLen_ < size_t(m.minLength)) ||
        (m.maxLength >= 0 && inputLen_ > size_t(m.maxLength))) {
        feedback("The value has the wrong length; check the displayed limits");
        return;
    }
    if (editor_->fieldCount) {
        snprintf(fieldValues_[fieldIndex_], sizeof fieldValues_[0], "%s", input_);
        if (fieldIndex_ + 1 < editor_->fieldCount) {
            ++fieldIndex_;
            input_[0] = 0;
            inputLen_ = choiceOffset_ = 0;
            choiceSelected_ = custom_ = false;
            render();
            return;
        }
    }
    const char *tpl = editor_->commandTemplate;
    size_t used = 0;
    for (const char *p = tpl; *p;) {
        const char *value = nullptr;
        if (*p == '{') {
            const char *end = strchr(p, '}');
            if (!end)
                break;
            if (editor_->fieldCount) {
                for (size_t i = 0; i < editor_->fieldCount; ++i)
                    if (strlen(editor_->fields[i].id) == size_t(end - p - 1) &&
                        !strncmp(p + 1, editor_->fields[i].id, end - p - 1))
                        value = fieldValues_[i];
            } else
                value = input_;
            p = end + 1;
            if (!value) {
                feedback("This value cannot be submitted");
                return;
            }
            size_t n = strlen(value);
            if (used + n >= sizeof pending_) {
                feedback("Value too long; action not submitted");
                return;
            }
            memcpy(pending_ + used, value, n);
            used += n;
        } else {
            if (used + 1 >= sizeof pending_) {
                feedback("Value too long; action not submitted");
                return;
            }
            pending_[used++] = *p++;
        }
    }
    // Omitted optional fields are trailing command arguments (speed, DNS or duration).
    if (editor_->fieldCount && editor_->fields[editor_->fieldCount - 1].optional &&
        !*fieldValues_[editor_->fieldCount - 1] && used && pending_[used - 1] == ',')
        --used;
    pending_[used] = 0;
    if (prompt_->screen == S::Phonebook && (!strcmp(prompt_->id, "phonebook.delete-entry") ||
                                            !strcmp(prompt_->id, "phonebook.dial"))) {
        auto *book = services_ ? services_->phonebook() : nullptr;
        const auto *entry = book ? book->find(input_) : nullptr;
        if (!entry) {
            feedback("No saved entry has that number");
            return;
        }
        snprintf(confirmedNumber_, sizeof confirmedNumber_, "%s", entry->number);
        confirmedEntryHash_ = entryHash(*entry);
    }
    confirmingLeave_ = prompt_->leave;
    devicecfg::wipe(input_, sizeof input_);
    devicecfg::wipe(fieldValues_, sizeof fieldValues_);
    inputLen_ = 0;
    if (confirmation(*prompt_, session_.port().browser(), pending_)) {
        focus_ = Focus::Confirm;
        render();
    } else
        dispatchPending();
}
void Terminal::dispatchPending() {
    if (*confirmedNumber_) {
        auto *book = services_ ? services_->phonebook() : nullptr;
        const auto *e = book ? book->find(confirmedNumber_) : nullptr;
        if (!e || entryHash(*e) != confirmedEntryHash_) {
            clearInput();
            focus_ = Focus::Menu;
            feedback("Entry changed; select it and review again");
            return;
        }
    }
    char command[sizeof pending_];
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
    responseLine_ = 0;
    render();
}
void Terminal::poll(uint32_t now) {
    now_ = now;
    if (!open_)
        return;
    if (diagnostic_ || capturing_)
        activityAt_ = now;
    else if (now - activityAt_ >= kIdleCloseMs) {
        bool browser = session_.port().browser();
        close();
        if (!browser)
            session_.port().print("\r\nScreen closed after 5 minutes idle.\r\n");
        return;
    }
    if (diagnostic_) {
        diagnostic_->poll(now);
        if (!diagnostic_->isOpen()) {
            diagnostic_ = nullptr;
            clearOutput();
            feedback("Client test finished; L. Summary shows findings");
        }
        return;
    }
    if ((focus_ == Focus::Confirm && now - activityAt_ >= 30000) ||
        (focus_ == Focus::Form && prompt_ && prompt_->secret && now - activityAt_ >= 120000)) {
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
}
size_t Terminal::write(const uint8_t *data, size_t len) {
    if (diagnostic_)
        return diagnostic_->write(data, len);
    if (!open_ || !len)
        return 0;
    input(char(data[0]));
    return 1;
}
void Terminal::sequence() {
    // Geometry reports still feed transport state, including during a secret prompt.
    const char *s = escape_ + 1;
    if (!strncmp(s, "[8;", 3)) {
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
        unsigned rowCount, cols;
        if (number(rowCount) && *p++ == ';' && number(cols) && *p++ == 't' && !*p)
            resize(std::min(cols, 132u), std::min(rowCount, 50u));
    }
    escapeLen_ = 0;
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
            if (c >= 32 && c <= 126)
                return;
            key(27);
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
    if (swallowEnter_ && (c == '\r' || c == '\n')) {
        swallowEnter_ = false;
        return;
    }
    if (c >= 32 && c <= 126)
        swallowEnter_ = false;
    if (capturing_) {
        if (c == '\r' || c == '\n')
            c = char(session_.port().modem().active().s[3]);
        else if (c == '\b' || c == 127)
            c = char(session_.port().modem().active().s[5]);
        session_.port().feed(c);
        return;
    }
    key(uint8_t(c));
}
void Terminal::key(unsigned key) {
    if (capturing_) {
        if (key == 27) {
            if (!session_.port().cancelPrompt() && session_.port().busy())
                session_.port().feed(char(27));
            finishResponse();
        }
        return;
    }
    if (key == 12) {
        repaint();
        return;
    }
    if (focus_ == Focus::Form) {
        formKey(key);
        return;
    }
    if (focus_ == Focus::Confirm) {
        if (key == 'y' || key == 'Y') {
            dispatchPending();
            swallowEnter_ = true;
        } else if (key == 'n' || key == 'N' || key == 27) {
            back();
            feedback("Action cancelled");
            swallowEnter_ = true;
        }
        return;
    }
    unsigned upper = std::toupper(uint8_t(key));
    if (key == 27 || upper == 'X' || upper == 'Q') {
        back();
        return;
    }
    if (response_) {
        if (upper == 'P') {
            size_t previous = 0, next = 0;
            while (next < responseLine_) {
                previous = next;
                size_t start = next, lines = 0;
                while (next < length_ && lines < kPageLines && next - start < 4096)
                    if (output_[next++] == '\n')
                        ++lines;
            }
            responseLine_ = previous;
        } else if (key == '\r' || key == '\n' || key == ' ' || upper == 'R') {
            if (more_) {
                size_t start = responseLine_, lines = 0;
                while (responseLine_ < length_ && lines < kPageLines &&
                       responseLine_ - start < 4096)
                    if (output_[responseLine_++] == '\n')
                        ++lines;
            } else {
                back();
                return;
            }
        } else
            return;
        render();
        return;
    }
    size_t &offset = offsets_[unsigned(screen_)];
    if (upper == 'P' && offset) {
        offset -= kPageItems;
        render();
    } else if (upper == 'R' && offset + kPageItems < itemCount()) {
        offset += kPageItems;
        render();
    } else if (upper >= 'A' && upper < 'A' + kPageItems && offset + upper - 'A' < itemCount()) {
        size_t i = offset + upper - 'A';
        if (screen_ == S::Profile)
            profileAction(i);
        else
            activate(*item(i));
        swallowEnter_ = true;
    }
}
void Terminal::formKey(unsigned key) {
    if (key == 27) {
        back();
        feedback("Edit cancelled");
        return;
    }
    const auto &m = choosingVariant_ ? *editor_ : *field();
    size_t count = choosingVariant_ ? editor_->variantCount : m.valueCount;
    unsigned upper = std::toupper(uint8_t(key));
    if (count && !custom_) {
        if (upper == 'P' && choiceOffset_) {
            choiceOffset_ -= kPageItems;
            render();
            return;
        }
        if (upper == 'R' && choiceOffset_ + kPageItems < count) {
            choiceOffset_ += kPageItems;
            render();
            return;
        }
        if (upper == 'S' && m.allowCustom) {
            custom_ = true;
            input_[0] = 0;
            inputLen_ = 0;
            emitLine("\r\nCustom value > ");
            return;
        }
        if (upper >= 'A' && upper < 'A' + kPageItems && choiceOffset_ + upper - 'A' < count) {
            size_t i = choiceOffset_ + upper - 'A';
            if (choosingVariant_) {
                editor_ = editor_->variants + i;
                choosingVariant_ = false;
                choiceOffset_ = 0;
                fieldIndex_ = 0;
                if (!editor_->fieldCount && !strchr(editor_->commandTemplate, '{')) {
                    snprintf(pending_, sizeof pending_, "%s", editor_->commandTemplate);
                    confirmingLeave_ = prompt_->leave;
                    focus_ = Focus::Confirm;
                }
                render();
                swallowEnter_ = true;
                return;
            }
            snprintf(input_, sizeof input_, "%s", m.values[i].value);
            inputLen_ = strlen(input_);
            choiceSelected_ = true;
            emit("\r\nSelected: ");
            emitLine(m.values[i].label);
            emit("Enter confirms > ");
            return;
        }
        if (key == '\r' || key == '\n')
            submit();
        return;
    }
    if (key == '\r' || key == '\n') {
        emit("\r\n");
        submit();
        return;
    }
    if (key == 21) {
        devicecfg::wipe(input_, sizeof input_);
        inputLen_ = 0;
        inputOverflow_ = false;
        emitLine("\r\nValue cleared.");
        emit("Value > ");
        return;
    }
    if ((key == backspace_ || key == 8 || key == 127) && inputLen_) {
        input_[--inputLen_] = 0;
        emit("\b \b");
        return;
    }
    if (key >= 32 && key < 127) {
        if (inputLen_ + 1 >= sizeof input_) {
            if (!inputOverflow_)
                feedback("Input too long; clear (Ctrl-U) or cancel (Esc) the form");
            inputOverflow_ = true;
            return;
        }
        input_[inputLen_++] = char(key);
        input_[inputLen_] = 0;
        const char echo[2] = {prompt_ && prompt_->secret ? '*' : char(key), 0};
        emit(echo);
    }
}
}  // namespace config
