// Time zones: see net_tz.h.
#include "net_tz.h"

#include <cctype>
#include <cstdio>
#include <cstring>

namespace netcfg {
namespace tz {

namespace {

struct Zone {
    const char *name;
    const char *rule;
};

// Rules as in the IANA database's POSIX footers (2026).
constexpr Zone kZones[] = {
    {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
    {"Pacific/Chatham", "<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45"},
    {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
    {"Australia/Brisbane", "AEST-10"},
    {"Australia/Adelaide", "ACST-9:30ACDT,M10.1.0,M4.1.0/3"},
    {"Australia/Perth", "AWST-8"},
    {"Asia/Tokyo", "JST-9"},
    {"Asia/Singapore", "<+08>-8"},
    {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
    {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},
    {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
    {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},
    {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
    {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
    {"Pacific/Honolulu", "HST10"},
    {"UTC", "UTC0"},
};
constexpr size_t kZoneCount = sizeof kZones / sizeof kZones[0];

bool sameText(const char *a, const char *b) {
    for (; *a != '\0' && *b != '\0'; ++a, ++b) {
        if (tolower(static_cast<unsigned char>(*a)) != tolower(static_cast<unsigned char>(*b))) {
            return false;
        }
    }
    return *a == '\0' && *b == '\0';
}

// Zone abbreviation: 3+ letters, or <...> with letters, digits, '+' and '-'.
bool parseName(const char *&p, char *out, size_t cap) {
    size_t n = 0;
    if (*p == '<') {
        ++p;
        while (*p != '>') {
            if (*p == '\0' ||
                !(isalnum(static_cast<unsigned char>(*p)) || *p == '+' || *p == '-')) {
                return false;
            }
            if (n + 1 >= cap) {
                return false;
            }
            out[n++] = *p++;
        }
        ++p;
    } else {
        while (isalpha(static_cast<unsigned char>(*p))) {
            if (n + 1 >= cap) {
                return false;
            }
            out[n++] = *p++;
        }
    }
    out[n] = '\0';
    return n >= 3;
}

// [+-]h[:mm[:ss]] with h up to `maxHours`. Returns seconds.
bool parseTime(const char *&p, int maxHours, int32_t *out) {
    int sign = 1;
    if (*p == '+' || *p == '-') {
        sign = *p == '-' ? -1 : 1;
        ++p;
    }
    int32_t parts[3] = {0, 0, 0};
    for (int i = 0; i < 3; ++i) {
        if (i > 0) {
            if (*p != ':') {
                break;
            }
            ++p;
        }
        if (!isdigit(static_cast<unsigned char>(*p))) {
            return false;
        }
        int32_t v = 0;
        int digits = 0;
        while (isdigit(static_cast<unsigned char>(*p))) {
            v = v * 10 + (*p++ - '0');
            if (++digits > 3) {
                return false;
            }
        }
        if ((i == 0 && v > maxHours) || (i > 0 && v > 59)) {
            return false;
        }
        parts[i] = v;
    }
    *out = sign * (parts[0] * 3600 + parts[1] * 60 + parts[2]);
    return true;
}

bool parseNumber(const char *&p, int lo, int hi, uint8_t *out) {
    if (!isdigit(static_cast<unsigned char>(*p))) {
        return false;
    }
    int v = 0;
    while (isdigit(static_cast<unsigned char>(*p))) {
        v = v * 10 + (*p++ - '0');
        if (v > hi) {
            return false;
        }
    }
    if (v < lo) {
        return false;
    }
    *out = static_cast<uint8_t>(v);
    return true;
}

// Mm.w.d[/time]
bool parseChange(const char *&p, Change *out) {
    if (*p != 'M') {
        return false;
    }
    ++p;
    Change c = {};
    if (!parseNumber(p, 1, 12, &c.month) || *p++ != '.' || !parseNumber(p, 1, 5, &c.week) ||
        *p++ != '.' || !parseNumber(p, 0, 6, &c.wday)) {
        return false;
    }
    c.time = 2 * 3600;
    if (*p == '/') {
        ++p;
        if (!parseTime(p, 167, &c.time)) {
            return false;
        }
    }
    *out = c;
    return true;
}

int64_t floorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

unsigned daysInMonth(int y, unsigned m) {
    int64_t next = m == 12 ? daysFromCivil(y + 1, 1, 1) : daysFromCivil(y, m + 1, 1);
    return static_cast<unsigned>(next - daysFromCivil(y, m, 1));
}

// Seconds since 1970 (as if local time were UTC) when the change happens in year `y`.
int64_t changeLocal(int y, const Change &c) {
    const int64_t first = daysFromCivil(y, c.month, 1);
    const int wdayFirst = static_cast<int>(((first % 7) + 11) % 7);  // 1970-01-01: Thursday
    unsigned day = 1 + static_cast<unsigned>((c.wday - wdayFirst + 7) % 7) + (c.week - 1u) * 7u;
    const unsigned dim = daysInMonth(y, c.month);
    while (day > dim) {
        day -= 7;
    }
    return daysFromCivil(y, c.month, day) * 86400 + c.time;
}

bool inDst(const Rule &r, int64_t utc) {
    if (!r.hasDst) {
        return false;
    }
    int y;
    unsigned m, d;
    civilFromDays(floorDiv(utc + r.stdOffset, 86400), &y, &m, &d);
    const int64_t start = changeLocal(y, r.start) - r.stdOffset;
    const int64_t end = changeLocal(y, r.end) - r.dstOffset;
    if (start < end) {
        return utc >= start && utc < end;  // northern hemisphere
    }
    return utc >= start || utc < end;  // southern: summer spans the new year
}

}  // namespace

bool parse(const char *text, Rule *out) {
    const char *p = text;
    Rule r = {};
    int32_t west = 0;
    if (!parseName(p, r.stdName, sizeof r.stdName) || !parseTime(p, 24, &west)) {
        return false;
    }
    r.stdOffset = -west;
    if (*p == '\0') {
        *out = r;
        return true;
    }
    if (!parseName(p, r.dstName, sizeof r.dstName)) {
        return false;
    }
    r.hasDst = true;
    r.dstOffset = r.stdOffset + 3600;
    if (*p != ',') {
        if (!parseTime(p, 24, &west)) {
            return false;
        }
        r.dstOffset = -west;
    }
    if (*p++ != ',' || !parseChange(p, &r.start) || *p++ != ',' || !parseChange(p, &r.end) ||
        *p != '\0') {
        return false;
    }
    *out = r;
    return true;
}

bool resolve(const char *setting, Rule *out, char *canonical, size_t cap) {
    for (const Zone &z : kZones) {
        if (sameText(setting, z.name)) {
            if (canonical != nullptr) {
                snprintf(canonical, cap, "%s", z.name);
            }
            return parse(z.rule, out);
        }
    }
    if (!parse(setting, out)) {
        return false;
    }
    if (canonical != nullptr) {
        snprintf(canonical, cap, "%s", setting);
    }
    return true;
}

size_t zoneCount() {
    return kZoneCount;
}

const char *zoneName(size_t i) {
    return i < kZoneCount ? kZones[i].name : "";
}

const char *zoneRule(size_t i) {
    return i < kZoneCount ? kZones[i].rule : "";
}

int32_t offsetAt(const Rule &r, int64_t utc, const char **abbrev) {
    const bool dst = inDst(r, utc);
    if (abbrev != nullptr) {
        *abbrev = dst ? r.dstName : r.stdName;
    }
    return dst ? r.dstOffset : r.stdOffset;
}

void formatLocal(const Rule &r, int64_t utc, char *buf, size_t cap) {
    const char *abbrev = "";
    const int64_t local = utc + offsetAt(r, utc, &abbrev);
    const int64_t days = floorDiv(local, 86400);
    const int64_t secs = local - days * 86400;
    int y;
    unsigned m, d;
    civilFromDays(days, &y, &m, &d);
    snprintf(buf, cap, "%04d-%02u-%02u %02u:%02u:%02u %s", y, m, d,
             static_cast<unsigned>(secs / 3600), static_cast<unsigned>(secs / 60 % 60),
             static_cast<unsigned>(secs % 60), abbrev);
}

void formatOffset(int32_t seconds, char *buf, size_t cap) {
    const char sign = seconds < 0 ? '-' : '+';
    const int32_t a = seconds < 0 ? -seconds : seconds;
    snprintf(buf, cap, "%c%02d:%02d", sign, static_cast<int>(a / 3600),
             static_cast<int>(a / 60 % 60));
}

// Howard Hinnant's algorithms (public domain).
int64_t daysFromCivil(int y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const int64_t era = floorDiv(y, 400);
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<int64_t>(doe) - 719468;
}

void civilFromDays(int64_t z, int *y, unsigned *m, unsigned *d) {
    z += 719468;
    const int64_t era = floorDiv(z, 146097);
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = static_cast<int>(static_cast<int64_t>(yoe) + era * 400 + (*m <= 2 ? 1 : 0));
}

}  // namespace tz
}  // namespace netcfg
