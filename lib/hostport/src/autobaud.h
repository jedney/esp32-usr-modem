// Host speed detection from "AT" (spec 3.2) and 8N1 decoding of a captured
// waveform.
//
// The board's RMT receiver records the RS232 TXD-in pin (TTL side, after the MAX3232)
// as runs of constant level, with 1 us resolution, and hands over a burst once the line
// has been idle for about 32 ms. In command mode the modem reads the computer's typing
// from these bursts rather than from the UART, so it can follow any speed change:
//   - A burst that starts with "A" or "a" gives the speed. Both have the same edges at
//     bits 0, 1, 2, 6 or 7, 8 and 9 (start bit = 0), so the bit time is the time from the
//     start bit to the stop bit divided by 9.
//   - The speed is snapped to the nearest standard rate (300 to 115200).
//   - The whole burst is then decoded in software at that speed.
// A real Sportster also detects parity from "AT"; this modem is 8N1 only.
//
// No Arduino dependencies: unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

#include "modemcfg.h"

namespace autobaud {

// One stretch of constant level on the TXD-in pin, TTL side: 1 = idle / mark / stop bit,
// 0 = start bit / space. us = 0 means "until the end of the burst" (the line went idle).
struct Run {
    uint32_t us;
    uint8_t level;
};

// The rate in modemcfg::kHostRates whose bit time is within 6 % of `bitNs`
// (nanoseconds), or 0.
uint32_t snapRate(uint64_t bitNs);

// The speed of an "A" or "a" at the start of the burst (leading idle is skipped), or 0
// if the first character isn't one of them at a rate in modemcfg::kHostRates.
uint32_t rateOfA(const Run *runs, size_t n);

// Decodes 8N1 characters at `baud`. Characters with a framing error (no stop bit, e.g. a
// break or the wrong speed) are dropped. Returns the number of bytes written to `out`.
size_t decode(const Run *runs, size_t n, uint32_t baud, uint8_t *out, size_t cap);

struct Burst {
    uint32_t baud;  // the speed the burst was decoded at
    bool changed;   // true if that is a new speed, detected from "AT" / "at"
    size_t len;     // bytes written to `out`
};

// Decodes a burst. If it starts with "AT" or "at" (or a lone "A" / "a", for typing) at
// another standard speed, that becomes the new speed; otherwise it is decoded at
// `current`.
Burst decodeBurst(const Run *runs, size_t n, uint32_t current, uint8_t *out, size_t cap);

}  // namespace autobaud
