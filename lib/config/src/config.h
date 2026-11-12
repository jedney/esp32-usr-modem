// Shared local configuration model, commands and terminal. No hardware dependencies.
#pragma once
#include "cell_grid.h"
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
    Client
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
};
const Action *actions(size_t &count);
const char *screenName(Screen screen);
const char *settingHelp(modemcfg::Setting setting);
const char *registerHelp(unsigned reg);
enum class Style : uint8_t { Ansi, Cp437, Plain };
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
        drawing_ = false;
        shown_.valid = false;  // the test owns the terminal until it ends
        stagePos_ = stageLen_ = 0;
        if (!connection.binaryService() && style_ != Style::Plain) {
            const char prefix[] = "\x18\x1b[0m\x1b[?7h\x1b[?25h\x1b[2J\x1b[H";
            for (char c : prefix)
                if (c)
                    staging_[stageLen_++] = c;
        }
        dirty_ = false;
        querySize_ = false;
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
    // Sends a full frame next instead of only the changes: Ctrl-L, or after something
    // wrote to the terminal behind the screen (a hardware test on the console or browser).
    void repaint() {
        shown_.valid = false;
        render();
    }

private:
    void render();
    void planFrame();
    bool stageExport();
    void line(const char *text);
    void append(const char *text);
    void input(char c);
    void buildFrame();
    void drawSmall();
    void drawHeader(bool main);
    void drawFooter(bool main);
    const char *keyHints() const;
    void drawMenu();
    void drawMainMenu();
    void drawPhonebook();
    void drawPhonebookPanel(const calls::Entry *selected);
    void drawForm();
    void drawResponse();
    void key(unsigned key);
    void capturedKey(unsigned key);
    void pageKey(unsigned key);
    void helpKey(unsigned key);
    void formKey(unsigned key);
    void typeCharacter(char c);
    void confirmKey(unsigned key);
    void responseKey(unsigned key);
    void exportKey(unsigned key);
    void menuKey(unsigned key);
    bool moveKey(unsigned key);
    void showEntryDetails();
    void hotkey(unsigned key);
    void sequence();
    void activate(const Action &action);
    void submit();
    void dispatchPending();
    void back();
    void help();
    void navigate(int direction);
    void changeScreen(Screen screen);
    void feedback(const char *text);
    void clearInput();
    void syncPhonebook();
    const calls::Entry *entry() const;
    const Action *item(size_t index) const;
    size_t itemCount() const;
    size_t visibleItems() const;
    size_t responseRows() const;
    size_t phonebookPageSize() const;
    bool enabled(size_t index) const;
    void profileAction(size_t index);
    void execute(const char *command);
    void clearOutput();
    void finishResponse();
    hostport::Session &session_;
    bool rs232_;
    bool open_ = false, capturing_ = false, response_ = false, more_ = false;
    bool overflow_ = false, skipLf_ = false, departure_ = false;
    Style style_ = Style::Ansi;
    Screen screen_ = Screen::Main;
    size_t length_ = 0, pos_ = 0;
    unsigned lines_ = 0;
    unsigned column_ = 0;
    bool liveStatus_ = false;
    bool exporting_ = false;
    uint32_t refreshedAt_ = 0;
    char output_[kOutputSize] = {};
    char input_[atcmd::kLineMax + 1] = {};
    size_t inputLen_ = 0;
    bool inputOverflow_ = false;
    const Action *prompt_ = nullptr;
    char departureCommand_[atcmd::kLineMax + 1] = {};
    UiServices *services_ = nullptr;
    CellGrid grid_;
    unsigned cols_ = 80, rows_ = 25;
    Color color_ = Color::Ansi16;
    Style drawStyle_ = Style::Ansi;
    Color drawColor_ = Color::Ansi16;
    unsigned drawModelRgb_ = 0;
    bool dirty_ = false, drawing_ = false, querySize_ = false;
    bool styled_ = false;
    unsigned drawRow_ = 0;
    // What the terminal shows (PERF-01). A frame sends only the cells that differ from it,
    // unless it is a full frame: first frame, new size, style, colours or theme, Ctrl-L,
    // or other output on the port since (Port::directWrites()). Plain text is always full.
    struct Shown {
        bool valid = false;
        Style style = Style::Ansi;
        Color color = Color::Ansi16;
        unsigned modelRgb = 0, cursorRow = 0, cursorCol = 0;
        uint32_t directWrites = 0;
    } shown_;
    CellGrid shownGrid_;
    bool fullFrame_ = true;
    uint32_t frameWrites_ = 0;
    char staging_[4096] = {};
    size_t stageLen_ = 0, stagePos_ = 0;
    enum class Focus : uint8_t { Menu, Form, Confirm, Details, Help };
    Focus focus_ = Focus::Menu;
    Focus helpReturn_ = Focus::Menu;
    struct Location {
        size_t selected = 0, offset = 0;
    } locations_[10];
    Screen stack_[10] = {};
    unsigned depth_ = 0;
    hostport::Connection *diagnostic_ = nullptr;
    uint8_t backspace_ = 8;
    size_t cursor_ = 0, responseLine_ = 0;
    size_t helpReturnLine_ = 0;
    unsigned responseLines_ = 0, drawCursorRow_ = 24, drawCursorCol_ = 13;
    unsigned escapeLen_ = 0;
    char escape_[24] = {};
    bool escapeDiscard_ = false;
    uint32_t now_ = 0, escapeAt_ = 0, model_ = 0;
    uint32_t activityAt_ = 0;
    uint32_t sizeQueriedAt_ = 0;
    bool exportPrompt_ = false, exportFinished_ = false;
    bool swallowEnter_ = false, confirmingLeave_ = false;
    char pending_[atcmd::kLineMax + 1] = {};
    char notice_[144] = {};
    char resultTitle_[64] = "Result";
    Action profileField_{};
    char profileCommand_[32] = {}, profileHelp_[384] = {};
    unsigned bookIndices_[calls::kPhonebookCount] = {};
    size_t bookCount_ = 0;
    char selectedNumber_[12] = {};
    char confirmedNumber_[12] = {};
    uint32_t confirmedEntryHash_ = 0;
};
}  // namespace config
