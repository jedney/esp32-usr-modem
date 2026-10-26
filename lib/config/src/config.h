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
    Hardware
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
};
const Action *actions(size_t &count);
const char *screenName(Screen screen);
const char *settingHelp(modemcfg::Setting setting);
const char *registerHelp(unsigned reg);
enum class Style : uint8_t { Ansi, Cp437, Plain };
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
    Terminal(hostport::Session &session, bool rs232) : session_(session), rs232_(rs232) {}
    bool open();
    bool isOpen() const override { return open_; }
    bool localService() const override { return true; }
    void escaped() override;
    void close() override;
    size_t space() const override { return open_ ? 1 : 0; }
    size_t write(const uint8_t *data, size_t len) override;
    void write(const char *data, size_t len) override;
    size_t read(uint8_t *data, size_t cap) override;
    void poll(uint32_t now) override;
    Style style() const { return style_; }
    bool setStyle(const char *text);
    bool takeDeparture(char *command, size_t cap);
    bool capturing() const { return capturing_; }

private:
    void render();
    void line(const char *text);
    void append(const char *text);
    void input(char c);
    void select();
    void execute(const char *command);
    void clearOutput();
    void finishResponse();
    hostport::Session &session_;
    bool rs232_;
    bool open_ = false, capturing_ = false, response_ = false, more_ = false;
    bool overflow_ = false, skipLf_ = false, departure_ = false;
    Style style_ = Style::Ansi;
    Screen screen_ = Screen::Main;
    size_t page_ = 0, length_ = 0, pos_ = 0, pageEnd_ = 0;
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
};
}  // namespace config
