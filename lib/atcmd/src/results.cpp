// Result codes: see results.h. Numbers and texts from the USR Sportster technical
// reference (credits/LICENSES.md).
#include "results.h"

#include <cstdio>

namespace atcmd {

namespace {

struct Speed {
    uint32_t bps;
    Result code;
};

// Ascending. &N1-&N16 select these in order.
const Speed kSpeeds[] = {
    {300, Result::kConnect},        {1200, Result::kConnect1200},   {2400, Result::kConnect2400},
    {4800, Result::kConnect4800},   {7200, Result::kConnect7200},   {9600, Result::kConnect9600},
    {12000, Result::kConnect12000}, {14400, Result::kConnect14400}, {16800, Result::kConnect16800},
    {19200, Result::kConnect19200}, {21600, Result::kConnect21600}, {24000, Result::kConnect24000},
    {26400, Result::kConnect26400}, {28800, Result::kConnect28800}, {31200, Result::kConnect31200},
    {33600, Result::kConnect33600},
};
constexpr size_t kSpeedCount = sizeof kSpeeds / sizeof kSpeeds[0];

bool isConnect(Result r) {
    if (r == Result::kConnect) {
        return true;
    }
    for (const Speed &s : kSpeeds) {
        if (s.code == r) {
            return true;
        }
    }
    return false;
}

}  // namespace

const char *resultText(Result r) {
    switch (r) {
        case Result::kOk:
            return "OK";
        case Result::kConnect:
            return "CONNECT";
        case Result::kRing:
            return "RING";
        case Result::kNoCarrier:
            return "NO CARRIER";
        case Result::kError:
            return "ERROR";
        case Result::kConnect1200:
            return "CONNECT 1200";
        case Result::kNoDialTone:
            return "NO DIAL TONE";
        case Result::kBusy:
            return "BUSY";
        case Result::kNoAnswer:
            return "NO ANSWER";
        case Result::kConnect2400:
            return "CONNECT 2400";
        case Result::kConnect9600:
            return "CONNECT 9600";
        case Result::kConnect4800:
            return "CONNECT 4800";
        case Result::kConnect7200:
            return "CONNECT 7200";
        case Result::kConnect12000:
            return "CONNECT 12000";
        case Result::kConnect14400:
            return "CONNECT 14400";
        case Result::kConnect16800:
            return "CONNECT 16800";
        case Result::kConnect19200:
            return "CONNECT 19200";
        case Result::kConnect21600:
            return "CONNECT 21600";
        case Result::kConnect24000:
            return "CONNECT 24000";
        case Result::kConnect26400:
            return "CONNECT 26400";
        case Result::kConnect28800:
            return "CONNECT 28800";
        case Result::kConnect31200:
            return "CONNECT 31200";
        case Result::kConnect33600:
            return "CONNECT 33600";
        default:
            return "ERROR";
    }
}

Result filterForX(Result r, unsigned x) {
    if (isConnect(r)) {
        return x == 0 ? Result::kConnect : r;
    }
    switch (r) {
        case Result::kNoDialTone:
            return (x == 2 || x >= 4) ? r : Result::kNoCarrier;
        case Result::kBusy:
        case Result::kNoAnswer:
            return x >= 3 ? r : Result::kNoCarrier;
        default:
            return r;
    }
}

Result connectFor(uint32_t bps) {
    Result r = Result::kConnect;
    for (const Speed &s : kSpeeds) {
        if (s.bps <= bps) {
            r = s.code;
        }
    }
    return r;
}

uint32_t lineSpeedForN(unsigned n) {
    if (n == 0 || n > kSpeedCount) {
        return 0;
    }
    return kSpeeds[n - 1].bps;
}

size_t formatResult(Result r, const modemcfg::Profile &p, char *buf, size_t cap) {
    if (cap == 0) {
        return 0;
    }
    buf[0] = '\0';
    if (p.get(modemcfg::kQ) == 1) {
        return 0;
    }
    Result shown = filterForX(r, p.get(modemcfg::kX));
    const char cr = static_cast<char>(p.s[3]);
    const char lf = static_cast<char>(p.s[4]);
    int n;
    if (p.get(modemcfg::kV) == 0) {
        n = snprintf(buf, cap, "%u%c", static_cast<unsigned>(shown), cr);
    } else {
        n = snprintf(buf, cap, "%c%c%s%c%c", cr, lf, resultText(shown), cr, lf);
    }
    if (n < 0) {
        return 0;
    }
    return static_cast<size_t>(n) < cap ? static_cast<size_t>(n) : cap - 1;
}

}  // namespace atcmd
