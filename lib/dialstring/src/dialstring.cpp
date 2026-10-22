#include "dialstring.h"

#include <cstdio>
#include <cstring>

namespace dialstring {

namespace {

bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

bool isSpace(char c) {
    return c == ' ' || c == '\t';
}

// Characters dropped from a numeric dial string: formatting and Hayes dial modifiers.
bool isIgnorable(char c) {
    switch (c) {
        case ' ':
        case '\t':
        case '-':
        case '(':
        case ')':
        case ',':  // pause
        case 'W':  // wait for dial tone
        case 'w':
        case '@':  // wait for quiet answer
        case '!':  // hook flash
        case ';':  // return to command mode
        case 'T':  // switch to tone mid-string
        case 't':
        case 'P':  // switch to pulse mid-string
        case 'p':
            return true;
        default:
            return false;
    }
}

bool isHostChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || isDigit(c) || c == '.' || c == '-' ||
           c == '_';
}

// Parses up to 5 decimal digits as a port. Returns false if empty, too long, 0 or > 65535.
bool parsePort(const char *s, size_t len, uint16_t *out) {
    if (len == 0 || len > 5) {
        return false;
    }
    uint32_t value = 0;
    for (size_t i = 0; i < len; ++i) {
        if (!isDigit(s[i])) {
            return false;
        }
        value = value * 10 + static_cast<uint32_t>(s[i] - '0');
    }
    if (value == 0 || value > 65535) {
        return false;
    }
    *out = static_cast<uint16_t>(value);
    return true;
}

Result invalid() {
    return Result{};
}

// "host" or "host:port", already trimmed and unquoted.
Result classifyHost(const char *s, size_t len) {
    Result r;
    const char *colon = static_cast<const char *>(memchr(s, ':', len));
    size_t hostLen = colon ? static_cast<size_t>(colon - s) : len;
    if (hostLen == 0 || hostLen > kMaxHostLen) {
        return invalid();
    }
    for (size_t i = 0; i < hostLen; ++i) {
        if (!isHostChar(s[i])) {
            return invalid();
        }
    }
    if (s[0] == '.' || s[hostLen - 1] == '.' || s[0] == '-') {
        return invalid();
    }
    if (colon) {
        if (!parsePort(colon + 1, len - hostLen - 1, &r.port)) {
            return invalid();
        }
    }
    memcpy(r.host, s, hostLen);
    r.host[hostLen] = '\0';
    r.kind = Kind::Host;
    return r;
}

Special specialFor(const char *digits) {
    if (strcmp(digits, "123") == 0) {
        return Special::ConfigMenu;
    }
    if (strcmp(digits, "666") == 0 || strcmp(digits, "5517546") == 0) {
        return Special::Ppp;
    }
    if (strcmp(digits, "667") == 0 || strcmp(digits, "5517545") == 0) {
        return Special::Slip;
    }
    return Special::None;
}

Result classifyNumeric(const char *s, size_t len) {
    Result r;
    char digits[kMaxDigits + 1];
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) {
        char c = s[i];
        if (isDigit(c)) {
            if (n == kMaxDigits) {
                return invalid();  // more than 17 digits
            }
            digits[n++] = c;
        } else if (!isIgnorable(c)) {
            return invalid();
        }
    }
    digits[n] = '\0';
    if (n == 0) {
        return invalid();
    }

    if (n >= 12) {
        for (int octet = 0; octet < 4; ++octet) {
            int value = (digits[octet * 3] - '0') * 100 + (digits[octet * 3 + 1] - '0') * 10 +
                        (digits[octet * 3 + 2] - '0');
            if (value > 255) {
                return invalid();
            }
            r.ip[octet] = static_cast<uint8_t>(value);
        }
        if (n > 12 && !parsePort(digits + 12, n - 12, &r.port)) {
            return invalid();
        }
        snprintf(r.host, sizeof(r.host), "%u.%u.%u.%u", r.ip[0], r.ip[1], r.ip[2], r.ip[3]);
        memcpy(r.number, digits, n + 1);
        r.kind = Kind::IpAddress;
        return r;
    }

    memcpy(r.number, digits, n + 1);
    r.special = specialFor(digits);
    r.kind = (r.special != Special::None) ? Kind::Special : Kind::Phonebook;
    return r;
}

}  // namespace

Result classify(const char *input) {
    if (input == nullptr) {
        return invalid();
    }
    // Trim leading and trailing whitespace.
    const char *start = input;
    while (isSpace(*start)) {
        ++start;
    }
    size_t len = strlen(start);
    while (len > 0 && isSpace(start[len - 1])) {
        --len;
    }
    if (len == 0) {
        return invalid();
    }

    // Quoted: always a host. The closing quote is optional, as in many Hayes parsers.
    if (start[0] == '"') {
        const char *body = start + 1;
        size_t bodyLen = len - 1;
        if (bodyLen > 0 && body[bodyLen - 1] == '"') {
            --bodyLen;
        }
        return classifyHost(body, bodyLen);
    }

    // Contains '.' or ':' (neither is a dial modifier): a host, left unstripped.
    if (memchr(start, '.', len) != nullptr || memchr(start, ':', len) != nullptr) {
        return classifyHost(start, len);
    }

    return classifyNumeric(start, len);
}

const char *kindName(Kind kind) {
    switch (kind) {
        case Kind::IpAddress:
            return "ip";
        case Kind::Host:
            return "host";
        case Kind::Phonebook:
            return "phonebook";
        case Kind::Special:
            return "special";
        case Kind::Invalid:
        default:
            return "invalid";
    }
}

const char *specialName(Special special) {
    switch (special) {
        case Special::ConfigMenu:
            return "config";
        case Special::Ppp:
            return "ppp";
        case Special::Slip:
            return "slip";
        case Special::None:
        default:
            return "none";
    }
}

}  // namespace dialstring
