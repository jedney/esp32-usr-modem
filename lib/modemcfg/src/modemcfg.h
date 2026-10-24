// Modem settings (project brief 6.2, 6.6, 6.7): the USR Sportster 33.6 settings, the
// three read-only factory templates (&F0-&F2), the two stored templates (Y0/Y1, written
// with &W0/&W1), the four stored phone numbers (&Z0-&Z3) and the saved format.
//
// Behaviour and defaults follow the USR Sportster technical reference; differences
// decided for this project are listed in docs/decisions-log.md (v0.4).
//
// No Arduino dependencies: this module is unit-tested on the PC. The board stores the
// blob in NVS through hw::NvsStore (lib/hw).

#pragma once

#include <cstddef>
#include <cstdint>

namespace modemcfg {

// --- Settings ---------------------------------------------------------------------------
// The order is part of the saved format: add new settings at the end and bump
// kLayoutVersion.
enum Setting : uint8_t {
    kB,     // B   answer sequence (0 ITU-T, 1 Bell)
    kE,     // E   command echo
    kF,     // F   online local echo (1 = off)
    kL,     // L   speaker volume
    kM,     // M   speaker control
    kQ,     // Q   quiet (0 results shown, 1 none, 2 none when answering)
    kV,     // V   verbal (1) or numeric (0) results
    kX,     // X   result code set
    kTone,  // P/T default dial method (1 = tone). Shown as DIAL=TONE / DIAL=PULSE.
    kAmpA,  // &A  extra result codes (ARQ / modulation / protocol suffixes)
    kAmpB,  // &B  host serial speed: 0 detect from "AT", 1 fixed (brief 6.2)
    kAmpC,  // &C  DCD: 0 always on, 1 follows the connection
    kAmpD,  // &D  DTR: 0 ignored, 2 drop hangs up, 3 drop resets
    kAmpG,  // &G  guard tone
    kAmpH,  // &H  transmit flow control: 0 off, 1 CTS, 2 XON/XOFF, 3 both
    kAmpI,  // &I  receive software flow control
    kAmpK,  // &K  data compression
    kAmpM,  // &M  error control
    kAmpN,  // &N  line speed: 0 = highest (33600), 1-16 fixed (brief 6.2)
    kAmpP,  // &P  pulse make/break ratio (1 = UK)
    kAmpR,  // &R  receive hardware flow control: 1 ignore RTS, 2 RTS
    kAmpS,  // &S  DSR: fixed at 0, DSR is hard-wired on (brief 6.2)
    kAmpT,  // &T  remote digital loopback: 4 allowed, 5 refused
    kAmpU,  // &U  lowest line speed
    kAmpY,  // &Y  break handling
    kSettingCount
};

struct SettingInfo {
    const char *name;  // "E", "&C"
    uint32_t allowed;  // bit n set = value n is accepted
};

const SettingInfo &settingInfo(Setting s);

// Finds a setting by its command letter: '&' + 'C' -> kAmpC; 0 + 'E' -> kE. Case-
// insensitive. P and T are not here (they are commands, not a value). Returns -1 if
// there is no such setting.
int findSetting(char prefix, char letter);

bool settingAllows(Setting s, unsigned value);

// --- Profile (the active settings, or one stored template) ---------------------------------
constexpr size_t kSRegCount = 64;  // S0-S63; Sportster registers end at S54

struct Profile {
    uint8_t v[kSettingCount];
    uint8_t s[kSRegCount];
    uint32_t baud;  // host speed stored with the template (BAUD= in ATI4/ATI5)

    uint8_t get(Setting x) const { return v[x]; }
};

bool operator==(const Profile &a, const Profile &b);
inline bool operator!=(const Profile &a, const Profile &b) {
    return !(a == b);
}

// &F0 (no flow control), &F1 (hardware flow control, the default) or &F2 (software
// flow control). Any other number gives &F1.
Profile factoryProfile(unsigned n);

constexpr unsigned kDefaultFixedBaud = 38400;  // &B1 default (brief 6.2, Q1)

// S-registers shown by ATI4 (current settings) and ATI5 (stored templates), in the
// order a real Sportster 33600 lists them. Both lists end with 0xFF.
extern const uint8_t kSRegsCurrent[];
extern const uint8_t kSRegsStored[];

// --- Stored settings ("NVRAM") ----------------------------------------------------------
constexpr unsigned kTemplates = 2;   // Y0, Y1
constexpr unsigned kPhoneSlots = 4;  // &Z0-&Z3
constexpr size_t kPhoneLen = 40;     // characters per stored number

struct Nvram {
    Profile tmpl[kTemplates];
    uint8_t y;  // power-on template: 0/1 = Y0/Y1, 2-4 = factory &F0-&F2
    char phone[kPhoneSlots][kPhoneLen + 1];
};

// As shipped: Y0 = &F1, Y1 = &F2, power-on from Y0, no stored numbers.
Nvram factoryNvram();

// The profile loaded at power-on and by ATZ0.
Profile powerOnProfile(const Nvram &n);

// --- Saved format ---------------------------------------------------------------------------
// magic "USRM", layout version, then the Nvram fields, then a CRC-32 of everything
// before it. Multi-byte values are little-endian.
constexpr uint8_t kLayoutVersion = 1;
constexpr size_t kBlobSize =
    4 + 1 + 1 + kTemplates * (kSettingCount + kSRegCount + 4) + kPhoneSlots * kPhoneLen + 4;

void serialize(const Nvram &n, uint8_t *out);  // writes kBlobSize bytes

enum class LoadResult : uint8_t {
    kOk,
    kEmpty,
    kBadSize,
    kBadMagic,
    kBadVersion,
    kBadCrc,
    kBadValue
};

// On anything but kOk, *out is set to factoryNvram().
LoadResult deserialize(const uint8_t *in, size_t len, Nvram *out);

// "saved settings", "nothing saved yet", ... for the boot banner.
const char *loadResultText(LoadResult r);

uint32_t crc32(const uint8_t *data, size_t len);

// Where the blob lives. The board uses NVS (hw::NvsStore); tests use memory.
class Store {
public:
    virtual ~Store() = default;
    // Copies the saved blob into buf; returns its length, or 0 if nothing is saved.
    virtual size_t load(uint8_t *buf, size_t cap) = 0;
    virtual bool save(const uint8_t *data, size_t len) = 0;
    virtual bool erase() = 0;
};

LoadResult loadNvram(Store &store, Nvram *out);
bool saveNvram(Store &store, const Nvram &n);

}  // namespace modemcfg
