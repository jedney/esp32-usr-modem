// Saved Wi-Fi and clock settings (spec 3.6): one network, DHCP
// or a static IP, the mDNS name, the NTP server and the time zone.
//
// Kept apart from the modem templates (modemcfg), so AT&F, ATZ and &W never touch them
// (spec 3.7). The board stores the blob in NVS namespace "net" (hw::NvsStore).
//
// The Wi-Fi password is kept here and in the blob only (NVS without flash encryption). Nothing in
// this module or the AT* commands prints it.
//
// No Arduino dependencies: unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

#include "modemcfg.h"

namespace netcfg {

constexpr size_t kSsidMax = 32;
constexpr size_t kPassMax = 64;  // 8-63 characters, or a 64-digit hex key
constexpr size_t kHostMax = 32;  // mDNS name, without ".local"
constexpr size_t kNtpMax = 63;
constexpr size_t kTzMax = 63;  // a zone name from tz.h or a POSIX rule

constexpr const char *kDefaultHost = "usr-modem";
constexpr const char *kDefaultNtp = "pool.ntp.org";
constexpr const char *kDefaultTz = "Pacific/Auckland";

// IPv4 addresses are held as a.b.c.d = a << 24 | b << 16 | c << 8 | d.
struct Settings {
    bool enabled;  // AT*WIFI=ON/OFF
    char ssid[kSsidMax + 1];
    char pass[kPassMax + 1];
    bool dhcp;
    uint32_t ip;
    uint32_t mask;
    uint32_t gateway;
    uint32_t dns;
    char host[kHostMax + 1];
    char ntp[kNtpMax + 1];
    char tz[kTzMax + 1];

    bool hasNetwork() const { return ssid[0] != '\0'; }
};

Settings defaults();

// --- Saved format -------------------------------------------------------------------------
// "USRN", layout version, flags (bit 0 enabled, bit 1 DHCP), then fixed-width,
// zero-padded fields: SSID 32, password 64, IP/mask/gateway/DNS 4 each, name 32, NTP 64,
// zone 64; then CRC-32 of everything before it.
constexpr uint8_t kLayoutVersion = 1;
constexpr size_t kBlobSize = 4 + 1 + 1 + 32 + 64 + 16 + 32 + 64 + 64 + 4;

enum class LoadResult : uint8_t { kSaved, kEmpty, kDamaged };

void serialize(const Settings &s, uint8_t *out);                       // writes kBlobSize bytes
LoadResult deserialize(const uint8_t *in, size_t len, Settings *out);  // defaults if not kSaved
LoadResult load(modemcfg::Store &store, Settings *out);
bool save(modemcfg::Store &store, const Settings &s);
const char *loadResultText(LoadResult r);

// --- Checks -------------------------------------------------------------------------------
bool validSsid(const char *s);      // 1-32 bytes, no control characters
bool validPassword(const char *s);  // "" (open), 8-63 bytes without control bytes, or 64 hex digits
// mDNS name: 1-32 of a-z 0-9 -, not starting or ending with '-'. Upper case is accepted
// and lowered by normalizeHost(), which also drops a trailing ".local".
bool normalizeHost(const char *in, char *out, size_t cap);
bool validServer(const char *s);  // NTP server: 1-63 of letters, digits, '-', '.'

// "a.b.c.d", then the end, ',' or a space. `rest` (optional) is set after it and any ','.
bool parseIp(const char *s, uint32_t *out, const char **rest = nullptr);
void formatIp(uint32_t ip, char *buf, size_t cap);
// A usable static set: a host address, a contiguous mask, the gateway in the same subnet.
bool validStatic(uint32_t ip, uint32_t mask, uint32_t gateway);

// Overwrites a buffer so a password doesn't linger in memory.
void wipe(char *buf, size_t len);

}  // namespace netcfg
