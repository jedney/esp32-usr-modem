// Dial-string classifier (spec 3.3).
//
// Takes the part of an ATD command after the optional T or P, e.g. for
// "ATDT001002003004789" the input is "001002003004789", and works out what it means:
//
//   * Zero-padded IPv4 address + optional port, digits only:
//       12 digits        -> IP, port 23        ("170064142064")
//       13-17 digits     -> IP + port           ("001002003004789" -> 1.2.3.4:789)
//   * Hostname: quoted, or containing '.' or ':'  ("\"bbs.example.com:2323\"",
//       "www.amigaretro.com", "170.64.142.64:23"). Dial modifiers are NOT stripped.
//   * Reserved number: 123 (configuration screen), 666 (Internet, PPP), 667 (SLIP),
//       5517546 (PPP, Zimodem), 5517545 (SLIP, Zimodem).
//   * Otherwise 1-11 digits -> phonebook number.
//
// For numeric strings, spaces, dashes, brackets and Hayes dial modifiers
// (, W @ ! ; T P and trailing ;) are removed first.
//
// Invalid strings (octet > 255, port 0 or > 65535, more than 17 digits, empty, bad
// characters, host too long) return Kind::Invalid so the modem can answer ERROR before
// any sound plays.
//
// No Arduino dependencies: this module is unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

namespace dialstring {

constexpr uint16_t kDefaultPort = 23;
constexpr size_t kMaxHostLen = 63;  // excluding the terminating NUL
constexpr size_t kMaxDigits = 17;   // 12 IP digits + up to 5 port digits
constexpr size_t kMaxPhonebookDigits = 11;

enum class Kind : uint8_t {
    Invalid,
    IpAddress,  // ip[] and port are set; host holds the dotted form
    Host,       // host and port are set
    Phonebook,  // number holds the digits
    Special,    // number holds the digits, special says which
};

enum class Special : uint8_t {
    None,
    ConfigMenu,  // 123
    Ppp,         // 666 or 5517546
    Slip,        // 667 or 5517545
};

struct Result {
    Kind kind = Kind::Invalid;
    Special special = Special::None;
    uint8_t ip[4] = {0, 0, 0, 0};
    uint16_t port = kDefaultPort;
    char host[kMaxHostLen + 1] = {0};
    char number[kMaxDigits + 1] = {0};
};

// Classify a dial string. `input` must be NUL-terminated; nullptr gives Invalid.
Result classify(const char *input);

// Short human-readable name of a Kind ("invalid", "ip", "host", "phonebook", "special").
const char *kindName(Kind kind);

// Short human-readable name of a Special ("none", "config", "ppp", "slip").
const char *specialName(Special special);

}  // namespace dialstring
