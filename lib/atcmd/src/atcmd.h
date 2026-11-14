// AT command parser (spec 3.2, 3.6, 3.7, 3.8), following USR Sportster 33.6
// behaviour:
//   - "AT" or "at" (not mixed) starts a command line; S3 ends it, S5 deletes a character
//     (DEL works too); E sets echo. "A/" repeats the last line at once, with no Enter.
//   - Several commands per line ("ATE1V1X4S7=45&W"). The first bad command stops the
//     line with ERROR; commands before it stay done.
//   - B E F L M Q V X, P T, H, O, In, Sr=n Sr? Sr.b=n, Yn, Zn, &F &W &V &Z and the other
//     & settings, +FCLASS. Project commands are AT* (one table, the rest of the line is
//     the command's argument).
//   - O (back online), H0 and Z hang-ups go to the port's Link (lib/hostport).
//   - An AT* command can ask for a secret (the Wi-Fi password) at a masked prompt, and
//     can send its result later (a Wi-Fi join or scan) without blocking the other port.
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
    // Output nobody on this port asked for (RING). A serial port drops it at once when it
    // has no room; by default it is written like any other output.
    virtual void writeUnsolicited(const char *data, size_t len) { write(data, len); }
};

class Port;

// The call handling behind a port (lib/hostport). Optional: a port without one has no
// calls, and ATO gives ERROR.
class Link {
public:
    virtual ~Link() = default;
    virtual bool callUp() const = 0;  // a call is connected (ATI4 shows OFF HOOK)
    virtual void resumeOnline() = 0;  // ATO with a call up: send CONNECT, back to the call
    virtual void hangUp() = 0;        // ATH0 or ATZ with a call up
    virtual bool answer() { return false; }
    virtual bool answering() const { return false; }
    virtual bool dial(const char *text, bool pulse) {
        (void)text;
        (void)pulse;
        return false;
    }
};

// What an AT* command handler returns.
enum class Reply : uint8_t {
    kOk,     // send OK
    kError,  // send ERROR
    kDone,   // the handler already sent its own final result
    // The result comes later: the handler called Port::askSecret() or Port::wait().
    kPending,
};

using StarHandler = Reply (*)(Port &port, const char *args);
// Gets the text typed at a masked prompt. It may return kPending again (after
// Port::wait()). The text is wiped as soon as it returns, so keep a copy if needed.
using SecretHandler = Reply (*)(Port &port, const char *text, void *ctx);
// Called when a key cancels a wait.
using CancelHandler = void (*)(void *ctx);

struct StarCommand {
    const char *name;   // after "AT*", e.g. "VER"
    const char *usage;  // for AT*HELP, e.g. "AT*VER"
    const char *help;   // one line for AT*HELP
    StarHandler fn;
    bool prefix;     // true: matches any command starting with `name` (e.g. "HW" for AT*HWLED)
    bool sensitive;  // mask arguments, wipe input, refuse A/ (passwords/destructive commands)
    constexpr StarCommand()
        : name(""), usage(""), help(""), fn(nullptr), prefix(false), sensitive(false) {}
    constexpr StarCommand(const char *n, const char *u, const char *h, StarHandler f, bool p,
                          bool s = false)
        : name(n), usage(u), help(h), fn(f), prefix(p), sensitive(s) {}
};

constexpr size_t kLineMax = 128;   // characters after "AT"
constexpr size_t kSecretMax = 64;  // characters at a masked prompt (a WPA key is up to 64)

// Shared state: active settings, stored templates, the AT* table.
class Modem {
public:
    Modem(modemcfg::Store &store, const StarCommand *star, size_t starCount);

    // Loads the stored settings and applies the power-on template (Y). Call once at boot.
    modemcfg::LoadResult begin();

    modemcfg::Profile &active() { return active_; }
    const modemcfg::Profile &active() const { return active_; }
    const modemcfg::Nvram &nvram() const { return nvram_; }

    bool writeTemplate(unsigned n, uint32_t baud);  // &Wn from a port at `baud`
    uint32_t hostBaud() const { return nvram_.hostBaud; }
    bool setHostBaud(uint32_t baud);  // AT*BAUD: 0 = detect from "AT" (saved at once)
    bool setPowerOn(unsigned y);      // Yn (saved at once)
    bool setPhone(unsigned slot, const char *number);  // &Zn=s (saved at once)
    void reset(unsigned z);                            // Zn
    const char *lastDialed() const { return lastDialed_; }
    void setLastDialed(const char *text);
    uint32_t model() const { return nvram_.model; }
    bool setModel(uint32_t speed);
    bool setThrottle(bool enabled);
    bool throttle() const { return nvram_.throttle; }
    modemcfg::AnswerPort answerPort() const { return nvram_.answerPort; }
    bool setAnswerPort(modemcfg::AnswerPort port);  // saved at once
    uint32_t ringDelayMs() const { return nvram_.ringDelayMs; }
    uint32_t connectDelayMs() const { return nvram_.connectDelayMs; }
    bool setCallDelay(bool ring, uint32_t milliseconds);

    const StarCommand *starTable() const { return star_; }
    size_t starCount() const { return starCount_; }
    void stopCommands() { stopped_ = true; }  // factory reset is awaiting reboot
    bool commandsStopped() const { return stopped_; }
    // Finds the AT* command for `text` (the part after "AT*"); *args is set to the text
    // after the command's name. Longest name wins. nullptr if none.
    const StarCommand *findStar(const char *text, const char **args) const;

private:
    modemcfg::Store &store_;
    const StarCommand *star_;
    size_t starCount_;
    modemcfg::Nvram nvram_;
    modemcfg::Profile active_;
    char lastDialed_[kLineMax + 1];
    bool stopped_ = false;
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
    void redirect(Sink *sink, bool readable = false) {
        redirected_ = sink;
        readable_ = sink && readable;
    }
    // Counts writes that went to the port itself (not redirected). A configuration screen
    // drawn by changes only repaints in full after anyone else wrote to its terminal.
    uint32_t directWrites() const { return directWrites_; }
    void print(const char *text);
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void newline();               // S3 S4
    void line(const char *text);  // text, then S3 S4
    void beginInfo();             // starts an information response (S3 S4 in verbal mode)
    void result(Result r);        // applies V, Q and X
    void unsolicited(Result r);   // result() for output nobody asked for (RING)

    // --- Prompts and late results, for AT* handlers (return Reply::kPending after) -------
    // Prints `prompt` and reads one line as a secret: each character echoes as '*' (only
    // with E1), Backspace/DEL delete, Enter hands the text to `fn`, Esc or Ctrl-C cancels
    // with ERROR. The text is never echoed, never kept for A/, and wiped after `fn`.
    void askSecret(const char *prompt, SecretHandler fn, void *ctx);
    // A visible line prompt for Y/N confirmation; uses the same cancellation rules.
    void askConfirmation(const char *prompt, SecretHandler fn, void *ctx);
    bool cancelPrompt();  // timeout: wipe the pending input and send ERROR
    // The final result comes later, through finish(). Meanwhile typing is not a command;
    // any key (not LF) cancels: `cancel` is called and ERROR sent.
    void wait(CancelHandler cancel, void *ctx, Result cancelResult = Result::kError,
              bool answerCall = false);  // Q2 also suppresses answer-handshake cancellation
    // Sends the result of a wait (nothing for kDone). False if the port isn't waiting
    // (the wait was cancelled).
    bool finish(Reply r);
    bool busy() const { return state_ == State::kSecret || state_ == State::kWait; }
    // Nothing typed since the last result: an unsolicited message can be printed.
    bool lineEmpty() const { return state_ == State::kIdle; }

    Modem &modem() { return modem_; }
    uint32_t baud() const { return baud_; }
    void setBaud(uint32_t baud) { baud_ = baud; }
    void setLink(Link *link) { link_ = link; }
    Link *link() const { return link_; }
    bool callUp() const { return link_ != nullptr && link_->callUp(); }
    bool browser() const { return browser_; }
    void setBrowser(bool value) { browser_ = value; }
    void clientBackspace(uint8_t code) { clientBackspace_ = code; }

private:
    enum class State : uint8_t { kIdle, kGotA, kLine, kSecret, kWait };

    void feedSecret(char c, bool isBs);
    void feedWait(char c);
    void wipeSecret();
    bool runCommand(const char *&p, bool *stop);
    bool runAmpersand(const char *&p, bool *stop);
    bool runSRegister(const char *&p);
    bool runPlus(const char *&p);
    void echo(const char *s, size_t len);

    Modem &modem_;
    Sink &out_;
    Sink *redirected_ = nullptr;
    bool readable_ = false;
    uint32_t directWrites_ = 0;
    bool browser_ = false;
    uint8_t clientBackspace_ = 0;
    uint32_t baud_;
    Link *link_ = nullptr;
    bool resultSent_ = false;   // a command (ATO) sent the line's final result itself
    bool unsolicited_ = false;  // write() is sending an unsolicited result
    State state_ = State::kIdle;
    char aCase_ = 'A';
    char line_[kLineMax + 1] = {};
    size_t len_ = 0;
    bool overflow_ = false;
    bool privateLine_ = false;
    size_t privateStart_ = 0;  // end of sensitive command name; delimiter is echoed once
    bool repeatAllowed_ = true;
    char last_[kLineMax + 1] = {};
    unsigned selected_ = 0;  // register for "AT=n" / "AT?"
    // Masked prompt and wait (askSecret, wait)
    SecretHandler secretFn_ = nullptr;
    CancelHandler cancelFn_ = nullptr;
    Result cancelResult_ = Result::kError;
    bool answerWait_ = false;
    void *ctx_ = nullptr;
    char secret_[kSecretMax + 1] = {};
    size_t secretLen_ = 0;
    bool secretOverflow_ = false;
    bool maskSecret_ = true;
};

// --- ATI and settings screens (identity.cpp) -----------------------------------------------
// ATIn. False if n is not an inquiry the modem has.
bool showInfo(Port &port, unsigned n);
void showCurrent(Port &port);  // ATI4
void showStored(Port &port);   // ATI5
// AT*HELP: one line per AT* command, in table order.
void showStarHelp(Port &port);
Reply cmdModel(Port &port, const char *args);
Reply cmdThrottle(Port &port, const char *args);
Reply cmdRingDelay(Port &port, const char *args);
Reply cmdConnectDelay(Port &port, const char *args);

}  // namespace atcmd
