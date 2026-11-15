// Shared local configuration model, commands and terminal. No hardware dependencies.
#pragma once
#include "devicecfg.h"
#include "diagnostics.h"
#include "netcfg.h"
#include "phonebook.h"
#include "session.h"

namespace config {
enum class Screen : uint8_t {
    Main,
    Settings,
    Profile,
    Phonebook,
    Wifi,
    Updates,
    Sounds,
    Debug,
    Hardware,
    Client,
    Options,
    Help,
    Clock,
    Reset,
    Count
};
enum class UiType : uint8_t { Enum, Int, String, Bool, Ip, Action, Secret };
struct UiValue {
    const char *value;
    const char *label;
    const char *description;
};
struct UiMetadata {
    const char *id;
    const char *friendly;
    const char *help;
    UiType type;
    const char *unit;
    int min, max, minLength, maxLength;  // -1 means unspecified; lengths are UTF-8 bytes.
    const char *pattern;                 // Portable regular expression; full-string match.
    const UiValue *values;
    size_t valueCount;
    const char *commandTemplate;  // Raw command without AT; named {placeholders}.
    const UiMetadata *fields;
    size_t fieldCount;
    const UiMetadata *variants;
    size_t variantCount;
    bool optional, allowCustom, readOnly;
    const char *displayUnit;
    double displayScale;  // Multiply the wire value for display (ms -> seconds: 0.001).
};
struct Action {
    Screen screen;
    const char *label;
    const char *command;
    const char *help;
    Screen target;
    bool input = false;
    bool secret = false;
    bool rs232Unavailable = false;
    bool leave = false;
    const char *id = "";
    char key = 0;
    bool available = true;
    const char *summary = "";
    const UiMetadata *metadata = nullptr;
};
const Action *actions(size_t &count);
const char *screenName(Screen screen);
const char *settingHelp(modemcfg::Setting setting);
const char *registerHelp(unsigned reg);
enum class Style : uint8_t { Ansi, Plain };
enum class Color : uint8_t { Ansi16, Ansi256, Rgb };
struct UiState {
    char link[24] = "Unknown";
    uint32_t hostBaud = 0;
    uint32_t reportedRate = 0;
};
class UiServices {
public:
    virtual ~UiServices() = default;
    virtual calls::Phonebook *phonebook() = 0;
    virtual UiState state() const = 0;
    // Public setting values only. Never return credentials or stored hashes.
    virtual bool value(const char *, char *, size_t) const { return false; }
};
class Hardware {
public:
    virtual ~Hardware() = default;
    virtual atcmd::Reply command(atcmd::Port &port, const char *name, const char *args) = 0;
};
class Commands {
public:
    Commands(Log &log, History &history, calls::Phonebook &book, netcfg::Net &net,
             devicecfg::Device &device, Hardware &hardware)
        : log_(log),
          history_(history),
          book_(book),
          net_(net),
          device_(device),
          hardware_(hardware) {}
    bool value(const char *id, char *out, size_t cap) const;
    atcmd::Reply command(atcmd::Port &port, const char *name, const char *args);
    static atcmd::Reply dispatch(atcmd::Port &port, const char *name, const char *args);
    static void attach(Commands &commands);

private:
    atcmd::Reply exportConfig(atcmd::Port &port);
    atcmd::Reply setting(atcmd::Port &port, const char *args);
    atcmd::Reply explicitRate(atcmd::Port &port, const char *args);
    atcmd::Reply restoreEnd(atcmd::Port &port, const char *args);
    atcmd::Reply profile(atcmd::Port &port, const char *args);
    atcmd::Reply profileControl(atcmd::Port &port, const char *args);
    atcmd::Reply bookClear(atcmd::Port &port, const char *args);
    atcmd::Reply dial(atcmd::Port &port, const char *args);
    atcmd::Reply reference(atcmd::Port &port, const char *args);
    atcmd::Reply about(atcmd::Port &port, const char *args);
    atcmd::Reply github(atcmd::Port &port, const char *args);
    atcmd::Reply exportCommand(atcmd::Port &port, const char *args);
    atcmd::Reply logCommand(atcmd::Port &port, const char *args);
    atcmd::Reply callLog(atcmd::Port &port, const char *args);
    Log &log_;
    History &history_;
    calls::Phonebook &book_;
    netcfg::Net &net_;
    devicecfg::Device &device_;
    Hardware &hardware_;
};

// Output is pulled by Session in UART-sized chunks. Command output is captured and
// paged; an explicit truncation message replaces silent overflow. No blocking writes.
class Terminal : public hostport::Connection, public atcmd::Sink {
public:
    static constexpr size_t kOutputSize = 32768;
    static constexpr uint32_t kIdleCloseMs = 5u * 60u * 1000u;  // no key: the screen closes
    Terminal(hostport::Session &session, bool rs232) : session_(session), rs232_(rs232) {}
    bool open();
    bool isOpen() const override { return open_; }
    bool localService() const override { return true; }
    bool binaryService() const override { return diagnostic_ && diagnostic_->binaryService(); }
    void diagnostic(hostport::Connection &connection) {
        diagnostic_ = &connection;
        stagePos_ = stageLen_ = 0;
        dirty_ = false;
    }
    void backspace(uint8_t code) { backspace_ = code; }
    void escaped() override;
    void close() override;
    size_t space() const override { return diagnostic_ ? diagnostic_->space() : open_ ? 1 : 0; }
    size_t write(const uint8_t *data, size_t len) override;
    void write(const char *data, size_t len) override;
    size_t read(uint8_t *data, size_t cap) override;
    void poll(uint32_t now) override;
    Style style() const { return style_; }
    bool setStyle(const char *text);
    bool takeDeparture(char *command, size_t cap);
    bool capturing() const { return capturing_; }
    void services(UiServices *services) { services_ = services; }
    bool resize(unsigned columns, unsigned rows);
    bool setSize(const char *text);
    unsigned columns() const { return cols_; }
    unsigned rows() const { return rows_; }
    void color(Color color) {
        color_ = color;
        render();
    }
    Color color() const { return color_; }
    // Print the current list again (Ctrl-L).
    void repaint() { render(); }

private:
    enum class Focus : uint8_t { Menu, Form, Confirm };
    static constexpr size_t kPageItems = 15, kPageLines = 15;
    void render();
    void buildList();
    void drawMenu();
    void drawForm();
    void drawResponse();
    void emit(const char *text);
    void emitLine(const char *text);
    void describe(const UiMetadata &metadata);
    void currentValue(const Action &action, char *out, size_t cap) const;
    const UiMetadata *field() const;
    void input(char c);
    void sequence();
    void key(unsigned key);
    void formKey(unsigned key);
    void activate(const Action &action);
    void submit();
    void dispatchPending();
    void execute(const char *command);
    void finishResponse();
    void back();
    void changeScreen(Screen screen);
    void feedback(const char *text);
    void clearInput();
    void clearOutput();
    void line(const char *text);
    void append(const char *text);
    const Action *item(size_t index) const;
    size_t itemCount() const;
    void profileAction(size_t index);
    hostport::Session &session_;
    bool rs232_;
    bool open_ = false, capturing_ = false, response_ = false, more_ = false;
    bool overflow_ = false, skipLf_ = false, departure_ = false;
    Style style_ = Style::Ansi;
    Color color_ = Color::Ansi16;
    Screen screen_ = Screen::Main;
    Focus focus_ = Focus::Menu;
    Screen stack_[10] = {};
    unsigned depth_ = 0;
    size_t offsets_[static_cast<unsigned>(Screen::Count)] = {};
    unsigned cols_ = 80, rows_ = 25;
    bool dirty_ = false;
    char staging_[16384] = {};
    size_t stageLen_ = 0, stagePos_ = 0;
    char output_[kOutputSize] = {};
    size_t length_ = 0, pos_ = 0, responseLine_ = 0;
    unsigned responseLines_ = 0;
    char input_[atcmd::kLineMax + 1] = {};
    size_t inputLen_ = 0;
    bool inputOverflow_ = false;
    const Action *prompt_ = nullptr;
    const UiMetadata *editor_ = nullptr;
    size_t fieldIndex_ = 0, choiceOffset_ = 0;
    bool choosingVariant_ = false, choiceSelected_ = false, custom_ = false;
    char fieldValues_[8][atcmd::kLineMax + 1] = {};
    char pending_[atcmd::kLineMax + 1] = {};
    char departureCommand_[atcmd::kLineMax + 1] = {};
    bool swallowEnter_ = false, confirmingLeave_ = false;
    char notice_[144] = {};
    char resultTitle_[64] = "Result";
    Action profileField_{};
    char profileCommand_[32] = {};
    char confirmedNumber_[12] = {};
    uint32_t confirmedEntryHash_ = 0;
    UiServices *services_ = nullptr;
    hostport::Connection *diagnostic_ = nullptr;
    uint8_t backspace_ = 8;
    unsigned escapeLen_ = 0;
    char escape_[24] = {};
    bool escapeDiscard_ = false;
    uint32_t now_ = 0, escapeAt_ = 0, activityAt_ = 0;
};
}  // namespace config
