// Result codes (project brief 6.5), numbered and worded as on the USR Sportster 33.6.

#pragma once

#include <cstddef>
#include <cstdint>

#include "modemcfg.h"

namespace atcmd {

enum class Result : uint16_t {
    kOk = 0,
    kConnect = 1,  // 300 bps, or any speed with X0
    kRing = 2,
    kNoCarrier = 3,
    kError = 4,
    kConnect1200 = 5,
    kNoDialTone = 6,
    kBusy = 7,
    kNoAnswer = 8,
    kConnect2400 = 10,
    kConnect9600 = 13,
    kConnect4800 = 18,
    kConnect7200 = 20,
    kConnect12000 = 21,
    kConnect14400 = 25,
    kConnect16800 = 43,
    kConnect19200 = 85,
    kConnect21600 = 91,
    kConnect24000 = 99,
    kConnect26400 = 103,
    kConnect28800 = 107,
    kConnect31200 = 151,
    kConnect33600 = 155,
    kConnect28000 = 256,
    kConnect29333 = 260,
    kConnect30666 = 264,
    kConnect32000 = 268,
    kConnect33333 = 180,
    kConnect34666 = 272,
    kConnect36000 = 276,
    kConnect37333 = 184,
    kConnect38666 = 280,
    kConnect40000 = 284,
    kConnect41333 = 188,
    kConnect42666 = 192,
    kConnect44000 = 196,
    kConnect45333 = 200,
    kConnect46666 = 204,
    kConnect48000 = 208,
    kConnect49333 = 212,
    kConnect50666 = 216,
    kConnect52000 = 220,
    kConnect53333 = 224,
    kConnect54666 = 228,
    kConnect56000 = 232,
};

// "OK", "NO DIAL TONE", "CONNECT 33600" ...
const char *resultText(Result r);

// The code actually sent with result code set Xn: with X0 every CONNECT is plain
// CONNECT; NO DIAL TONE needs X2 or X4; BUSY and NO ANSWER need X3 or X4. A code the
// set doesn't include becomes NO CARRIER (CONNECT for the connect codes).
Result filterForX(Result r, unsigned x);

// CONNECT code for a line speed in bps (300 ... 56000). Unknown speeds give the
// nearest lower one.
Result connectFor(uint32_t bps);

// Line speed for &N1-&N38 (0 for &N0 = highest, 56000). V.90 indexes after &N16
// are not in ascending speed order; existing &N1-&N16 meanings stay unchanged.
uint32_t lineSpeedForN(unsigned n);

// Formats a result for profile `p` (V, Q, X, S3, S4) into buf. Returns the length;
// 0 when Q1 suppresses it.
size_t formatResult(Result r, const modemcfg::Profile &p, char *buf, size_t cap);
size_t formatConnect(uint32_t reportedSpeed, uint32_t operationalSpeed, const modemcfg::Profile &p,
                     char *buf, size_t cap);

}  // namespace atcmd
