#include "web_policy.h"
#include <cctype>
#include <cstdio>
#include <cstring>
#include <map>
#include "devicecfg.h"

namespace webinterface {
namespace {
std::string lower(std::string value) {
    for (char &c : value) {
        c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    }
    return value;
}
bool hex(const std::string &text, size_t n) {
    if (text.size() != n) {
        return false;
    }
    for (char c : text) {
        if (!isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}
bool fields(const std::string &header, std::map<std::string, std::string> &out) {
    if (header.size() > 900 || header.compare(0, 7, "Digest ") != 0) {
        return false;
    }
    size_t i = 7;
    while (i < header.size()) {
        while (i < header.size() && header[i] == ' ') {
            ++i;
        }
        size_t start = i;
        while (i < header.size() && isalnum(static_cast<unsigned char>(header[i]))) {
            ++i;
        }
        std::string key = header.substr(start, i - start);
        if (key.empty() || i == header.size() || header[i++] != '=') {
            return false;
        }
        std::string value;
        if (i < header.size() && header[i] == '"') {
            ++i;
            bool closed = false;
            while (i < header.size()) {
                char c = header[i++];
                if (c == '"') {
                    closed = true;
                    break;
                }
                if (c == '\\') {
                    if (i == header.size()) {
                        return false;
                    }
                    c = header[i++];
                }
                if (static_cast<unsigned char>(c) < 32 || c == 127) {
                    return false;
                }
                value += c;
            }
            if (!closed) {
                return false;
            }
        } else {
            start = i;
            while (i < header.size() && header[i] != ',' && header[i] != ' ') {
                ++i;
            }
            value = header.substr(start, i - start);
        }
        if (!out.emplace(key, value).second || out.size() > 12) {
            return false;
        }
        while (i < header.size() && header[i] == ' ') {
            ++i;
        }
        if (i < header.size() && (header[i++] != ',' || i == header.size())) {
            return false;
        }
    }
    return true;
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
        if (head_.size() > 4096) {
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
        target_ = head_.substr(pathStart + 1, pathEnd - pathStart - 1);
        filtered_ = head_.substr(0, lineEnd + 2);
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
            for (char c : key) {
                if (!isalnum(static_cast<unsigned char>(c)) && c != '-') {
                    bad_ = true;
                    return false;
                }
            }
            if (++counts[key] > 1 &&
                (key == "host" || key == "origin" || key == "authorization" ||
                 key == "content-length" || key == "upgrade" || key.compare(0, 2, "x-") == 0)) {
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
            if (key == "host") {
                host_ = value;
            }
            if (key == "origin") {
                origin_ = value;
            }
            if (key == "upgrade") {
                websocket_ = lower(value) == "websocket";
            }
            if (key == "host" || key == "origin" || key == "authorization" ||
                key == "content-length" || key == "content-type" || key == "connection" ||
                key == "upgrade" || key == "sec-websocket-key" || key == "sec-websocket-version" ||
                key == "x-csrf-token" || key == "x-image-sha256" || key == "x-confirm-downgrade" ||
                key == "x-confirm-filesystem") {
                filtered_ += key + ": " + value + "\r\n";
            }
            pos = end + 2;
        }
        filtered_ += "\r\n";
        complete_ = counts["host"] == 1 && remaining_ <= 0x7e0000u && filtered_.size() <= 1024;
        if (!complete_) {
            bad_ = true;
            return false;
        }
        devicecfg::wipe(&head_[0], head_.size());
        headerLength_ = head_.size();
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
std::string jsonString(const char *value) {
    std::string result = "\"";
    for (const auto *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
        if (*p == '"' || *p == '\\') {
            result += '\\';
            result += static_cast<char>(*p);
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
    return am == bm && an == bn ? 0 : (am < bm || (am == bm && an < bn) ? -1 : 1);
}
bool Auth::token(char out[33]) {
    uint8_t bytes[16] = {};
    if (!crypto_.random(bytes, sizeof bytes)) {
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
void Auth::reset() {
    for (auto &n : nonce_) {
        n = {};
    }
}
std::string Auth::challenge(uint32_t now) {
    char value[33];
    if (!token(value)) {
        return "";
    }
    nonce_[next_] = {value, now, 0};
    next_ = (next_ + 1) % 8;
    return std::string("Digest realm=\"usr-modem\", qop=\"auth\", algorithm=MD5, nonce=\"") +
           value + '"';
}
bool Auth::verify(const std::string &header, const std::string &method, const std::string &uri,
                  const char *ha1, uint32_t now) {
    std::map<std::string, std::string> f;
    if (!ha1 || !hex(ha1, 32) || !fields(header, f) || f["username"] != "admin" ||
        f["realm"] != "usr-modem" || f["uri"] != uri || f["qop"] != "auth" ||
        (f.count("algorithm") && f["algorithm"] != "MD5") || !hex(f["nc"], 8) ||
        !hex(f["response"], 32) || f["cnonce"].empty() || f["cnonce"].size() > 128) {
        return false;
    }
    uint32_t count = 0;
    for (char c : f["nc"]) {
        count = (count << 4) | unsigned(c <= '9' ? c - '0' : tolower(c) - 'a' + 10);
    }
    for (auto &n : nonce_) {
        if (n.value.empty() || !equalSecret(f["nonce"], n.value) || now - n.at >= 60000 ||
            count <= n.count) {
            continue;
        }
        char ha2[33] = {}, expected[33] = {};
        bool ok = crypto_.md5(method + ':' + uri, ha2) &&
                  crypto_.md5(std::string(ha1) + ':' + n.value + ':' + f["nc"] + ':' + f["cnonce"] +
                                  ":auth:" + ha2,
                              expected) &&
                  equalSecret(lower(f["response"]), expected);
        devicecfg::wipe(ha2, sizeof ha2);
        devicecfg::wipe(expected, sizeof expected);
        if (ok) {
            n.count = count;
        }
        return ok;
    }
    return false;
}
void Tickets::reset() {
    for (auto &t : ticket_) {
        t = {};
    }
}
std::string Tickets::issue(uint32_t now) {
    char value[33];
    if (!auth_.token(value)) {
        return "";
    }
    ticket_[next_] = {value, now};
    next_ = (next_ + 1) % 4;
    return value;
}
bool Tickets::consume(const std::string &token, uint32_t now) {
    if (token.size() != 32) {
        return false;
    }
    for (auto &t : ticket_) {
        if (!t.value.empty() && equalSecret(t.value, token) && now - t.at < 30000) {
            t = {};
            return true;
        }
    }
    return false;
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
