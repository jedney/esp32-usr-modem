// AT command parser: see atcmd.h.
#include "atcmd.h"

#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace atcmd {

using modemcfg::Profile;
using modemcfg::Setting;

namespace {

char upper(char c) {
    return static_cast<char>(toupper(static_cast<unsigned char>(c)));
}

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

void skipSpaces(const char *&p) {
    while (*p == ' ') {
        ++p;
    }
}

// Optional decimal number; a missing number is 0 (Hayes rule). False if it is > max.
bool number(const char *&p, unsigned max, unsigned *out) {
    skipSpaces(p);
    unsigned v = 0;
    while (isDigit(*p)) {
        v = v * 10 + static_cast<unsigned>(*p - '0');
        if (v > max) {
            return false;
        }
        ++p;
    }
    *out = v;
    return true;
}

// Case-insensitive: does `s` start with `word`?
bool startsWith(const char *s, const char *word) {
    for (; *word != '\0'; ++s, ++word) {
        if (upper(*s) != upper(*word)) {
            return false;
        }
    }
    return true;
}

void copyTrimmed(char *dst, size_t cap, const char *src) {
    while (*src == ' ') {
        ++src;
    }
    size_t n = strlen(src);
    while (n > 0 && src[n - 1] == ' ') {
        --n;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memmove(dst, src, n);
    dst[n] = '\0';
}

}  // namespace

// --- Modem ----------------------------------------------------------------------------------

Modem::Modem(modemcfg::Store &store, const StarCommand *star, size_t starCount)
    : store_(store),
      star_(star),
      starCount_(starCount),
      nvram_(modemcfg::factoryNvram()),
      active_(modemcfg::factoryProfile(1)),
      lastDialed_() {}

modemcfg::LoadResult Modem::begin() {
    modemcfg::LoadResult r = modemcfg::loadNvram(store_, &nvram_);
    active_ = modemcfg::powerOnProfile(nvram_);
    if (r == modemcfg::LoadResult::kMigrated) {
        modemcfg::saveNvram(store_, nvram_);  // in the current format from now on
    }
    return r;
}

bool Modem::writeTemplate(unsigned n, uint32_t baud) {
    if (n >= modemcfg::kTemplates) {
        return false;
    }
    nvram_.tmpl[n] = active_;
    nvram_.tmpl[n].s[1] = 0;
    nvram_.tmpl[n].baud = baud;  // as on the Sportster, the template keeps the port speed
    return modemcfg::saveNvram(store_, nvram_);
}

bool Modem::setHostBaud(uint32_t baud) {
    if (baud != 0 && !modemcfg::isHostRate(baud)) {
        return false;
    }
    nvram_.hostBaud = baud;
    return modemcfg::saveNvram(store_, nvram_);
}

bool Modem::setPowerOn(unsigned y) {
    if (y > modemcfg::kTemplates + 2) {
        return false;
    }
    nvram_.y = static_cast<uint8_t>(y);
    return modemcfg::saveNvram(store_, nvram_);
}

bool Modem::setPhone(unsigned slot, const char *number) {
    if (slot >= modemcfg::kPhoneSlots || strlen(number) > modemcfg::kPhoneLen) {
        return false;
    }
    strcpy(nvram_.phone[slot], number);  // length checked above
    return modemcfg::saveNvram(store_, nvram_);
}

void Modem::reset(unsigned z) {
    if (z == 0) {
        active_ = modemcfg::powerOnProfile(nvram_);
    } else if (z <= modemcfg::kTemplates) {
        active_ = nvram_.tmpl[z - 1];
    } else {
        active_ = modemcfg::factoryProfile(z - modemcfg::kTemplates - 1);
    }
    active_.s[1] = 0;
}

void Modem::setLastDialed(const char *text) {
    copyTrimmed(lastDialed_, sizeof lastDialed_, text);
}

bool Modem::setModel(uint32_t speed) {
    if (!modemcfg::validModel(speed)) {
        return false;
    }
    uint32_t before = nvram_.model;
    nvram_.model = speed;
    if (modemcfg::saveNvram(store_, nvram_)) {
        return true;
    }
    nvram_.model = before;
    return false;
}

bool Modem::setAnswerConsole(bool enabled) {
    bool before = nvram_.answerConsole;
    nvram_.answerConsole = enabled;
    if (modemcfg::saveNvram(store_, nvram_)) {
        return true;
    }
    nvram_.answerConsole = before;
    return false;
}

bool Modem::setThrottle(bool enabled) {
    bool before = nvram_.throttle;
    nvram_.throttle = enabled;
    if (modemcfg::saveNvram(store_, nvram_)) {
        return true;
    }
    nvram_.throttle = before;
    return false;
}

const StarCommand *Modem::findStar(const char *text, const char **args) const {
    const StarCommand *best = nullptr;
    size_t bestLen = 0;
    for (size_t i = 0; i < starCount_; ++i) {
        const StarCommand &c = star_[i];
        size_t n = strlen(c.name);
        if (n < bestLen || !startsWith(text, c.name)) {
            continue;
        }
        char next = text[n];
        if (!c.prefix && isalnum(static_cast<unsigned char>(next))) {
            continue;  // "AT*VERX" is not AT*VER
        }
        best = &c;
        bestLen = n;
    }
    if (best != nullptr && args != nullptr) {
        *args = text + bestLen;
    }
    return best;
}

// --- Port: output -----------------------------------------------------------------------

Port::Port(Modem &modem, Sink &out, uint32_t baud) : modem_(modem), out_(out), baud_(baud) {}

void Port::write(const char *data, size_t len) {
    (redirected_ ? *redirected_ : out_).write(data, len);
}

void Port::print(const char *text) {
    write(text, strlen(text));
}

void Port::printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        write(buf, static_cast<size_t>(n) < sizeof buf ? static_cast<size_t>(n) : sizeof buf - 1);
    }
}

void Port::newline() {
    if (readable_) {
        write("\r\n", 2);
        return;
    }
    const Profile &p = modem_.active();
    const char crlf[2] = {static_cast<char>(p.s[3]), static_cast<char>(p.s[4])};
    write(crlf, 2);
}

void Port::line(const char *text) {
    print(text);
    newline();
}

void Port::beginInfo() {
    if (modem_.active().get(modemcfg::kV) != 0) {
        newline();
    }
}

void Port::result(Result r) {
    if (readable_) {
        Profile display = modem_.active();
        display.v[modemcfg::kQ] = 0;
        display.v[modemcfg::kV] = 1;
        display.s[3] = 13;
        display.s[4] = 10;
        char text[32];
        size_t n = formatResult(r, display, text, sizeof text);
        write(text, n);
        return;
    }
    if (modem_.active().get(modemcfg::kQ) == 2 && link_ && link_->answering() &&
        r == Result::kNoCarrier) {
        return;
    }
    char buf[32];
    size_t n = formatResult(r, modem_.active(), buf, sizeof buf);
    if (n > 0) {
        write(buf, n);
    }
}

void Port::echo(const char *s, size_t len) {
    if (readable_ || modem_.active().get(modemcfg::kE) != 0) {
        write(s, len);
    }
}

// --- Port: line input -----------------------------------------------------------------------

Reply cmdModel(Port &port, const char *args) {
    if (strcmp(args, "=?") == 0) {
        port.beginInfo();
        port.line("14400,28800,33600,56000");
        return Reply::kOk;
    }
    if (*args == '\0' || strcmp(args, "?") == 0) {
        port.beginInfo();
        port.printf("%u (%s)", static_cast<unsigned>(port.modem().model()),
                    modemcfg::modelName(port.modem().model()));
        port.newline();
        return Reply::kOk;
    }
    if (*args++ != '=') {
        return Reply::kError;
    }
    unsigned speed = 0;
    if (!isDigit(*args) || !number(args, 56000, &speed) || *args != '\0') {
        return Reply::kError;
    }
    return port.modem().setModel(speed) ? Reply::kOk : Reply::kError;
}

Reply cmdThrottle(Port &port, const char *args) {
    if (*args == '\0' || strcmp(args, "?") == 0) {
        port.beginInfo();
        port.line(port.modem().throttle() ? "ON" : "OFF");
        return Reply::kOk;
    }
    bool on = strlen(args) == 3 && startsWith(args, "=ON");
    bool off = strlen(args) == 4 && startsWith(args, "=OFF");
    if (!on && !off) {
        return Reply::kError;
    }
    return port.modem().setThrottle(on) ? Reply::kOk : Reply::kError;
}

void Port::feed(char c) {
    if (modem_.commandsStopped()) {
        return;
    }
    const Profile &p = modem_.active();
    const char cr = static_cast<char>(p.s[3]);
    const bool bsOn = p.s[5] < 128;
    // A client preference adds an input alias; the standard S5 setting remains
    // effective, and DEL retains the parser's existing compatibility behavior.
    const bool isBs = (bsOn && c == static_cast<char>(p.s[5])) || c == 0x7F ||
                      (clientBackspace_ && static_cast<uint8_t>(c) == clientBackspace_);

    switch (state_) {
        case State::kIdle:
        case State::kGotA:
            if (state_ == State::kGotA && c == '/') {
                echo(&c, 1);
                echo(&cr, 1);
                state_ = State::kIdle;
                if (!repeatAllowed_) {
                    result(Result::kError);
                    return;
                }
                char again[kLineMax + 1];
                memcpy(again, last_, sizeof again);
                execute(again);
                return;
            }
            if (state_ == State::kGotA &&
                ((aCase_ == 'A' && c == 'T') || (aCase_ == 'a' && c == 't'))) {
                echo(&c, 1);
                state_ = State::kLine;
                len_ = 0;
                overflow_ = false;
                privateLine_ = false;
                privateStart_ = 0;
                return;
            }
            if (c == 'A' || c == 'a') {
                aCase_ = c;
                state_ = State::kGotA;
            } else {
                state_ = State::kIdle;
            }
            if (c == cr) {
                echo(&c, 1);
            } else if (static_cast<unsigned char>(c) >= ' ' && c != 0x7F) {
                echo(&c, 1);
            }
            return;

        case State::kLine:
            if (c == cr) {
                echo(&c, 1);
                state_ = State::kIdle;
                line_[len_] = '\0';
                if (overflow_) {
                    memset(line_, 0, sizeof line_);
                    memset(last_, 0, sizeof last_);
                    repeatAllowed_ = false;
                    result(Result::kError);
                    return;
                }
                repeatAllowed_ = !privateLine_;
                if (repeatAllowed_) {
                    memcpy(last_, line_, sizeof last_);
                } else {
                    memset(last_, 0, sizeof last_);
                }
                execute(line_);
                volatile char *wipe = line_;
                for (size_t i = 0; i < sizeof line_; ++i) {
                    wipe[i] = 0;
                }
                return;
            }
            if (isBs) {
                if (len_ > 0) {  // "AT" itself can't be deleted
                    --len_;
                    echo("\b \b", 3);
                }
                return;
            }
            if (static_cast<unsigned char>(c) < ' ') {
                if (privateLine_) {
                    overflow_ = true;  // do not silently remove a control byte from a password
                }
                return;  // LF and other control characters are ignored
            }
            if (privateLine_ && !(len_ == privateStart_ && c == '=')) {
                echo("*", 1);
            } else {
                echo(&c, 1);
            }
            if (len_ < kLineMax) {
                line_[len_++] = c;
                line_[len_] = '\0';
                // Recognise a sensitive AT* command before its first argument is
                // echoed, including a command after an ordinary init string.
                const char *star = strchr(line_, '*');
                if (star != nullptr) {
                    const StarCommand *command = modem_.findStar(star + 1, nullptr);
                    if (!privateLine_ && command != nullptr && command->sensitive) {
                        privateLine_ = true;
                        privateStart_ = len_;
                    }
                }
            } else {
                overflow_ = true;
            }
            return;

        case State::kSecret:
            feedSecret(c, isBs);
            return;

        case State::kWait:
            feedWait(c);
            return;
    }
}

// --- Port: masked prompt and late results ---------------------------------------------------

namespace {
constexpr char kEsc = 0x1B;
constexpr char kCtrlC = 0x03;
}  // namespace

void Port::askSecret(const char *prompt, SecretHandler fn, void *ctx) {
    print(prompt);
    wipeSecret();
    secretFn_ = fn;
    ctx_ = ctx;
    state_ = State::kSecret;
    maskSecret_ = true;
}

void Port::askConfirmation(const char *prompt, SecretHandler fn, void *ctx) {
    askSecret(prompt, fn, ctx);
    maskSecret_ = false;
}

bool Port::cancelPrompt() {
    if (state_ != State::kSecret) {
        return false;
    }
    wipeSecret();
    secretFn_ = nullptr;
    state_ = State::kIdle;
    result(Result::kError);
    return true;
}

void Port::wait(CancelHandler cancel, void *ctx, Result cancelResult, bool answerCall) {
    cancelFn_ = cancel;
    cancelResult_ = cancelResult;
    answerWait_ = answerCall;
    ctx_ = ctx;
    state_ = State::kWait;
}

bool Port::finish(Reply r) {
    if (state_ != State::kWait) {
        return false;
    }
    state_ = State::kIdle;
    cancelFn_ = nullptr;
    if (r == Reply::kOk || r == Reply::kError) {
        result(r == Reply::kOk ? Result::kOk : Result::kError);
    }
    return true;
}

void Port::wipeSecret() {
    volatile char *s = secret_;  // volatile: the compiler must not skip the wipe
    for (size_t i = 0; i < sizeof secret_; ++i) {
        s[i] = '\0';
    }
    secretLen_ = 0;
    secretOverflow_ = false;
}

void Port::feedSecret(char c, bool isBs) {
    const char cr = static_cast<char>(modem_.active().s[3]);
    if (c == cr) {
        echo(&c, 1);
        state_ = State::kIdle;
        if (secretOverflow_) {
            wipeSecret();
            result(Result::kError);
            return;
        }
        char text[kSecretMax + 1];
        memcpy(text, secret_, sizeof text);
        wipeSecret();
        SecretHandler fn = secretFn_;
        secretFn_ = nullptr;
        Reply r = fn != nullptr ? fn(*this, text, ctx_) : Reply::kError;
        volatile char *t = text;
        for (size_t i = 0; i < sizeof text; ++i) {
            t[i] = '\0';
        }
        if (r == Reply::kOk || r == Reply::kError) {
            result(r == Reply::kOk ? Result::kOk : Result::kError);
        }
        return;
    }
    if (c == kEsc || c == kCtrlC) {
        wipeSecret();
        secretFn_ = nullptr;
        state_ = State::kIdle;
        result(Result::kError);
        return;
    }
    if (isBs) {
        if (secretLen_ > 0) {
            secret_[--secretLen_] = '\0';
            echo("\b \b", 3);
        }
        return;
    }
    if (static_cast<unsigned char>(c) < ' ') {
        return;  // LF and other control characters are ignored
    }
    if (maskSecret_) {
        echo("*", 1);
    } else {
        echo(&c, 1);
    }
    if (secretLen_ < kSecretMax) {
        secret_[secretLen_++] = c;
    } else {
        secretOverflow_ = true;
    }
}

void Port::feedWait(char c) {
    const char cr = static_cast<char>(modem_.active().s[3]);
    if (static_cast<unsigned char>(c) < ' ' && c != cr && c != kEsc && c != kCtrlC) {
        return;  // LF after the command's CR, XON/XOFF: not a key press
    }
    CancelHandler cancel = cancelFn_;
    void *ctx = ctx_;
    const bool quiet = answerWait_ && modem_.active().get(modemcfg::kQ) == 2;
    cancelFn_ = nullptr;
    state_ = State::kIdle;
    if (cancel != nullptr) {
        cancel(ctx);
    }
    if (!quiet) {
        result(cancelResult_);
    }
}

// --- Port: command execution ------------------------------------------------------------

void Port::execute(const char *line) {
    if (modem_.commandsStopped()) {
        result(Result::kError);
        return;
    }
    const char *p = line;
    bool stop = false;
    resultSent_ = false;
    while (!stop) {
        skipSpaces(p);
        if (*p == '\0') {
            break;
        }
        if (*p == '*') {  // project command: takes the rest of the line
            const char *args = nullptr;
            const StarCommand *cmd = modem_.findStar(p + 1, &args);
            if (cmd == nullptr) {
                result(Result::kError);
                return;
            }
            Reply r = cmd->fn(*this, args);
            if (r == Reply::kPending) {
                return;  // askSecret() or wait() sends the result later
            }
            if (r != Reply::kDone) {
                result(r == Reply::kOk ? Result::kOk : Result::kError);
            }
            return;
        }
        if (!runCommand(p, &stop)) {
            result(Result::kError);
            return;
        }
    }
    if (!resultSent_) {
        result(Result::kOk);
    }
}

bool Port::runCommand(const char *&p, bool *stop) {
    Profile &a = modem_.active();
    const char c = upper(*p++);
    unsigned n = 0;

    switch (c) {
        case 'A':
            if (link_ == nullptr || !link_->answer()) {
                return false;
            }
            resultSent_ = true;
            *stop = true;
            return true;
        case 'D': {
            bool pulse = a.get(modemcfg::kTone) == 0;
            if (upper(*p) == 'T' || upper(*p) == 'P') {
                pulse = upper(*p++) == 'P';
            }
            const char *digits = p;
            if (upper(*p) == 'L' && p[1] == '\0') {
                digits = modem_.lastDialed();
            }
            if (upper(*p) == 'S') {
                ++p;
                if (!number(p, modemcfg::kPhoneSlots - 1, &n) || *p != '\0') {
                    return false;
                }
                digits = modem_.nvram().phone[n];
            }
            if (link_ == nullptr || !link_->dial(digits, pulse)) {
                return false;
            }
            modem_.setLastDialed(digits);
            resultSent_ = true;
            *stop = true;
            return true;
        }
        case 'B':
        case 'E':
        case 'F':
        case 'L':
        case 'M':
        case 'Q':
        case 'V':
        case 'X': {
            int s = modemcfg::findSetting(0, c);
            if (s < 0 || !number(p, 255, &n) ||
                !modemcfg::settingAllows(static_cast<Setting>(s), n)) {
                return false;
            }
            a.v[s] = static_cast<uint8_t>(n);
            return true;
        }
        case 'P':
            a.v[modemcfg::kTone] = 0;
            return true;
        case 'T':
            a.v[modemcfg::kTone] = 1;
            return true;
        case 'H':  // H0 hangs up; H1 (off hook) has nothing to do
            if (!number(p, 1, &n)) {
                return false;
            }
            if (n == 0 && link_ != nullptr) {
                link_->hangUp();
            }
            return true;
        case 'O':  // back online; O1 (retrain) does the same
            if (!number(p, 1, &n) || !callUp()) {
                return false;
            }
            link_->resumeOnline();  // sends CONNECT
            resultSent_ = true;
            *stop = true;  // the rest of the line is ignored
            return true;
        case 'I':
            return number(p, 255, &n) && showInfo(*this, n);
        case 'Y':
            return number(p, modemcfg::kTemplates + 2, &n) && modem_.setPowerOn(n);
        case 'Z':
            if (!number(p, 5, &n)) {
                return false;
            }
            if (link_ != nullptr) {
                link_->hangUp();  // the Sportster hangs up on Z
            }
            modem_.reset(n);
            selected_ = 0;
            *stop = true;  // the Sportster ignores everything after Z
            return true;
        case 'S':
            return runSRegister(p);
        case '=':
        case '?':
            --p;
            return runSRegister(p);
        case '&':
            return runAmpersand(p, stop);
        case '+':
            return runPlus(p);
        default:
            return false;  // includes D and A until calls exist (v0.8, v0.9)
    }
}

bool Port::runSRegister(const char *&p) {
    Profile &a = modem_.active();
    unsigned r = selected_;
    if (isDigit(*p) || *p == ' ') {
        if (!number(p, modemcfg::kSRegCount - 1, &r)) {
            return false;
        }
    } else if (*p != '=' && *p != '?' && *p != '.') {
        r = 0;  // "ATS" alone selects S0
    }
    selected_ = r;
    skipSpaces(p);
    unsigned v = 0;
    if (*p == '.') {  // Sr.b=n sets one bit
        ++p;
        unsigned b = 0;
        if (!number(p, 7, &b)) {
            return false;
        }
        skipSpaces(p);
        if (*p != '=') {
            return false;
        }
        ++p;
        if (!number(p, 1, &v)) {
            return false;
        }
        uint8_t mask = static_cast<uint8_t>(1u << b);
        a.s[r] = static_cast<uint8_t>(v ? (a.s[r] | mask) : (a.s[r] & ~mask));
        return true;
    }
    if (*p == '=') {
        ++p;
        if (!number(p, 255, &v)) {
            return false;
        }
        a.s[r] = static_cast<uint8_t>(v);
        return true;
    }
    if (*p == '?') {
        ++p;
        beginInfo();
        printf("%03u", static_cast<unsigned>(a.s[r]));
        newline();
        return true;
    }
    return true;  // "ATSr" only selects the register
}

bool Port::runAmpersand(const char *&p, bool *stop) {
    Profile &a = modem_.active();
    const char c = upper(*p);
    if (c == '\0') {
        return false;
    }
    ++p;
    unsigned n = 0;

    switch (c) {
        case 'F':
            if (!number(p, 2, &n)) {
                return false;
            }
            a = modemcfg::factoryProfile(n);
            return true;
        case 'W':
            return number(p, modemcfg::kTemplates - 1, &n) && modem_.writeTemplate(n, baud_);
        case 'V':  // project addition: ATI4 and ATI5 together
            if (!number(p, 0, &n)) {
                return false;
            }
            showCurrent(*this);
            showStored(*this);
            return true;
        case 'Z': {
            skipSpaces(p);
            if (upper(*p) == 'L') {  // &ZL? shows the last number dialled
                ++p;
                skipSpaces(p);
                if (*p != '?') {
                    return false;
                }
                ++p;
                beginInfo();
                line(modem_.lastDialed());
                return true;
            }
            if (!number(p, modemcfg::kPhoneSlots - 1, &n)) {
                return false;
            }
            skipSpaces(p);
            if (*p == '?') {
                ++p;
                beginInfo();
                line(modem_.nvram().phone[n]);
                return true;
            }
            if (*p != '=') {
                return false;
            }
            // The rest of the line is the number (one character too many is kept so an
            // over-long number is refused rather than cut short).
            char digits[modemcfg::kPhoneLen + 2];
            copyTrimmed(digits, sizeof digits, p + 1);
            p += strlen(p);
            *stop = true;
            if ((digits[0] == 'L' || digits[0] == 'l') && digits[1] == '\0') {
                return modem_.setPhone(n, modem_.lastDialed());
            }
            return strlen(digits) <= modemcfg::kPhoneLen && modem_.setPhone(n, digits);
        }
        default: {
            int s = modemcfg::findSetting('&', c);
            if (s < 0 || !number(p, 255, &n) ||
                !modemcfg::settingAllows(static_cast<Setting>(s), n)) {
                return false;
            }
            a.v[s] = static_cast<uint8_t>(n);
            if (s == modemcfg::kAmpN) {
                a.lineExplicit = true;
            }
            return true;
        }
    }
}

bool Port::runPlus(const char *&p) {
    // Only +FCLASS: data mode (0) is the only class; fax classes give ERROR (brief 6.6, Q32).
    if (!startsWith(p, "FCLASS")) {
        return false;
    }
    p += 6;
    skipSpaces(p);
    if (*p == '?') {
        ++p;
        beginInfo();
        line("0");
        return true;
    }
    if (*p != '=') {
        return false;
    }
    ++p;
    skipSpaces(p);
    if (*p == '?') {
        ++p;
        beginInfo();
        line("0");
        return true;
    }
    unsigned n = 0;
    if (!isDigit(*p) || !number(p, 255, &n) || n != 0 || *p == '.') {
        return false;
    }
    return true;
}

}  // namespace atcmd
