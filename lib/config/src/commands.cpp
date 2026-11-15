#include <cctype>
#include <cstdio>
#include <cstring>
#include "config.h"
#include "credits_generated.h"

#ifndef FW_VERSION
#define FW_VERSION "test"
#endif
#ifndef FW_COMMIT
#define FW_COMMIT "test"
#endif
namespace config {
bool Commands::value(const char *id, char *out, size_t cap) const {
    const auto &s = net_.settings();
    const char *value = nullptr;
    if (!strcmp(id, "wifi-clock.wifi-toggle"))
        value = s.enabled ? "ON" : "OFF";
    else if (!strcmp(id, "wifi-clock.join"))
        value = *s.ssid ? s.ssid : "not set";
    else if (!strcmp(id, "wifi-clock.network-status"))
        value = net_.stateText();
    else if (!strcmp(id, "wifi-clock.network-name"))
        value = s.host;
    else if (!strcmp(id, "wifi-clock.ntp-server"))
        value = s.ntp;
    else if (!strcmp(id, "wifi-clock.time-zone"))
        value = s.tz;
    else if (!strcmp(id, "wifi-clock.ip-configuration")) {
        if (s.dhcp)
            value = "DHCP";
        else {
            char ip[16], mask[16], gateway[16], dns[16];
            netcfg::formatIp(s.ip, ip, sizeof ip);
            netcfg::formatIp(s.mask, mask, sizeof mask);
            netcfg::formatIp(s.gateway, gateway, sizeof gateway);
            netcfg::formatIp(s.dns, dns, sizeof dns);
            snprintf(out, cap, "%s, %s, %s, %s", ip, mask, gateway, dns);
            return true;
        }
    } else if (!strcmp(id, "updates-reset.ota-policy-window"))
        value = device_.policy() == devicecfg::Policy::kAlways ? "ALWAYS" : "DEMAND";
    else if (!strcmp(id, "debug.verbosity")) {
        snprintf(out, cap, "%u", log_.level);
        return true;
    }
    if (!value)
        return false;
    snprintf(out, cap, "%s", value);
    return true;
}
namespace {
Commands *attached = nullptr;
bool query(const char *s) {
    return !*s || !strcmp(s, "?");
}
bool digits(const char *&s) {  // consumes one or more digits
    if (!isdigit(static_cast<unsigned char>(*s)))
        return false;
    while (isdigit(static_cast<unsigned char>(*s)))
        ++s;
    return true;
}
// True for exactly one setting command: Xn, Xn?, &Xn, Sr=n, Sr?, Sr.b=n or &Zn=host (X a
// letter, n digits). Anything after it, such as the D of "E0DT555", makes it false.
bool oneSetting(const char *s) {
    const bool amp = *s == '&';
    const char letter = s[amp ? 1 : 0];
    if (!isupper(static_cast<unsigned char>(letter)))
        return false;
    s += amp ? 2 : 1;
    if (!amp && letter == 'S') {
        if (!digits(s))
            return false;
        if (!*s || !strcmp(s, "?"))
            return true;
        if (*s == '.' && (++s, !digits(s) || *s != '='))
            return false;
        return *s == '=' && (++s, digits(s)) && !*s;
    }
    if (amp && letter == 'Z' && isdigit(static_cast<unsigned char>(*s))) {
        digits(s);
        if (*s != '=')
            return !*s || !strcmp(s, "?");
        for (++s; *s; ++s)
            if (!(isalnum(static_cast<unsigned char>(*s)) || strchr(".:-_/,", *s)))
                return false;
        return true;
    }
    while (isdigit(static_cast<unsigned char>(*s)))
        ++s;
    return !*s || !strcmp(s, "?");
}
void profileExport(atcmd::Port &p, const modemcfg::Profile &a) {
    // Keep response formatting stable until the final restore commands.
    for (unsigned i = 0; i < modemcfg::kSettingCount; ++i) {
        if (i == modemcfg::kQ || i == modemcfg::kV || i == modemcfg::kTone) {
            continue;
        }
        p.printf("AT*SETTING=%s%u\r\n",
                 modemcfg::settingInfo(static_cast<modemcfg::Setting>(i)).name, a.v[i]);
    }
    p.printf("AT*SETTING=%c\r\n", a.v[modemcfg::kTone] ? 'T' : 'P');
    for (unsigned i = 0; i < modemcfg::kSRegCount; ++i) {
        if (i == 1 || i == 3 || i == 4) {
            continue;
        }
        p.printf("AT*SETTING=S%u=%u\r\n", i, a.s[i]);
    }
    p.printf("AT*EXPLICIT=%u\r\n", a.lineExplicit);
}
bool safe(const char *s) {
    for (; *s; ++s) {
        if (static_cast<unsigned char>(*s) < 32 || *s > 126) {
            return false;
        }
    }
    return true;
}
}  // namespace
void Commands::attach(Commands &commands) {
    attached = &commands;
}
atcmd::Reply Commands::dispatch(atcmd::Port &p, const char *name, const char *args) {
    return attached ? attached->command(p, name, args) : atcmd::Reply::kError;
}
// The commands lib/config answers itself; the rest go to the firmware's Hardware.
atcmd::Reply Commands::command(atcmd::Port &p, const char *name, const char *args) {
    using Handler = atcmd::Reply (Commands::*)(atcmd::Port &, const char *);
    static const struct {
        const char *name;
        Handler handler;
    } kTable[] = {
        {"SETTING", &Commands::setting},
        {"EXPLICIT", &Commands::explicitRate},
        {"RESTOREEND", &Commands::restoreEnd},
        {"PROFILE", &Commands::profile},
        {"PROFILECTL", &Commands::profileControl},
        {"BOOKCLEAR", &Commands::bookClear},
        {"DIAL", &Commands::dial},
        {"REFERENCE", &Commands::reference},
        {"ABOUT", &Commands::about},
        {"EXPORT", &Commands::exportCommand},
        {"LOG", &Commands::logCommand},
        {"CALLLOG", &Commands::callLog},
    };
    for (const auto &entry : kTable) {
        if (!strcmp(name, entry.name)) {
            return (this->*entry.handler)(p, args);
        }
    }
    return hardware_.command(p, name, args);
}
atcmd::Reply Commands::setting(atcmd::Port &p, const char *args) {
    using atcmd::Reply;
    if (query(args)) {
        atcmd::showCurrent(p);
        atcmd::showStored(p);
        return Reply::kOk;
    }
    if (*args++ != '=' || !*args || strlen(args) > atcmd::kLineMax) {
        return Reply::kError;
    }
    // Only modem/profile commands are accepted by this wrapper. Network,
    // password, call and destructive handlers retain their own entry points.
    const char *allowed = "BEFLMQVXPTSYZ&";
    if (!strchr(allowed, *args) || (*args == '&' && !strchr("ABCDGHIKMNPRSTUYWZF", args[1]))) {
        return Reply::kError;
    }
    if (!oneSetting(args)) {  // one command only: "E0DT555" must not dial
        return Reply::kError;
    }
    if (!strcmp(args, "T0") || !strcmp(args, "T1")) {
        p.execute(args[1] == '1' ? "T" : "P");
    } else {
        p.execute(args);
    }
    return Reply::kDone;
}
atcmd::Reply Commands::explicitRate(atcmd::Port &p, const char *args) {
    if (!strcmp(args, "=0") || !strcmp(args, "=1")) {
        p.modem().active().lineExplicit = args[1] == '1';
        return atcmd::Reply::kOk;
    }
    return atcmd::Reply::kError;
}
atcmd::Reply Commands::restoreEnd(atcmd::Port &p, const char *args) {
    unsigned baud, q, v, cr, lf;
    char tail;
    if (sscanf(args, "=%u,%u,%u,%u,%u%c", &baud, &q, &v, &cr, &lf, &tail) != 5 ||
        (baud && !modemcfg::isHostRate(baud)) || q > 2 || v > 1 || cr > 255 || lf > 255 ||
        !p.modem().setHostBaud(baud)) {
        return atcmd::Reply::kError;
    }
    p.result(atcmd::Result::kOk);  // acknowledge at the old framing and speed
    p.modem().active().v[modemcfg::kQ] = q;
    p.modem().active().v[modemcfg::kV] = v;
    p.modem().active().s[3] = cr;
    p.modem().active().s[4] = lf;
    hardware_.command(p, "APPLYBAUD", "");
    return atcmd::Reply::kDone;
}
atcmd::Reply Commands::profile(atcmd::Port &p, const char *args) {
    unsigned slot = 0, baud = 0;
    char tail = 0;
    if (sscanf(args, "=%u,%u%c", &slot, &baud, &tail) != 2 || slot > 1 ||
        !modemcfg::isHostRate(baud)) {
        return atcmd::Reply::kError;
    }
    return p.modem().writeTemplate(slot, baud) ? atcmd::Reply::kOk : atcmd::Reply::kError;
}
atcmd::Reply Commands::profileControl(atcmd::Port &p, const char *args) {
    unsigned t, cr, lf, quiet, verbal;
    char tail;
    if (sscanf(args, "=%u,%u,%u,%u,%u%c", &t, &cr, &lf, &quiet, &verbal, &tail) != 5 || t > 1 ||
        cr > 255 || lf > 255 || quiet > 2 || verbal > 1) {
        return atcmd::Reply::kError;
    }
    auto active = p.modem().active();
    p.modem().active() = p.modem().nvram().tmpl[t];
    auto &a = p.modem().active();
    a.s[3] = cr;
    a.s[4] = lf;
    a.v[modemcfg::kQ] = quiet;
    a.v[modemcfg::kV] = verbal;
    bool ok = p.modem().writeTemplate(t, a.baud);
    p.modem().active() = active;
    return ok ? atcmd::Reply::kOk : atcmd::Reply::kError;
}
atcmd::Reply Commands::bookClear(atcmd::Port &, const char *args) {
    return !*args && book_.clear() ? atcmd::Reply::kOk : atcmd::Reply::kError;
}
atcmd::Reply Commands::dial(atcmd::Port &p, const char *args) {
    if (*args != '=' || !args[1] || !p.link()) {
        return atcmd::Reply::kError;
    }
    return p.link()->dial(args + 1, false) ? atcmd::Reply::kDone : atcmd::Reply::kError;
}
atcmd::Reply Commands::reference(atcmd::Port &p, const char *args) {
    if (!query(args)) {
        return atcmd::Reply::kError;
    }
    p.beginInfo();
    p.line("AT / A/ repeat; D/DT/DP dial; DS0-3 stored dial; ATA answer; ATH hang up");
    p.line("+++ guarded escape; ATO resume; ATI0-7,9 inquiry; +FCLASS=0 modem");
    p.line("B E F L M Q V X; P/T dial mode; S0-S63=value or ? (S1 live ring count)");
    p.line("&F0-2 factory active; &W0-1 save; Y0-4 power-on; Z0-5 reset; &V profiles");
    p.line("&Z0-3=number saved dial; &A &B &C &D &G &H &I &K &M &N &P &R &S &T &U &Y");
    atcmd::showStarHelp(p);
    return atcmd::Reply::kOk;
}
atcmd::Reply Commands::about(atcmd::Port &p, const char *args) {
    if (!query(args)) {
        return atcmd::Reply::kError;
    }
    p.beginInfo();
    p.printf("USR Modem Emulator v%s (%s)\r\n", FW_VERSION, FW_COMMIT);
    p.line("USRobotics command behavior; replica project by Jon Edney.");
    for (const char *credit : kCredits) {
        p.line(credit);
    }
    return atcmd::Reply::kOk;
}
atcmd::Reply Commands::exportCommand(atcmd::Port &p, const char *args) {
    return query(args) ? exportConfig(p) : atcmd::Reply::kError;
}
atcmd::Reply Commands::logCommand(atcmd::Port &p, const char *args) {
    if (!strcmp(args, "=CLEAR")) {
        log_.clear();
        return atcmd::Reply::kOk;
    }
    if (args[0] == '=' && args[1] >= '0' && args[1] <= '2' && !args[2]) {
        log_.level = args[1] - '0';
        return atcmd::Reply::kOk;
    }
    unsigned offset = log_.size() > 8192 ? log_.size() - 8192 : 0, count = 8192;
    if (!query(args)) {
        char tail;
        if (sscanf(args, "=%u,%u%c", &offset, &count, &tail) != 2 || count > 8192 ||
            offset > log_.size()) {
            return atcmd::Reply::kError;
        }
    }
    p.beginInfo();
    p.printf("Log level %u, capacity %u bytes%s\r\n", log_.level, unsigned(log_.capacity()),
             log_.capacity() < 256 * 1024 ? " (reduced capacity)" : " PSRAM");
    p.printf("Bytes %u-%u of %u; AT*LOG=offset,count reads up to 8192 bytes.\r\n", offset,
             unsigned(offset + count < log_.size() ? offset + count : log_.size()),
             unsigned(log_.size()));
    for (size_t i = offset; i < log_.size() && i < size_t(offset) + count; ++i) {
        char c = log_.at(i);
        p.write(&c, 1);
    }
    return atcmd::Reply::kOk;
}
atcmd::Reply Commands::callLog(atcmd::Port &p, const char *args) {
    if (!query(args)) {
        return atcmd::Reply::kError;
    }
    p.beginInfo();
    p.printf("Call history: %u/50 (configured time zone, result, seconds, TX/RX payload)\r\n",
             unsigned(history_.size()));
    for (size_t i = 0; i < history_.size(); ++i) {
        const auto &r = history_.at(i);
        char stamp[48] = "time unavailable";
        if (r.utc > 0) {
            netcfg::tz::formatLocal(net_.zone(), r.utc, stamp, sizeof stamp);
        }
        p.printf("%s %s %s %s result=%u %lus TX=%lu RX=%lu\r\n", stamp, r.incoming ? "IN" : "OUT",
                 r.number, r.host, r.result, static_cast<unsigned long>(r.duration),
                 static_cast<unsigned long>(r.sent), static_cast<unsigned long>(r.received));
    }
    return atcmd::Reply::kOk;
}
atcmd::Reply Commands::exportConfig(atcmd::Port &p) {
    const auto &m = p.modem();
    // This is deliberately an explicit allowlist. No password storage or generic
    // NVS/debug dump is consulted. Network credentials must be entered separately.
    p.beginInfo();
    p.print("ATQ0V1S3=13S4=10\r\n");
    for (unsigned t = 0; t < 2; ++t) {
        profileExport(p, m.nvram().tmpl[t]);
        p.printf("AT*PROFILE=%u,%lu\r\n", t, static_cast<unsigned long>(m.nvram().tmpl[t].baud));
    }
    for (unsigned i = 0; i < modemcfg::kPhoneSlots; ++i) {
        if (safe(m.nvram().phone[i])) {
            p.printf("AT&Z%u=%s\r\n", i, m.nvram().phone[i]);
        } else {
            p.print("; Stored number omitted: unsupported control characters.\r\n");
        }
    }
    // Delete existing phonebook contents before restoring the source entries.
    // Only source numbers are known: restore onto factory/empty configuration.
    p.print("AT*BOOKCLEAR\r\n");
    for (size_t i = 0; i < calls::kPhonebookCount; ++i) {
        const auto &e = book_.at(i);
        if (*e.number && safe(e.host)) {
            p.printf("AT*PHONEBOOK=%s,%s,%u,%s,%lu\r\n", e.number, e.host, e.port,
                     e.raw ? "RAW" : "TELNET", static_cast<unsigned long>(e.speed));
        }
    }
    p.printf("AT*MODEL=%lu\r\nAT*THROTTLE=%s\r\nAT*ANSWERPORT=%s\r\n",
             static_cast<unsigned long>(m.model()), m.throttle() ? "ON" : "OFF",
             modemcfg::answerPortName(m.answerPort()));
    p.printf("AT*RINGDELAY=%lu\r\nAT*CONNECTDELAY=%lu\r\n",
             static_cast<unsigned long>(m.ringDelayMs()),
             static_cast<unsigned long>(m.connectDelayMs()));
    const auto &s = net_.settings();
    p.printf("AT*HOSTNAME=%s\r\nAT*NTP=%s\r\nAT*TZ=%s\r\n", s.host, s.ntp, s.tz);
    if (s.dhcp) {
        p.print("AT*WIFIIP=DHCP\r\n");
    } else {
        char ip[16], mask[16], gw[16], dns[16];
        netcfg::formatIp(s.ip, ip, sizeof ip);
        netcfg::formatIp(s.mask, mask, sizeof mask);
        netcfg::formatIp(s.gateway, gw, sizeof gw);
        netcfg::formatIp(s.dns, dns, sizeof dns);
        p.printf("AT*WIFIIP=%s,%s,%s,%s\r\n", ip, mask, gw, dns);
    }
    p.printf("AT*WIFI=%s\r\nAT*OTA=%s\r\nATY%u\r\n", s.enabled ? "ON" : "OFF",
             device_.policy() == devicecfg::Policy::kAlways ? "ALWAYS" : "DEMAND", m.nvram().y);
    hardware_.command(p, "WEBEXPORT", "");
    hardware_.command(p, "NETCONSOLEEXPORT", "");
    hardware_.command(p, "CLIENTEXPORT", "");
    hardware_.command(p, "INTERNETEXPORT", "");
    profileExport(p, m.active());
    // Control bytes/quiet settings are last so the whole script remains executable.
    for (unsigned t = 0; t < 2; ++t) {
        const auto &a = m.nvram().tmpl[t];
        p.printf("AT*PROFILECTL=%u,%u,%u,%u,%u\r\n", t, a.s[3], a.s[4], a.v[modemcfg::kQ],
                 a.v[modemcfg::kV]);
    }
    p.printf("AT*RESTOREEND=%lu,%u,%u,%u,%u\r\n", static_cast<unsigned long>(m.hostBaud()),
             m.active().v[modemcfg::kQ], m.active().v[modemcfg::kV], m.active().s[3],
             m.active().s[4]);
    return atcmd::Reply::kOk;
}
}  // namespace config
