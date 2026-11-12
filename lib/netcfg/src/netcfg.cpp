// Wi-Fi and clock controller: see netcfg.h.
#include "netcfg.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace netcfg {

namespace {

void copy(char *dst, size_t cap, const char *src) {
    snprintf(dst, cap, "%s", src);
}

bool due(uint32_t now, uint32_t at) {
    return static_cast<int32_t>(now - at) >= 0;
}

}  // namespace

const char *reasonText(Reason r) {
    switch (r) {
        case Reason::kWrongPassword:
            return "wrong password";
        case Reason::kNotFound:
            return "network not found";
        case Reason::kNoAnswer:
            return "no answer";
        case Reason::kLost:
            return "connection lost";
        case Reason::kOther:
            return "refused";
        case Reason::kNone:
        default:
            return "unknown";
    }
}

const char *securityText(Security s) {
    switch (s) {
        case Security::kOpen:
            return "open";
        case Security::kWep:
            return "WEP";
        case Security::kWpa:
            return "WPA";
        case Security::kWpa2:
            return "WPA2";
        case Security::kWpa3:
            return "WPA3";
        case Security::kEnterprise:
            return "WPA2-Ent";
        case Security::kOther:
        default:
            return "?";
    }
}

Net::Net(modemcfg::Store &store, Radio &radio)
    : store_(store), radio_(radio), s_(defaults()), zone_(), candidate_(defaults()) {
    tz::resolve(s_.tz, &zone_, nullptr, 0);
}

LoadResult Net::begin(uint32_t nowMs) {
    now_ = nowMs;
    LoadResult r = load(store_, &s_);
    tz::resolve(s_.tz, &zone_, nullptr, 0);
    if (s_.enabled && s_.hasNetwork()) {
        attempt(nowMs);
    } else if (!s_.enabled) {
        radio_.off();
    }
    return r;
}

bool Net::store() {
    if (browserGuard_) {
        return true;  // commit only after the candidate network connects
    }
    return save(store_, s_);
}

void Net::guardBrowserChange(uint32_t nowMs) {
    now_ = nowMs;
    if (!browserGuard_) {
        clearJoin();  // an old result nobody collected must not fail this change
        browserPrevious_ = s_;
        browserAt_ = nowMs;
        browserGuard_ = true;
    }
}

void Net::attempt(uint32_t nowMs) {
    radio_.connect(s_);
    trying_ = true;
    attemptAt_ = nowMs;
    retryDue_ = false;
    wasUp_ = false;
    link_.state = LinkState::kConnecting;
}

uint32_t Net::retryDelay() const {
    static const uint32_t kDelays[] = {5000, 10000, 20000, 30000};
    const size_t n = sizeof kDelays / sizeof kDelays[0];
    return failures_ >= 1 && failures_ <= n ? kDelays[failures_ - 1] : 60000;
}

void Net::notice(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(notice_, sizeof notice_, fmt, ap);
    va_end(ap);
    hasNotice_ = true;
}

bool Net::takeNotice(char *buf, size_t cap) {
    if (!hasNotice_) {
        return false;
    }
    copy(buf, cap, notice_);
    hasNotice_ = false;
    return true;
}

// --- Main loop ------------------------------------------------------------------------------

void Net::tick(uint32_t nowMs) {
    if (static_cast<int32_t>(nowMs - now_) < 0)
        return;
    now_ = nowMs;
    link_ = radio_.link();
    if (browserGuard_ && join_ != JoinResult::kRunning) {
        bool connected = link_.state == LinkState::kUp && join_ != JoinResult::kFailed;
        bool failed = nowMs - browserAt_ >= 60000 || join_ == JoinResult::kFailed;
        if (connected || failed) {
            bool committed = connected && save(store_, s_);
            if (!committed) {
                s_ = browserPrevious_;
                tz::resolve(s_.tz, &zone_, nullptr, 0);
                attempt(nowMs);
            }
            wipe(browserPrevious_.pass, sizeof browserPrevious_.pass);
            browserGuard_ = false;
        }
    }

    if (!clockNoticed_ && radio_.lastSync() != 0) {
        clockNoticed_ = true;
        char t[48];
        tz::formatLocal(zone_, radio_.utcNow(), t, sizeof t);
        notice("Clock: set by NTP, %s", t);
    }

    if (scan_ == ScanState::kRunning) {
        int n = radio_.scanResult(raw_, static_cast<int>(kRawMax));
        if (n >= 0 || n == -2) {
            if (n >= 0) {
                finishScan(static_cast<size_t>(n));
                scan_ = ScanState::kDone;
            } else {
                scan_ = ScanState::kFailed;
            }
            radioOffIfDisabled();
            if (scanPaused_) {
                scanPaused_ = false;
                retryDue_ = true;  // carry on with the saved network at once
                retryAt_ = nowMs;
            }
        }
    }

    if (join_ == JoinResult::kRunning) {
        pollJoin(nowMs);
        return;
    }
    if (scan_ == ScanState::kRunning) {
        return;  // the background attempt waits for the scan
    }

    if (!s_.enabled) {
        if (link_.state != LinkState::kOff) {
            radio_.off();
        }
        trying_ = false;
        wasUp_ = false;
        return;
    }
    if (!s_.hasNetwork()) {
        if (link_.state == LinkState::kUp || link_.state == LinkState::kConnecting) {
            radio_.leave();
        }
        trying_ = false;
        wasUp_ = false;
        return;
    }

    switch (link_.state) {
        case LinkState::kUp:
            if (!wasUp_) {
                wasUp_ = true;
                trying_ = false;
                failures_ = 0;
                lastFail_ = Reason::kNone;
                radio_.startServices(s_);
                char ip[16];
                formatIp(link_.ip, ip, sizeof ip);
                notice("Wi-Fi: connected to %s, IP %s", s_.ssid, ip);
            }
            break;

        case LinkState::kConnecting:
            if (trying_ && due(nowMs, attemptAt_ + kAttemptTimeoutMs)) {
                radio_.leave();
                trying_ = false;
                ++failures_;
                if (lastFail_ != Reason::kNoAnswer) {
                    notice("Wi-Fi: can't join %s: %s. Trying again", s_.ssid,
                           reasonText(Reason::kNoAnswer));
                }
                lastFail_ = Reason::kNoAnswer;
                retryDue_ = true;
                retryAt_ = nowMs + retryDelay();
            }
            break;

        case LinkState::kDown:
        case LinkState::kOff:
        default:
            if (wasUp_) {
                wasUp_ = false;
                failures_ = 0;
                lastFail_ = Reason::kLost;
                notice("Wi-Fi: lost %s, reconnecting", s_.ssid);
                retryDue_ = true;
                retryAt_ = nowMs;
            } else if (trying_) {
                trying_ = false;
                ++failures_;
                const Reason r = link_.reason == Reason::kNone ? Reason::kOther : link_.reason;
                if (r != lastFail_) {
                    notice("Wi-Fi: can't join %s: %s. Trying again", s_.ssid, reasonText(r));
                }
                lastFail_ = r;
                retryDue_ = true;
                retryAt_ = nowMs + retryDelay();
            } else if (!retryDue_) {
                retryDue_ = true;  // after a scan, a change of settings, ...
                retryAt_ = nowMs;
            }
            if (retryDue_ && due(nowMs, retryAt_)) {
                attempt(nowMs);
            }
            break;
    }
}

const char *Net::stateText() const {
    if (join_ == JoinResult::kRunning) {
        return "joining";
    }
    if (!s_.enabled) {
        return "off";
    }
    if (!s_.hasNetwork()) {
        return "not set up";
    }
    if (up()) {
        return "connected";
    }
    switch (lastFail_) {
        case Reason::kWrongPassword:
            return "not connected (wrong password), trying again";
        case Reason::kNotFound:
            return "not connected (network not found), trying again";
        case Reason::kNoAnswer:
            return "not connected (no answer), trying again";
        case Reason::kLost:
            return "connection lost, reconnecting";
        case Reason::kOther:
            return "not connected (refused), trying again";
        case Reason::kNone:
        default:
            return "connecting";
    }
}

// --- Lights ---------------------------------------------------------------------------------

Rgb Net::rgb(uint32_t nowMs) const {
    const Rgb flash = (nowMs / kFlashMs) % 2 == 0 ? Rgb::kGreen : Rgb::kOff;
    if (join_ == JoinResult::kRunning) {
        return flash;
    }
    if (!s_.enabled) {
        return Rgb::kOff;
    }
    if (!s_.hasNetwork()) {
        return Rgb::kRed;
    }
    return up() ? Rgb::kGreen : flash;
}

Arq Net::arq() const {
    if (join_ == JoinResult::kRunning) {
        return Arq::kBlink;
    }
    if (!s_.enabled || !s_.hasNetwork()) {
        return Arq::kOff;
    }
    return up() ? Arq::kOn : Arq::kBlink;
}

// --- Changes --------------------------------------------------------------------------------

bool Net::apply(const Settings &next, bool cancel) {
    if ((cancel || !browserGuard_) && !save(store_, next))
        return false;
    if (cancel) {
        browserGuard_ = false;
        wipe(browserPrevious_.pass, sizeof browserPrevious_.pass);
        join_ = JoinResult::kNone;
        wipe(candidate_.pass, sizeof candidate_.pass);
    }
    s_ = next;
    if (cancel) {
        tz::resolve(s_.tz, &zone_, nullptr, 0);  // a dropped browser change may have set it
    }
    return true;
}

// The settings a command builds on: while a browser change is unverified, the previous ones.
// The unverified change is dropped, not saved.
Settings Net::baseline() {
    if (!browserGuard_) {
        return s_;
    }
    return browserPrevious_;
}

bool Net::setEnabled(bool on) {
    Settings next = baseline();
    next.enabled = on;
    bool ok = apply(next, true);
    wipe(next.pass, sizeof next.pass);
    if (!ok)
        return false;
    if (on && s_.hasNetwork()) {
        if (!up())
            attempt(now_);
    } else if (!on) {
        radio_.off();
        link_.state = LinkState::kOff;
        trying_ = wasUp_ = false;
    }
    return true;
}

bool Net::forget() {
    Settings next = baseline();
    wipe(next.ssid, sizeof next.ssid);
    wipe(next.pass, sizeof next.pass);
    if (!apply(next, true))
        return false;
    if (s_.enabled) {
        radio_.leave();
        link_.state = LinkState::kDown;
    }
    trying_ = wasUp_ = false;
    lastFail_ = Reason::kNone;
    return true;
}

void Net::reconnect() {
    if (s_.enabled && s_.hasNetwork())
        attempt(now_);
}

bool Net::setDhcp() {
    Settings next = s_;
    next.dhcp = true;
    bool ok = apply(next);
    wipe(next.pass, sizeof next.pass);
    if (ok)
        reconnect();
    return ok;
}

bool Net::setStatic(uint32_t ip, uint32_t mask, uint32_t gateway, uint32_t dns) {
    if (!validStatic(ip, mask, gateway))
        return false;
    Settings next = s_;
    next.dhcp = false;
    next.ip = ip;
    next.mask = mask;
    next.gateway = gateway;
    next.dns = dns != 0 ? dns : gateway;
    bool ok = apply(next);
    wipe(next.pass, sizeof next.pass);
    if (ok)
        reconnect();
    return ok;
}

bool Net::setHost(const char *name) {
    char host[kHostMax + 1];
    if (!normalizeHost(name, host, sizeof host))
        return false;
    Settings next = s_;
    copy(next.host, sizeof next.host, host);
    bool ok = apply(next);
    wipe(next.pass, sizeof next.pass);
    if (ok && up())
        radio_.startServices(s_);
    return ok;
}

bool Net::setNtp(const char *server) {
    if (!validServer(server))
        return false;
    Settings next = s_;
    copy(next.ntp, sizeof next.ntp, server);
    bool ok = apply(next);
    wipe(next.pass, sizeof next.pass);
    if (ok && up())
        radio_.startServices(s_);
    return ok;
}

bool Net::setTz(const char *zone) {
    tz::Rule rule;
    char name[kTzMax + 1];
    if (strlen(zone) > kTzMax || !tz::resolve(zone, &rule, name, sizeof name))
        return false;
    Settings next = s_;
    copy(next.tz, sizeof next.tz, name);
    bool ok = apply(next);
    wipe(next.pass, sizeof next.pass);
    if (ok)
        zone_ = rule;
    return ok;
}

// --- AT*WIFIJOIN ----------------------------------------------------------------------------

bool Net::join(const char *ssid, const char *pass, uint32_t nowMs, uint32_t timeoutMs) {
    if (join_ == JoinResult::kRunning || scan_ == ScanState::kRunning || !validSsid(ssid) ||
        !validPassword(pass)) {
        return false;
    }
    now_ = nowMs;
    candidate_ = s_;
    copy(candidate_.ssid, sizeof candidate_.ssid, ssid);
    wipe(candidate_.pass, sizeof candidate_.pass);
    copy(candidate_.pass, sizeof candidate_.pass, pass);
    candidate_.enabled = true;
    join_ = JoinResult::kRunning;
    joinReason_ = Reason::kNone;
    joinAt_ = nowMs;
    joinTimeout_ = timeoutMs;
    joinTryAt_ = nowMs;
    trying_ = false;
    wasUp_ = false;
    radio_.connect(candidate_);
    link_.state = LinkState::kConnecting;
    return true;
}

void Net::pollJoin(uint32_t nowMs) {
    if (link_.state == LinkState::kUp) {
        Settings previous = s_;
        s_ = candidate_;
        wipe(candidate_.pass, sizeof candidate_.pass);
        if (!(browserGuard_ ? save(store_, s_) : store())) {
            s_ = previous;
            wipe(previous.pass, sizeof previous.pass);
            join_ = JoinResult::kFailed;
            restoreAfterJoin();
            return;
        }
        wipe(previous.pass, sizeof previous.pass);
        if (browserGuard_) {
            browserGuard_ = false;
            wipe(browserPrevious_.pass, sizeof browserPrevious_.pass);
        }
        join_ = JoinResult::kJoined;
        wasUp_ = true;
        trying_ = false;
        failures_ = 0;
        lastFail_ = Reason::kNone;
        radio_.startServices(s_);
        return;
    }
    const bool failed = link_.state == LinkState::kDown || link_.state == LinkState::kOff;
    if (failed) {
        joinReason_ = link_.reason == Reason::kNone ? Reason::kOther : link_.reason;
        // A wrong password won't come right. Anything else may be a missed beacon: try
        // again while there is time.
        if (joinReason_ == Reason::kWrongPassword || due(nowMs, joinAt_ + joinTimeout_ / 2)) {
            join_ = JoinResult::kFailed;
            restoreAfterJoin();
        } else if (due(nowMs, joinTryAt_ + 2000)) {
            joinTryAt_ = nowMs;
            radio_.connect(candidate_);
        }
        return;
    }
    if (due(nowMs, joinAt_ + joinTimeout_)) {
        joinReason_ = Reason::kNoAnswer;
        join_ = JoinResult::kFailed;
        restoreAfterJoin();
    }
}

void Net::restoreAfterJoin() {
    if (browserGuard_) {
        s_ = browserPrevious_;
        browserGuard_ = false;
        wipe(browserPrevious_.pass, sizeof browserPrevious_.pass);
    }
    wipe(candidate_.pass, sizeof candidate_.pass);
    wasUp_ = false;
    if (s_.enabled && s_.hasNetwork()) {
        attempt(now_);
    } else if (!s_.enabled) {
        radio_.off();
        link_.state = LinkState::kOff;
    } else {
        radio_.leave();
        link_.state = LinkState::kDown;
    }
}

void Net::cancelJoin() {
    if (join_ == JoinResult::kRunning) {
        join_ = JoinResult::kNone;
        restoreAfterJoin();
    }
}

void Net::clearJoin() {
    if (join_ != JoinResult::kRunning) {
        join_ = JoinResult::kNone;
    }
}

// --- AT*WIFISCAN ----------------------------------------------------------------------------

bool Net::scanStart(uint32_t nowMs) {
    if (scan_ == ScanState::kRunning || join_ == JoinResult::kRunning) {
        return false;
    }
    now_ = nowMs;
    if (!up() && link_.state != LinkState::kOff) {
        // The radio can't scan while it is trying to join: pause the attempt.
        radio_.leave();
        trying_ = false;
        scanPaused_ = true;
    }
    if (!radio_.scanStart()) {
        if (scanPaused_) {
            scanPaused_ = false;
            retryDue_ = true;
            retryAt_ = nowMs;
        }
        scan_ = ScanState::kFailed;
        radioOffIfDisabled();
        return false;
    }
    scan_ = ScanState::kRunning;
    return true;
}

// A scan switches the radio on even with Wi-Fi off, and the radio still reports "off", so
// tick() would never switch it back (5.5-01).
void Net::radioOffIfDisabled() {
    if (!s_.enabled) {
        radio_.off();
    }
}

void Net::cancelScan() {
    if (scan_ == ScanState::kRunning) {
        scan_ = ScanState::kIdle;  // results that still arrive are dropped by the radio
        radioOffIfDisabled();
        if (scanPaused_) {
            scanPaused_ = false;
            retryDue_ = true;
            retryAt_ = now_;
        }
    }
}

void Net::finishScan(size_t n) {
    // Strongest first (insertion sort: a few dozen entries at most).
    for (size_t i = 1; i < n; ++i) {
        ScanEntry e = raw_[i];
        size_t j = i;
        while (j > 0 && raw_[j - 1].rssi < e.rssi) {
            raw_[j] = raw_[j - 1];
            --j;
        }
        raw_[j] = e;
    }
    scanCount_ = 0;
    for (size_t i = 0; i < n && scanCount_ < kScanMax; ++i) {
        if (raw_[i].ssid[0] == '\0') {
            continue;  // hidden network
        }
        bool seen = false;
        for (size_t k = 0; k < scanCount_; ++k) {
            if (strcmp(scanList_[k].ssid, raw_[i].ssid) == 0) {
                seen = true;  // another access point of the same network
            }
        }
        if (!seen) {
            scanList_[scanCount_++] = raw_[i];
        }
    }
}

}  // namespace netcfg
