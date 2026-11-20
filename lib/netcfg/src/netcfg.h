// Wi-Fi and clock controller (spec 2.4, 3.6, 3.7): keeps the modem on the saved
// network, retries when it drops, runs AT*WIFIJOIN and AT*WIFISCAN, starts mDNS and NTP
// once connected, and decides the Wi-Fi lights and the console notices.
//
// The radio itself is behind Radio (the board's is hw::EspRadio in lib/hw; the unit
// tests use a fake). No Arduino dependencies: unit-tested on the PC.
//
// Front ARQ LED (the S3 board's own RGB LED is not used: it can't be seen in the case):
//   no network set up                    off
//   network saved, connecting/retrying   blinking
//   connected                            on
//   switched off (AT*WIFI=OFF)           off

#pragma once

#include <cstddef>
#include <cstdint>

#include "net_settings.h"
#include "net_tz.h"

namespace netcfg {

enum class LinkState : uint8_t {
    kOff,         // radio off
    kDown,        // radio on, not connected and not trying
    kConnecting,  // joining or waiting for an address
    kUp,          // connected with an IP address
};

// Why the last attempt failed or the link dropped.
enum class Reason : uint8_t { kNone, kWrongPassword, kNotFound, kNoAnswer, kLost, kOther };
const char *reasonText(Reason r);

enum class Security : uint8_t { kOpen, kWep, kWpa, kWpa2, kWpa3, kEnterprise, kOther };
const char *securityText(Security s);

struct LinkInfo {
    LinkState state;
    Reason reason;  // set when state became kDown
    int8_t rssi;    // dBm, while up
    uint8_t channel;
    uint32_t ip;  // while up (a.b.c.d = a << 24 ...)
    uint32_t mask;
    uint32_t gateway;
    uint32_t dns;
};

struct ScanEntry {
    char ssid[kSsidMax + 1];
    int8_t rssi;
    uint8_t channel;
    Security security;
};

class Radio {
public:
    virtual ~Radio() = default;
    // Radio on and join `s.ssid` with `s.pass`, by DHCP or `s`'s static address, as
    // `s.host`. Leaves any current network first.
    virtual void connect(const Settings &s) = 0;
    virtual void leave() = 0;  // leave the network, radio stays on (for a scan)
    virtual void off() = 0;    // leave and turn the radio off
    virtual LinkInfo link() = 0;
    virtual bool scanStart() = 0;  // turns the radio on if needed
    // -1 still scanning, -2 failed, else the number of entries written (up to `max`).
    virtual int scanResult(ScanEntry *out, int max) = 0;
    // Start or restart mDNS (`s.host`) and NTP (`s.ntp`). Called when the link comes up
    // and when the name or the server changes.
    virtual void startServices(const Settings &s) = 0;
    virtual int64_t utcNow() = 0;    // seconds since 1970, 0 = clock not set
    virtual int64_t lastSync() = 0;  // UTC of the last NTP update, 0 = none yet
    virtual void mac(uint8_t out[6]) = 0;
};

enum class Arq : uint8_t { kOff, kBlink, kOn };  // hostport::WifiLed kOff/kConnecting/kConnected

enum class JoinResult : uint8_t { kNone, kRunning, kJoined, kFailed };
enum class ScanState : uint8_t { kIdle, kRunning, kDone, kFailed };

class Net {
public:
    static constexpr uint32_t kJoinTimeoutMs = 20000;     // AT*WIFIJOIN gives up
    static constexpr uint32_t kAttemptTimeoutMs = 30000;  // a background attempt gives up
    static constexpr size_t kScanMax = 20;
    static constexpr size_t kNoticeMax = 96;

    Net(modemcfg::Store &store, Radio &radio);
    Net(const Net &) = delete;
    Net &operator=(const Net &) = delete;

    // Loads the settings and starts joining the saved network. Call once at boot.
    LoadResult begin(uint32_t nowMs);
    void tick(uint32_t nowMs);  // call every loop

    const Settings &settings() const { return s_; }
    const LinkInfo &link() const { return link_; }
    bool up() const { return link_.state == LinkState::kUp; }
    Radio &radio() { return radio_; }
    // Text for AT*WIFI? and the banner: "connected", "connecting", "not set up", ...
    const char *stateText() const;

    // --- Lights ------------------------------------------------------------------------
    Arq arq() const;

    // --- Changes (saved at once; false if not valid or not saved) -----------------------
    bool setEnabled(bool on);
    bool forget();  // forget the network (and its password)
    bool setDhcp();
    bool setStatic(uint32_t ip, uint32_t mask, uint32_t gateway, uint32_t dns);
    bool setHost(const char *name);  // normalized: lower case, ".local" dropped
    bool setNtp(const char *server);
    bool setTz(const char *zone);  // a zone name (any case) or a POSIX rule

    // --- AT*WIFIJOIN --------------------------------------------------------------------
    // Tries `ssid` with `pass`. The settings change only if it connects (result through
    // joinResult()); on failure the previous network is joined again.
    bool join(const char *ssid, const char *pass, uint32_t nowMs,
              uint32_t timeoutMs = kJoinTimeoutMs);
    void guardBrowserChange(uint32_t nowMs);
    JoinResult joinResult() const { return join_; }
    Reason joinReason() const { return joinReason_; }
    void cancelJoin();
    void clearJoin();  // after the result has been reported

    // --- AT*WIFISCAN --------------------------------------------------------------------
    // While not connected, the background attempt pauses for the scan.
    bool scanStart(uint32_t nowMs);
    ScanState scanState() const { return scan_; }
    void cancelScan();
    // The last scan, strongest first, one entry per name, hidden networks left out.
    size_t scanCount() const { return scanCount_; }
    const ScanEntry &scanEntry(size_t i) const { return scanList_[i]; }

    // --- Clock --------------------------------------------------------------------------
    const tz::Rule &zone() const { return zone_; }
    int64_t utcNow() { return radio_.utcNow(); }

    // --- Console notices (Wi-Fi connected / lost, clock set) ----------------------------
    // One line, no line ending. False if there is nothing new.
    bool takeNotice(char *buf, size_t cap);

private:
    static constexpr size_t kRawMax = 32;  // scan entries read from the radio

    bool store();
    bool apply(const Settings &next, bool cancel = false);
    void attempt(uint32_t nowMs);
    void reconnect();
    void pollJoin(uint32_t nowMs);
    void restoreAfterJoin();
    void notice(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
    void finishScan(size_t n);
    void radioOffIfDisabled();
    uint32_t retryDelay() const;

    modemcfg::Store &store_;
    Radio &radio_;
    Settings s_;
    tz::Rule zone_;
    LinkInfo link_ = {};
    uint32_t now_ = 0;
    bool wasUp_ = false;
    Reason lastFail_ = Reason::kNone;  // why the last background attempt failed
    // Background attempts
    bool trying_ = false;  // an attempt is under way
    uint32_t attemptAt_ = 0;
    uint32_t retryAt_ = 0;
    bool retryDue_ = false;
    unsigned failures_ = 0;
    // AT*WIFIJOIN
    Settings candidate_;
    JoinResult join_ = JoinResult::kNone;
    Reason joinReason_ = Reason::kNone;
    uint32_t joinAt_ = 0;
    uint32_t joinTryAt_ = 0;
    uint32_t joinTimeout_ = kJoinTimeoutMs;
    Settings baseline();
    Settings browserPrevious_ = {};
    bool browserGuard_ = false;
    uint32_t browserAt_ = 0;
    // AT*WIFISCAN
    ScanState scan_ = ScanState::kIdle;
    bool scanPaused_ = false;  // the background attempt waits for the scan
    ScanEntry raw_[kRawMax] = {};
    ScanEntry scanList_[kScanMax] = {};
    size_t scanCount_ = 0;
    // Clock
    bool clockNoticed_ = false;
    // Notice
    char notice_[kNoticeMax] = {};
    bool hasNotice_ = false;
};

}  // namespace netcfg
