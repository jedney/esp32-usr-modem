// Modem settings: Sportster 14400 identity with the later USR speed range through 56k,
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
    kAmpB,  // &B  serial speed: 0 follows the call, 1 stays at the speed of "AT", 2 mixed
    kAmpC,  // &C  DCD: 0 always on, 1 follows the connection
    kAmpD,  // &D  DTR: 0 ignored, 2 drop hangs up, 3 drop resets
    kAmpG,  // &G  guard tone
    kAmpH,  // &H  transmit flow control: 0 off, 1 CTS, 2 XON/XOFF, 3 both
    kAmpI,  // &I  receive software flow control
    kAmpK,  // &K  data compression
    kAmpM,  // &M  error control
    kAmpN,  // &N  line speed: 0 = highest (56000), 1-38 fixed; factory = 8 (14400)
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
    uint64_t allowed;  // bit n set = value n is accepted (line-speed indexes reach 38)
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
    uint32_t baud;  // serial speed when the template was written with &W (BAUD= in ATI5)
    bool lineExplicit = false;  // factory uses model CONNECT fallback; explicit &N overrides it

    uint8_t get(Setting x) const { return v[x]; }
};

bool operator==(const Profile &a, const Profile &b);
inline bool operator!=(const Profile &a, const Profile &b) {
    return !(a == b);
}

// &F0 (no flow control), &F1 (hardware flow control, the default) or &F2 (software
// flow control). Any other number gives &F1.
Profile factoryProfile(unsigned n);

// BAUD= of a template that was never written with &W. The RS232 port also starts at the
// power-on template's speed, until the computer sends "AT".
constexpr uint32_t kFactoryBaud = 38400;
constexpr unsigned kMaxLineSpeedIndex = 38;
constexpr uint32_t kMaxLineSpeed = 56000;
constexpr uint32_t kMaxCallDelayMs = 60000;

// The host serial speeds the modem detects from "AT" and accepts for AT*BAUD (the
// Sportster's range).
constexpr uint32_t kHostRates[] = {300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200};
bool isHostRate(uint32_t baud);

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
    // AT*BAUD: 0 = detect the RS232 speed from "AT" (default), otherwise the port stays at
    // this speed and nothing is detected. Not part of a template.
    uint32_t hostBaud;
    uint32_t model = 14400;  // device-wide identity, not changed by &F/Z
    bool throttle = false;
    bool answerConsole = false;     // AT*ANSWERPORT, RS232 by default
    uint32_t ringDelayMs = 1500;    // outgoing ringback before TCP connection
    uint32_t connectDelayMs = 250;  // pause after the handshake, before CONNECT
};

// As shipped: Y0 = &F1, Y1 = &F2, power-on from Y0, no stored numbers, speed from "AT".
Nvram factoryNvram();

// The profile loaded at power-on and by ATZ0.
Profile powerOnProfile(const Nvram &n);

// --- Saved format ---------------------------------------------------------------------------
// magic "USRM", layout version, then the Nvram fields, then a CRC-32 of everything
// before it. Multi-byte values are little-endian.
//   Layout 1 (v0.4): y, templates, phone numbers.
//   Layout 2 (v0.5): adds hostBaud after the phone numbers.
//   Layout 5 (v0.17): appends ringback and pre-CONNECT delay milliseconds.
constexpr uint8_t kLayoutVersion = 5;
constexpr size_t kBlobSizeV1 =
    4 + 1 + 1 + kTemplates * (kSettingCount + kSRegCount + 4) + kPhoneSlots * kPhoneLen + 4;
constexpr size_t kBlobSizeV2 = kBlobSizeV1 + 4;
constexpr size_t kBlobSizeV3 = kBlobSizeV2 + kTemplates + 4 + 1;
constexpr size_t kBlobSizeV4 = kBlobSizeV3 + 1;
constexpr size_t kBlobSize = kBlobSizeV4 + 8;

bool validModel(uint32_t speed);
const char *modelName(uint32_t speed);

void serialize(const Nvram &n, uint8_t *out);  // writes kBlobSize bytes

enum class LoadResult : uint8_t {
    kOk,
    kMigrated,  // layout 1 (v0.4) read and converted; &B set to 1 in both templates
    kEmpty,
    kBadSize,
    kBadMagic,
    kBadVersion,
    kBadCrc,
    kBadValue
};

// On anything but kOk or kMigrated, *out is set to factoryNvram().
LoadResult deserialize(const uint8_t *in, size_t len, Nvram *out);

// "saved settings", "nothing saved yet", ... for the boot banner.
const char *loadResultText(LoadResult r);

uint32_t crc32(const uint8_t *data, size_t len);

// Where the blob lives. The board uses NVS (hw::NvsStore); tests use memory.
constexpr size_t kLoadError = static_cast<size_t>(-1);
class Store {
public:
    virtual ~Store() = default;
    // Copies the blob into buf; 0 means absent, kLoadError means unreadable/oversized.
    // Never return a partial read as an empty store. Error results must fail validation.
    virtual size_t load(uint8_t *buf, size_t cap) = 0;
    virtual bool save(const uint8_t *data, size_t len) = 0;
    virtual bool erase() = 0;
};

LoadResult loadNvram(Store &store, Nvram *out);
bool saveNvram(Store &store, const Nvram &n);

}  // namespace modemcfg
