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

// USR V.90 Technical Quick Reference (v90tecrf.pdf), &N1-&N38 in index order.
// &N17-&N20 are slower than &N16: connectFor() must compare speeds, not indexes.
const Speed kSpeeds[] = {
    {300, Result::kConnect},        {1200, Result::kConnect1200},   {2400, Result::kConnect2400},
    {4800, Result::kConnect4800},   {7200, Result::kConnect7200},   {9600, Result::kConnect9600},
    {12000, Result::kConnect12000}, {14400, Result::kConnect14400}, {16800, Result::kConnect16800},
    {19200, Result::kConnect19200}, {21600, Result::kConnect21600}, {24000, Result::kConnect24000},
    {26400, Result::kConnect26400}, {28800, Result::kConnect28800}, {31200, Result::kConnect31200},
    {33600, Result::kConnect33600}, {28000, Result::kConnect28000}, {29333, Result::kConnect29333},
    {30666, Result::kConnect30666}, {32000, Result::kConnect32000}, {33333, Result::kConnect33333},
    {34666, Result::kConnect34666}, {36000, Result::kConnect36000}, {37333, Result::kConnect37333},
    {38666, Result::kConnect38666}, {40000, Result::kConnect40000}, {41333, Result::kConnect41333},
    {42666, Result::kConnect42666}, {44000, Result::kConnect44000}, {45333, Result::kConnect45333},
    {46666, Result::kConnect46666}, {48000, Result::kConnect48000}, {49333, Result::kConnect49333},
    {50666, Result::kConnect50666}, {52000, Result::kConnect52000}, {53333, Result::kConnect53333},
    {54666, Result::kConnect54666}, {56000, Result::kConnect56000},
};
constexpr size_t kSpeedCount = sizeof kSpeeds / sizeof kSpeeds[0];
static_assert(kSpeedCount == modemcfg::kMaxLineSpeedIndex, "settings and speed indexes agree");

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
        case Result::kConnect28000:
            return "CONNECT 28000";
        case Result::kConnect29333:
            return "CONNECT 29333";
        case Result::kConnect30666:
            return "CONNECT 30666";
        case Result::kConnect32000:
            return "CONNECT 32000";
        case Result::kConnect33333:
            return "CONNECT 33333";
        case Result::kConnect34666:
            return "CONNECT 34666";
        case Result::kConnect36000:
            return "CONNECT 36000";
        case Result::kConnect37333:
            return "CONNECT 37333";
        case Result::kConnect38666:
            return "CONNECT 38666";
        case Result::kConnect40000:
            return "CONNECT 40000";
        case Result::kConnect41333:
            return "CONNECT 41333";
        case Result::kConnect42666:
            return "CONNECT 42666";
        case Result::kConnect44000:
            return "CONNECT 44000";
        case Result::kConnect45333:
            return "CONNECT 45333";
        case Result::kConnect46666:
            return "CONNECT 46666";
        case Result::kConnect48000:
            return "CONNECT 48000";
        case Result::kConnect49333:
            return "CONNECT 49333";
        case Result::kConnect50666:
            return "CONNECT 50666";
        case Result::kConnect52000:
            return "CONNECT 52000";
        case Result::kConnect53333:
            return "CONNECT 53333";
        case Result::kConnect54666:
            return "CONNECT 54666";
        case Result::kConnect56000:
            return "CONNECT 56000";
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
    uint32_t best = 0;
    for (const Speed &s : kSpeeds) {
        if (s.bps <= bps && s.bps > best) {
            r = s.code;
            best = s.bps;
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

size_t formatConnect(uint32_t reportedSpeed, uint32_t operationalSpeed, const modemcfg::Profile &p,
                     char *buf, size_t cap) {
    size_t n = formatResult(connectFor(reportedSpeed), p, buf, cap);
    unsigned detail = p.get(modemcfg::kAmpA);
    if (n < 2 || p.get(modemcfg::kV) == 0 || p.get(modemcfg::kX) == 0 || !detail) {
        return n;
    }
    // TCP supplies the reliable channel; no modem compression or LAPM frames are
    // implemented. Keep the default plain, and do not claim V.42bis compression.
    const char *reliable = operationalSpeed >= 1200 && p.get(modemcfg::kAmpM) >= 4 ? "/ARQ" : "";
    const char *modulation = operationalSpeed > 33600   ? "/V90"
                             : operationalSpeed > 14400 ? "/V34"
                             : operationalSpeed > 2400  ? "/V32"
                             : operationalSpeed > 300   ? "/V22"
                                                        : "/V21";
    int written =
        snprintf(buf + n - 2, cap - (n - 2), "%s%s%s%c%c", reliable, detail >= 2 ? modulation : "",
                 detail >= 3 ? "/NONE" : "", static_cast<char>(p.s[3]), static_cast<char>(p.s[4]));
    if (written < 0) {
        return 0;
    }
    size_t total = n - 2 + static_cast<size_t>(written);
    return total < cap ? total : cap - 1;
}

}  // namespace atcmd
