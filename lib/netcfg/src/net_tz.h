// Time zones for the clock (project brief 6.8, Q59): Pacific/Auckland by default,
// configurable with AT*TZ.
//
// The ESP32 has no time-zone database, so zones are POSIX rules (as in the TZ variable):
// a short table of names (zoneCount/zoneName) plus any POSIX rule typed in. Only the
// "Mm.w.d[/time]" form of the change dates is supported; every zone in use today can be
// written with it. Our own code, so the PC tests and the board give the same answers.
//
// No Arduino dependencies: unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

namespace netcfg {
namespace tz {

struct Change {
    uint8_t month;  // 1-12
    uint8_t week;   // 1-5, 5 = the last one in the month
    uint8_t wday;   // 0 = Sunday
    int32_t time;   // seconds after local midnight (may be negative or over 24 h)
};

struct Rule {
    char stdName[8];
    int32_t stdOffset;  // seconds east of UTC (POSIX writes them west: NZST-12 = +12 h)
    bool hasDst;
    char dstName[8];
    int32_t dstOffset;
    Change start;  // local standard time when daylight saving starts
    Change end;    // local daylight time when it ends
};

// A POSIX rule such as "NZST-12NZDT,M9.5.0,M4.1.0/3" or "<+1245>-12:45<+1345>,...".
bool parse(const char *text, Rule *out);

// A zone name from the table (any case) or a POSIX rule. `canonical` (optional, `cap`
// bytes) gets the name as stored: the table's spelling, or the rule as typed.
bool resolve(const char *setting, Rule *out, char *canonical, size_t cap);

size_t zoneCount();
const char *zoneName(size_t i);
const char *zoneRule(size_t i);

// Offset from UTC (seconds) and the zone abbreviation in force at `utc`.
int32_t offsetAt(const Rule &r, int64_t utc, const char **abbrev = nullptr);

// "2026-10-24 21:15:07 NZDT" for `utc` (seconds since 1970).
void formatLocal(const Rule &r, int64_t utc, char *buf, size_t cap);
// "+13:00", "-05:00", "+12:45"
void formatOffset(int32_t seconds, char *buf, size_t cap);

// Calendar helpers (proleptic Gregorian, UTC).
int64_t daysFromCivil(int y, unsigned m, unsigned d);
void civilFromDays(int64_t days, int *y, unsigned *m, unsigned *d);

}  // namespace tz
}  // namespace netcfg
