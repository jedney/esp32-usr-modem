// AT command parser (project brief 6.2, 6.6, 6.7, 6.8), following USR Sportster 33.6
// behaviour:
//   - "AT" or "at" (not mixed) starts a command line; S3 ends it, S5 deletes a character
//     (DEL works too); E sets echo. "A/" repeats the last line at once, with no Enter.
//   - Several commands per line ("ATE1V1X4S7=45&W"). The first bad command stops the
//     line with ERROR; commands before it stay done.
//   - B E F L M Q V X, P T, H, In, Sr=n Sr? Sr.b=n, Yn, Zn, &F &W &V &Z and the other
//     & settings, +FCLASS. Project commands are AT* (one table, the rest of the line is
//     the command's argument).
//
// One Modem holds the shared settings; each serial port has its own Port (own line
// buffer and A/ memory). No Arduino dependencies: unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

#include "modemcfg.h"
#include "results.h"

namespace atcmd {

// Where a port's output goes (the console's Serial, UART1, a test buffer).
class Sink {
public:
    virtual ~Sink() = default;
    virtual void write(const char *data, size_t len) = 0;
};

class Port;

// What an AT* command handler returns.
enum class Reply : uint8_t {
    kOk,     // send OK
    kError,  // send ERROR
    kDone,   // the handler already sent its own final result
};

using StarHandler = Reply (*)(Port &port, const char *args);

struct StarCommand {
    const char *name;   // after "AT*", e.g. "VER"
    const char *usage;  // for AT*HELP, e.g. "AT*VER"
    const char *help;   // one line for AT*HELP
    StarHandler fn;
    bool prefix;  // true: matches any command starting with `name` (e.g. "HW" for AT*HWLED)
};

constexpr size_t kLineMax = 128;  // characters after "AT"

// Shared state: active settings, stored templates, the AT* table.
class Modem {
public:
    Modem(modemcfg::Store &store, const StarCommand *star, size_t starCount);

    // Loads the stored settings and applies the power-on template (Y). Call once at boot.
    modemcfg::LoadResult begin();

    modemcfg::Profile &active() { return active_; }
    const modemcfg::Profile &active() const { return active_; }
    const modemcfg::Nvram &nvram() const { return nvram_; }

    bool writeTemplate(unsigned n);                    // &Wn
    bool setPowerOn(unsigned y);                       // Yn (saved at once)
    bool setPhone(unsigned slot, const char *number);  // &Zn=s (saved at once)
    void reset(unsigned z);                            // Zn
    const char *lastDialed() const { return lastDialed_; }

    const StarCommand *starTable() const { return star_; }
    size_t starCount() const { return starCount_; }
    // Finds the AT* command for `text` (the part after "AT*"); *args is set to the text
    // after the command's name. Longest name wins. nullptr if none.
    const StarCommand *findStar(const char *text, const char **args) const;

private:
    modemcfg::Store &store_;
    const StarCommand *star_;
    size_t starCount_;
    modemcfg::Nvram nvram_;
    modemcfg::Profile active_;
    char lastDialed_[modemcfg::kPhoneLen + 1];
};

class Port {
public:
    // `baud` is the port's current speed, shown as BAUD= in ATI4.
    Port(Modem &modem, Sink &out, uint32_t baud);

    // One received character while in command mode.
    void feed(char c);

    // Runs one command line: the text after "AT" (e.g. "E0V1"). Sends the final result.
    void execute(const char *line);

    // --- Output, for AT* handlers ---------------------------------------------------------
    void write(const char *data, size_t len);
    void print(const char *text);
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void newline();               // S3 S4
    void line(const char *text);  // text, then S3 S4
    void beginInfo();             // starts an information response (S3 S4 in verbal mode)
    void result(Result r);        // applies V, Q and X

    Modem &modem() { return modem_; }
    uint32_t baud() const { return baud_; }
    void setBaud(uint32_t baud) { baud_ = baud; }

private:
    enum class State : uint8_t { kIdle, kGotA, kLine };

    bool runCommand(const char *&p, bool *stop);
    bool runAmpersand(const char *&p, bool *stop);
    bool runSRegister(const char *&p);
    bool runPlus(const char *&p);
    void echo(const char *s, size_t len);

    Modem &modem_;
    Sink &out_;
    uint32_t baud_;
    State state_ = State::kIdle;
    char aCase_ = 'A';
    char line_[kLineMax + 1] = {};
    size_t len_ = 0;
    bool overflow_ = false;
    char last_[kLineMax + 1] = {};
    unsigned selected_ = 0;  // register for "AT=n" / "AT?"
};

// --- ATI and settings screens (identity.cpp) -----------------------------------------------
// ATIn. False if n is not an inquiry the modem has.
bool showInfo(Port &port, unsigned n);
void showCurrent(Port &port);  // ATI4
void showStored(Port &port);   // ATI5
// AT*HELP: one line per AT* command, in table order.
void showStarHelp(Port &port);

}  // namespace atcmd
