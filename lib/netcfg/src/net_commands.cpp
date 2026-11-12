// AT* Wi-Fi and clock commands: see net_commands.h.
#include "net_commands.h"

#include <cstdio>
#include <cstring>

#include "hwtest.h"

namespace netcfg {

using atcmd::Port;
using atcmd::Reply;
using hwtest::argStart;
using hwtest::wordIs;

namespace {

Net *net = nullptr;
uint32_t (*clockMs)() = nullptr;
bool (*callUp)() = nullptr;

// The one join or scan waiting for its result (any port).
enum class Op : uint8_t { kNone, kPassword, kJoin, kScan };
Op pendingOp = Op::kNone;
Port *pendingPort = nullptr;
char joinSsid[kSsidMax + 1] = {};
uint32_t passwordAt = 0;
Port *confirmationPort = nullptr;
uint32_t confirmationAt = 0;
bool confirmationForget = false;

uint32_t now() {
    return clockMs != nullptr ? clockMs() : 0;
}
// A join, scan or address change would drop a call in progress without a word (5.5-05).
bool refusedDuringCall(Port &port) {
    if (callUp == nullptr || !callUp()) {
        return false;
    }
    port.beginInfo();
    port.print("A call is in progress; hang up first");
    port.newline();
    return true;
}
void releasePassword() {
    if (pendingOp != Op::kPassword || !pendingPort)
        return;
    if (pendingPort->busy() && now() - passwordAt < 120000)
        return;
    pendingPort->cancelPrompt();
    pendingPort = nullptr;
    pendingOp = Op::kNone;
    wipe(joinSsid, sizeof joinSsid);
}
Reply confirmNetworkLoss(Port &port, const char *text, void *) {
    bool valid = confirmationPort == &port && now() - confirmationAt < 30000 && wordIs(text, "Y");
    confirmationPort = nullptr;
    if (!valid) {
        return Reply::kError;
    }
    return (confirmationForget ? net->forget() : net->setEnabled(false)) ? Reply::kOk
                                                                         : Reply::kError;
}
Reply askNetworkLoss(Port &port, bool forget) {
    if (confirmationPort && confirmationPort->busy()) {
        return Reply::kError;
    }
    confirmationPort = &port;
    confirmationAt = now();
    confirmationForget = forget;
    port.askConfirmation(forget ? "Forget network and disconnect browser? Y/N (Enter): "
                                : "Turn Wi-Fi off and disconnect browser? Y/N (Enter): ",
                         confirmNetworkLoss, nullptr);
    return Reply::kPending;
}

// The first `n` characters of `s` as the network to join. False if too long.
bool copySsid(const char *s, size_t n) {
    if (n > kSsidMax) {
        return false;
    }
    memcpy(joinSsid, s, n);
    joinSsid[n] = '\0';
    return true;
}

void cancelPending(void *) {
    if (pendingOp == Op::kJoin) {
        net->cancelJoin();
        net->clearJoin();
    } else if (pendingOp == Op::kScan) {
        net->cancelScan();
    }
    pendingOp = Op::kNone;
    pendingPort = nullptr;
}

Reply startWait(Port &port, Op op) {
    pendingOp = op;
    pendingPort = &port;
    port.wait(cancelPending, nullptr);
    return Reply::kPending;
}

// "AT*X" and "AT*X?" show; "AT*X=?" lists choices; anything else sets.
bool isShow(const char *args) {
    const char *a = args;
    while (*a == ' ') {
        ++a;
    }
    return *a == '\0' || (a[0] == '?' && a[1] == '\0');
}

bool isList(const char *args) {
    const char *a = argStart(args);
    return args != a && a[0] == '?' && a[1] == '\0';
}

// The argument after '=' with surrounding spaces removed.
void argText(const char *args, char *out, size_t cap) {
    const char *a = argStart(args);
    size_t n = strlen(a);
    while (n > 0 && a[n - 1] == ' ') {
        --n;
    }
    if (n >= cap) {
        n = cap - 1;
    }
    memcpy(out, a, n);
    out[n] = '\0';
}

void line(Port &port, const char *label, const char *text) {
    port.printf("%-9s %s", label, text);
    port.newline();
}

void showStatus(Port &port) {
    const Settings &s = net->settings();
    const LinkInfo &l = net->link();
    char buf[64];
    port.beginInfo();
    line(port, "Wi-Fi:", net->stateText());
    line(port, "Network:", s.hasNetwork() ? s.ssid : "(none)");
    line(port, "Password:", s.pass[0] != '\0' ? "set" : "not set");
    if (net->up()) {
        snprintf(buf, sizeof buf, "%d dBm, channel %u", static_cast<int>(l.rssi),
                 static_cast<unsigned>(l.channel));
        line(port, "Signal:", buf);
    }
    const bool up = net->up();
    const uint32_t ip = up ? l.ip : s.ip;
    if (up || !s.dhcp) {
        char a[16];
        formatIp(ip, a, sizeof a);
        snprintf(buf, sizeof buf, "%s (%s)", a, s.dhcp ? "DHCP" : "static");
        line(port, "Address:", buf);
        formatIp(up ? l.mask : s.mask, a, sizeof a);
        line(port, "Mask:", a);
        formatIp(up ? l.gateway : s.gateway, a, sizeof a);
        line(port, "Gateway:", a);
        formatIp(up ? l.dns : s.dns, a, sizeof a);
        line(port, "DNS:", a);
    } else {
        line(port, "Address:", "from DHCP");
    }
    uint8_t mac[6];
    net->radio().mac(mac);
    snprintf(buf, sizeof buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3],
             mac[4], mac[5]);
    line(port, "MAC:", buf);
    snprintf(buf, sizeof buf, "%s.local", s.host);
    line(port, "Name:", buf);
}

void showScan(Port &port) {
    port.beginInfo();
    if (net->scanCount() == 0) {
        port.print("No networks found");
        port.newline();
        return;
    }
    for (size_t i = 0; i < net->scanCount(); ++i) {
        const ScanEntry &e = net->scanEntry(i);
        // A neighbour chooses this name: no control bytes may reach the terminal.
        char name[sizeof e.ssid];
        size_t n = 0;
        for (; n + 1 < sizeof name && e.ssid[n] != '\0'; ++n) {
            const unsigned char c = static_cast<unsigned char>(e.ssid[n]);
            name[n] = (c < 0x20 || c == 0x7F) ? '?' : static_cast<char>(c);
        }
        name[n] = '\0';
        port.printf("%2u  %-32s %4d dBm  ch %2u  %s", static_cast<unsigned>(i + 1), name,
                    static_cast<int>(e.rssi), static_cast<unsigned>(e.channel),
                    securityText(e.security));
        port.newline();
    }
}

Reply beginJoin(Port &port, const char *pass) {
    port.beginInfo();
    port.printf("Joining %s...", joinSsid);
    port.newline();
    if (port.browser()) {
        net->guardBrowserChange(now());
    }
    bool joined = net->join(joinSsid, pass, now(), port.browser() ? 60000 : Net::kJoinTimeoutMs);
    wipe(joinSsid, sizeof joinSsid);
    if (!joined) {
        return Reply::kError;
    }
    return startWait(port, Op::kJoin);
}

Reply onPassword(Port &port, const char *text, void *) {
    if (pendingOp != Op::kPassword || pendingPort != &port)
        return Reply::kError;
    pendingOp = Op::kNone;
    pendingPort = nullptr;
    if (now() - passwordAt >= 120000 || !validPassword(text)) {
        wipe(joinSsid, sizeof joinSsid);
        port.beginInfo();
        port.print("The password must be 8-63 characters (or 64 hex digits)");
        port.newline();
        return Reply::kError;
    }
    return beginJoin(port, text);
}

}  // namespace

void attachCommands(Net &n, uint32_t (*millisFn)(), bool (*callUpFn)()) {
    net = &n;
    clockMs = millisFn;
    callUp = callUpFn;
    pendingOp = Op::kNone;
    pendingPort = nullptr;
    confirmationPort = nullptr;
}

void pollCommands() {
    releasePassword();
    if (pendingOp == Op::kPassword)
        return;
    if (confirmationPort) {
        if (!confirmationPort->busy()) {
            confirmationPort = nullptr;
        } else if (now() - confirmationAt >= 30000) {
            confirmationPort->cancelPrompt();
            confirmationPort = nullptr;
        }
    }
    if (pendingOp == Op::kNone || pendingPort == nullptr) {
        return;
    }
    Port &port = *pendingPort;
    if (pendingOp == Op::kScan) {
        const ScanState st = net->scanState();
        if (st == ScanState::kRunning) {
            return;
        }
        pendingOp = Op::kNone;
        pendingPort = nullptr;
        if (st == ScanState::kDone) {
            showScan(port);
            port.finish(Reply::kOk);
        } else {
            port.beginInfo();
            port.print("Scan failed, try again");
            port.newline();
            port.finish(Reply::kError);
        }
        return;
    }
    const JoinResult r = net->joinResult();
    if (r == JoinResult::kRunning) {
        return;
    }
    pendingOp = Op::kNone;
    pendingPort = nullptr;
    port.beginInfo();
    if (r == JoinResult::kJoined) {
        char ip[16];
        formatIp(net->link().ip, ip, sizeof ip);
        port.printf("Connected, IP %s", ip);
        port.newline();
        net->clearJoin();
        port.finish(Reply::kOk);
    } else {
        port.printf("Not connected: %s. Settings not changed", reasonText(net->joinReason()));
        port.newline();
        net->clearJoin();
        port.finish(Reply::kError);
    }
}

void detachCommands(Port &port) {
    if (pendingPort == &port && pendingOp == Op::kPassword) {
        port.cancelPrompt();
        releasePassword();
    }
    if (pendingPort == &port && pendingOp == Op::kJoin) {
        // Browser network replacement must finish or roll back after its old
        // socket disappears. The candidate is owned by Net, not by this Port.
        pendingPort = nullptr;
        pendingOp = Op::kNone;
        // Nobody will collect a finished result: a stale failure would undo the
        // browser's next change.
        net->clearJoin();
    }
}

// --- Commands -------------------------------------------------------------------------------

Reply cmdWifi(Port &port, const char *args) {
    if (isShow(args)) {
        showStatus(port);
        return Reply::kOk;
    }
    const char *a = argStart(args);
    if (wordIs(a, "ON")) {
        return net->setEnabled(true) ? Reply::kOk : Reply::kError;
    }
    if (wordIs(a, "OFF")) {
        if (port.browser()) {
            return askNetworkLoss(port, false);
        }
        return net->setEnabled(false) ? Reply::kOk : Reply::kError;
    }
    return Reply::kError;
}

Reply cmdWifiScan(Port &port, const char *args) {
    releasePassword();
    if (*argStart(args) != '\0' || pendingOp != Op::kNone || refusedDuringCall(port)) {
        return Reply::kError;
    }
    if (!net->scanStart(now())) {
        return Reply::kError;
    }
    port.beginInfo();
    port.print("Scanning...");
    port.newline();
    return startWait(port, Op::kScan);
}

Reply cmdWifiJoin(Port &port, const char *args) {
    releasePassword();
    if (pendingOp != Op::kNone || refusedDuringCall(port)) {
        return Reply::kError;
    }
    char arg[atcmd::kLineMax + 1];
    argText(args, arg, sizeof arg);
    bool open = false;
    const size_t n = strlen(arg);
    if (n >= 2 && arg[0] == '"' && arg[n - 1] == '"') {
        if (!copySsid(arg + 1, n - 2)) {
            return Reply::kError;
        }
    } else {
        uint32_t index = 0;
        const bool number = n > 0 && strspn(arg, "0123456789") == n;
        if (number && hwtest::parseUint(arg, 1, 99, &index)) {
            if (index > net->scanCount()) {
                port.beginInfo();
                port.print(net->scanCount() == 0 ? "Run AT*WIFISCAN first"
                                                 : "No such network in the AT*WIFISCAN list");
                port.newline();
                return Reply::kError;
            }
            const ScanEntry &e = net->scanEntry(index - 1);
            snprintf(joinSsid, sizeof joinSsid, "%s", e.ssid);
            open = e.security == Security::kOpen;
        } else if (number) {
            return Reply::kError;  // 0 or over 99; quote a network named with digits
        } else if (!copySsid(arg, n)) {
            return Reply::kError;
        }
    }
    if (!validSsid(joinSsid)) {
        return Reply::kError;
    }
    if (open) {
        return beginJoin(port, "");
    }
    port.beginInfo();
    pendingOp = Op::kPassword;
    pendingPort = &port;
    passwordAt = now();
    port.askSecret("Password (Enter for none): ", onPassword, nullptr);
    return Reply::kPending;
}

Reply cmdWifiForget(Port &port, const char *args) {
    if (*argStart(args) != '\0') {
        return Reply::kError;
    }
    if (port.browser()) {
        return askNetworkLoss(port, true);
    }
    return net->forget() ? Reply::kOk : Reply::kError;
}

Reply cmdWifiIp(Port &port, const char *args) {
    const Settings &s = net->settings();
    if (isShow(args)) {
        port.beginInfo();
        if (s.dhcp) {
            port.print("DHCP");
        } else {
            char ip[16], mask[16], gw[16], dns[16];
            formatIp(s.ip, ip, sizeof ip);
            formatIp(s.mask, mask, sizeof mask);
            formatIp(s.gateway, gw, sizeof gw);
            formatIp(s.dns, dns, sizeof dns);
            port.printf("Static %s, mask %s, gateway %s, DNS %s", ip, mask, gw, dns);
        }
        port.newline();
        return Reply::kOk;
    }
    const char *a = argStart(args);
    if (refusedDuringCall(port)) {
        return Reply::kError;
    }
    if (wordIs(a, "DHCP")) {
        if (port.browser()) {
            net->guardBrowserChange(now());
        }
        return net->setDhcp() ? Reply::kOk : Reply::kError;
    }
    uint32_t ip = 0, mask = 0, gw = 0, dns = 0;
    if (!parseIp(a, &ip, &a) || !parseIp(a, &mask, &a) || !parseIp(a, &gw, &a)) {
        return Reply::kError;
    }
    if (*a != '\0' && (!parseIp(a, &dns, &a) || *a != '\0')) {
        return Reply::kError;
    }
    if (port.browser()) {
        net->guardBrowserChange(now());
    }
    if (!net->setStatic(ip, mask, gw, dns)) {
        port.beginInfo();
        port.print("Not a usable address, mask and gateway");
        port.newline();
        return Reply::kError;
    }
    return Reply::kOk;
}

Reply cmdHostname(Port &port, const char *args) {
    if (isShow(args)) {
        port.beginInfo();
        port.printf("%s (%s.local)", net->settings().host, net->settings().host);
        port.newline();
        return Reply::kOk;
    }
    char name[atcmd::kLineMax + 1];
    argText(args, name, sizeof name);
    if (port.browser()) {
        net->guardBrowserChange(now());
    }
    return net->setHost(name) ? Reply::kOk : Reply::kError;
}

Reply cmdTime(Port &port, const char *args) {
    if (!isShow(args)) {
        return Reply::kError;
    }
    const int64_t utc = net->utcNow();
    port.beginInfo();
    if (utc == 0) {
        port.print("Clock not set (it is set by NTP once Wi-Fi is connected)");
        port.newline();
    } else {
        char t[48], off[8];
        tz::formatLocal(net->zone(), utc, t, sizeof t);
        tz::formatOffset(tz::offsetAt(net->zone(), utc), off, sizeof off);
        port.printf("%s (UTC%s)", t, off);
        port.newline();
    }
    line(port, "Zone:", net->settings().tz);
    const int64_t sync = net->radio().lastSync();
    char last[48];
    if (sync != 0) {
        tz::formatLocal(net->zone(), sync, last, sizeof last);
    } else {
        snprintf(last, sizeof last, "none yet");
    }
    port.printf("%-9s %s, last update %s", "NTP:", net->settings().ntp, last);
    port.newline();
    return Reply::kOk;
}

Reply cmdTz(Port &port, const char *args) {
    if (isList(args)) {
        port.beginInfo();
        for (size_t i = 0; i < tz::zoneCount(); ++i) {
            port.printf("  %-20s %s", tz::zoneName(i), tz::zoneRule(i));
            port.newline();
        }
        port.print("  or a POSIX rule, e.g. AT*TZ=NZST-12NZDT,M9.5.0,M4.1.0/3");
        port.newline();
        return Reply::kOk;
    }
    if (isShow(args)) {
        port.beginInfo();
        const char *zone = net->settings().tz;
        for (size_t i = 0; i < tz::zoneCount(); ++i) {
            if (strcmp(zone, tz::zoneName(i)) == 0) {
                port.printf("%s (%s)", zone, tz::zoneRule(i));
                port.newline();
                return Reply::kOk;
            }
        }
        port.print(zone);
        port.newline();
        return Reply::kOk;
    }
    char zone[atcmd::kLineMax + 1];
    argText(args, zone, sizeof zone);
    return net->setTz(zone) ? Reply::kOk : Reply::kError;
}

Reply cmdNtp(Port &port, const char *args) {
    if (isShow(args)) {
        port.beginInfo();
        port.print(net->settings().ntp);
        port.newline();
        return Reply::kOk;
    }
    char server[atcmd::kLineMax + 1];
    argText(args, server, sizeof server);
    return net->setNtp(server) ? Reply::kOk : Reply::kError;
}

}  // namespace netcfg
