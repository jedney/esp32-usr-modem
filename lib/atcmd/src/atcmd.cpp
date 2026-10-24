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
    memcpy(dst, src, n);
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
    return r;
}

bool Modem::writeTemplate(unsigned n) {
    if (n >= modemcfg::kTemplates) {
        return false;
    }
    nvram_.tmpl[n] = active_;
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
    out_.write(data, len);
}

void Port::print(const char *text) {
    out_.write(text, strlen(text));
}

void Port::printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        out_.write(buf,
                   static_cast<size_t>(n) < sizeof buf ? static_cast<size_t>(n) : sizeof buf - 1);
    }
}

void Port::newline() {
    const Profile &p = modem_.active();
    const char crlf[2] = {static_cast<char>(p.s[3]), static_cast<char>(p.s[4])};
    out_.write(crlf, 2);
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
    char buf[32];
    size_t n = formatResult(r, modem_.active(), buf, sizeof buf);
    if (n > 0) {
        out_.write(buf, n);
    }
}

void Port::echo(const char *s, size_t len) {
    if (modem_.active().get(modemcfg::kE) != 0) {
        out_.write(s, len);
    }
}

// --- Port: line input -----------------------------------------------------------------------

void Port::feed(char c) {
    const Profile &p = modem_.active();
    const char cr = static_cast<char>(p.s[3]);
    const bool bsOn = p.s[5] < 128;
    const bool isBs = (bsOn && c == static_cast<char>(p.s[5])) || c == 0x7F;

    switch (state_) {
        case State::kIdle:
        case State::kGotA:
            if (state_ == State::kGotA && c == '/') {
                echo(&c, 1);
                echo(&cr, 1);
                state_ = State::kIdle;
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
                    result(Result::kError);
                    return;
                }
                memcpy(last_, line_, sizeof last_);
                execute(line_);
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
                return;  // LF and other control characters are ignored
            }
            echo(&c, 1);
            if (len_ < kLineMax) {
                line_[len_++] = c;
            } else {
                overflow_ = true;
            }
            return;
    }
}

// --- Port: command execution ------------------------------------------------------------

void Port::execute(const char *line) {
    const char *p = line;
    bool stop = false;
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
    result(Result::kOk);
}

bool Port::runCommand(const char *&p, bool *stop) {
    Profile &a = modem_.active();
    const char c = upper(*p++);
    unsigned n = 0;

    switch (c) {
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
        case 'H':  // on/off hook: nothing to do until calls exist (v0.8)
            return number(p, 1, &n);
        case 'I':
            return number(p, 255, &n) && showInfo(*this, n);
        case 'Y':
            return number(p, modemcfg::kTemplates + 2, &n) && modem_.setPowerOn(n);
        case 'Z':
            if (!number(p, 5, &n)) {
                return false;
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
            return false;  // includes D, A and O until calls exist (v0.5, v0.8, v0.9)
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
            return number(p, modemcfg::kTemplates - 1, &n) && modem_.writeTemplate(n);
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
