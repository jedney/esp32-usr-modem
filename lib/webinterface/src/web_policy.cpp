#include "web_policy.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include "devicecfg.h"

namespace webinterface {
bool terminalSize(const std::string &message, unsigned &columns, unsigned &rows) {
    const std::string prefix = "{\"t\":\"size\",\"cols\":";
    if (message.compare(0, prefix.size(), prefix) != 0 || message.size() > 48)
        return false;
    size_t offset = prefix.size();
    auto number = [&](unsigned &value, unsigned maximum) {
        value = 0;
        size_t start = offset;
        while (offset < message.size() && message[offset] >= '0' && message[offset] <= '9') {
            value = value * 10 + message[offset++] - '0';
            if (value > maximum)
                return false;
        }
        return offset > start && (offset - start == 1 || message[start] != '0');
    };
    unsigned c, r;
    if (!number(c, 65535) || c < 80 || message.compare(offset, 8, ",\"rows\":") != 0)
        return false;
    offset += 8;
    if (!number(r, 65535) || r < 25 || message.substr(offset) != "}")
        return false;
    columns = c;
    rows = r;
    return true;
}
namespace {
std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char c) { return static_cast<char>(tolower(c)); });
    return value;
}
bool version(const std::string &s, unsigned &major, unsigned &minor) {
    size_t dot = s.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 == s.size() || s.size() > 11) {
        return false;
    }
    major = minor = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        if (i == dot) {
            continue;
        }
        if (s[i] < '0' || s[i] > '9') {
            return false;
        }
        unsigned &part = i < dot ? major : minor;
        part = part * 10 + unsigned(s[i] - '0');
        if (part > 65535) {
            return false;
        }
    }
    return true;
}
}  // namespace
bool equalSecret(const std::string &a, const std::string &b) {
    if (a.size() != b.size()) {
        return false;
    }
    unsigned diff = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        diff |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return diff == 0;
}
bool HeaderGate::accept(const uint8_t *data, size_t len) {
    if (bad_) {
        return false;
    }
    for (size_t i = 0; i < len; ++i) {
        if (complete_) {
            if (websocket_) {
                return true;
            }
            if (!remaining_) {
                bad_ = true;
                return false;
            }
            --remaining_;
            continue;
        }
        head_ += static_cast<char>(data[i]);
        if (head_.size() > kMaxHeader) {
            bad_ = true;
            return false;
        }
        if (head_.size() < 4 || head_.compare(head_.size() - 4, 4, "\r\n\r\n")) {
            continue;
        }
        size_t lineEnd = head_.find("\r\n");
        size_t pathStart = head_.find(' '), pathEnd = head_.find(' ', pathStart + 1);
        if (pathStart == std::string::npos || pathEnd >= lineEnd) {
            bad_ = true;
            return false;
        }
        size_t pos = lineEnd + 2;
        std::map<std::string, unsigned> counts;
        while (pos + 2 < head_.size()) {
            size_t end = head_.find("\r\n", pos), colon = head_.find(':', pos);
            if (colon == std::string::npos || colon > end || head_[pos] == ' ' ||
                head_[pos] == '\t') {
                bad_ = true;
                return false;
            }
            std::string key = lower(head_.substr(pos, colon - pos));
            if (std::any_of(key.begin(), key.end(), [](char c) {
                    return !isalnum(static_cast<unsigned char>(c)) && c != '-';
                })) {
                bad_ = true;
                return false;
            }
            if (++counts[key] > 1 &&
                (key == "host" || key == "origin" || key == "cookie" || key == "content-length" ||
                 key == "upgrade" || key.compare(0, 2, "x-") == 0)) {
                bad_ = true;
                return false;
            }
            std::string value = head_.substr(colon + 1, end - colon - 1);
            while (!value.empty() && value[0] == ' ') {
                value.erase(0, 1);
            }
            if (key == "transfer-encoding") {
                bad_ = true;
                return false;
            }
            if (key == "content-length") {
                if (value.empty()) {
                    bad_ = true;
                    return false;
                }
                for (char c : value) {
                    if (c < '0' || c > '9' || remaining_ > 0x7e0000u / 10) {
                        bad_ = true;
                        return false;
                    }
                    remaining_ = remaining_ * 10 + unsigned(c - '0');
                }
            }
            if (key == "upgrade") {
                websocket_ = lower(value) == "websocket";
            }
            pos = end + 2;
        }
        complete_ = counts["host"] == 1 && remaining_ <= 0x7e0000u;
        if (!complete_) {
            bad_ = true;
            return false;
        }
        devicecfg::wipe(&head_[0], head_.size());
        head_.clear();
    }
    return true;
}
bool allowedHost(const std::string &value, const std::string &name, const std::string &ip) {
    if (value.empty() || value.size() > 80) {
        return false;
    }
    std::string host = lower(value);
    if (host.size() > 3 && host.compare(host.size() - 3, 3, ":80") == 0) {
        host.resize(host.size() - 3);
    }
    return (!name.empty() && host == lower(name) + ".local") || (!ip.empty() && host == ip);
}
bool allowedOrigin(const std::string &value, const std::string &name, const std::string &ip) {
    return value.compare(0, 7, "http://") == 0 && allowedHost(value.substr(7), name, ip);
}
namespace {
// Length of the well-formed UTF-8 sequence at p (2-4), or 0. Overlong forms, surrogates,
// code points above U+10FFFF and the C1 controls (U+0080-U+009F) are not accepted.
size_t utf8Length(const unsigned char *p) {
    const auto cont = [](unsigned char c) { return (c & 0xC0) == 0x80; };
    if (p[0] >= 0xC2 && p[0] <= 0xDF && cont(p[1])) {
        return p[0] == 0xC2 && p[1] < 0xA0 ? 0 : 2;
    }
    if (p[0] >= 0xE0 && p[0] <= 0xEF && cont(p[1]) && cont(p[2])) {
        const unsigned lead = p[0], second = p[1];
        if ((lead == 0xE0 && second < 0xA0) || (lead == 0xED && second >= 0xA0)) {
            return 0;
        }
        return 3;
    }
    if (p[0] >= 0xF0 && p[0] <= 0xF4 && cont(p[1]) && cont(p[2]) && cont(p[3])) {
        const unsigned lead = p[0], second = p[1];
        if ((lead == 0xF0 && second < 0x90) || (lead == 0xF4 && second >= 0x90)) {
            return 0;
        }
        return 4;
    }
    return 0;
}
}  // namespace
std::string jsonString(const char *value) {
    std::string result = "\"";
    for (const auto *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
        if (*p == '"' || *p == '\\') {
            result += '\\';
            result += static_cast<char>(*p);
        } else if (*p >= 0xC2) {
            // Valid UTF-8 passes through, so a network named "Café" is readable.
            // utf8Length() reads at most three bytes ahead; a NUL ends the string safely
            // because it is never a continuation byte.
            const size_t n = utf8Length(p);
            if (p[0] == 0xC2 && (p[1] & 0xE0) == 0x80) {
                // A C1 control character (U+0080-U+009F): valid UTF-8, but escaped.
                char escaped[7];
                snprintf(escaped, sizeof escaped, "\\u%04x", unsigned(p[1]));
                result += escaped;
                ++p;
            } else if (n) {
                result.append(reinterpret_cast<const char *>(p), n);
                p += n - 1;
            } else {
                char escaped[7];
                snprintf(escaped, sizeof escaped, "\\u%04x", unsigned(*p));
                result += escaped;
            }
        } else if (*p < 32 || *p >= 127) {
            // Escaping bytes as U+00xx keeps malformed SSID bytes out of JSON syntax.
            char escaped[7];
            snprintf(escaped, sizeof escaped, "\\u%04x", unsigned(*p));
            result += escaped;
        } else {
            result += static_cast<char>(*p);
        }
    }
    return result + '"';
}
int compareVersions(const std::string &a, const std::string &b) {
    unsigned am, an, bm, bn;
    if (!version(a, am, an) || !version(b, bm, bn)) {
        return -2;
    }
    if (am != bm) {
        return am < bm ? -1 : 1;
    }
    if (an != bn) {
        return an < bn ? -1 : 1;
    }
    return 0;
}
bool makeToken(Crypto &crypto, char out[33]) {
    uint8_t bytes[16] = {};
    if (!crypto.random(bytes, sizeof bytes)) {
        out[0] = 0;
        return false;
    }
    const char *digits = "0123456789abcdef";
    for (size_t i = 0; i < sizeof bytes; ++i) {
        out[2 * i] = digits[bytes[i] >> 4];
        out[2 * i + 1] = digits[bytes[i] & 15];
    }
    out[32] = 0;
    devicecfg::wipe(bytes, sizeof bytes);
    return true;
}
void Tickets::reset() {
    for (auto &t : ticket_) {
        t = {};
    }
}
std::string Tickets::issue(uint32_t session, uint32_t now) {
    char value[33];
    if (!session || !makeToken(crypto_, value)) {
        return "";
    }
    ticket_[next_] = {value, now, session};
    next_ = (next_ + 1) % 4;
    return value;
}
uint32_t Tickets::consume(const std::string &token, uint32_t now) {
    if (token.size() != 32) {
        return 0;
    }
    for (auto &t : ticket_) {
        if (!t.value.empty() && equalSecret(t.value, token) && now - t.at < 30000) {
            uint32_t session = t.session;
            t = {};
            return session;
        }
    }
    return 0;
}
void Tickets::revoke(uint32_t session) {
    for (auto &t : ticket_) {
        if (t.session == session) {
            t = {};
        }
    }
}
bool Settings::begin() {
    uint8_t data[10] = {};
    size_t n = store_.load(data, sizeof data);
    enabled_ = n == 0;
    if (!n) {
        return true;
    }
    if (n != sizeof data || memcmp(data, "USRW\1", 5) || data[5] > 1) {
        return false;
    }
    uint32_t crc = modemcfg::crc32(data, 6);
    for (unsigned i = 0; i < 4; ++i) {
        if (data[6 + i] != uint8_t(crc >> (8 * i))) {
            return false;
        }
    }
    enabled_ = data[5] == 1;
    return true;
}
bool Settings::setEnabled(bool on) {
    uint8_t data[10] = {'U', 'S', 'R', 'W', 1, uint8_t(on), 0, 0, 0, 0};
    uint32_t crc = modemcfg::crc32(data, 6);
    for (unsigned i = 0; i < 4; ++i) {
        data[6 + i] = uint8_t(crc >> (8 * i));
    }
    if (!store_.save(data, sizeof data)) {
        return false;
    }
    enabled_ = on;
    return true;
}
}  // namespace webinterface
