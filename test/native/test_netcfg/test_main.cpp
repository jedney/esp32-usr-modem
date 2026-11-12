// Unit tests for Wi-Fi and clock (lib/netcfg, brief 5.4, 6.6, 6.7): saved settings and
// their checks, time zones, the connect/retry rules, the Wi-Fi lights, AT*WIFIJOIN and
// AT*WIFISCAN, and the AT* commands with a fake radio. The canary test checks that the
// Wi-Fi password never appears in any output.
// Run with: pio test -e native
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "atcmd.h"
#include "net_commands.h"
#include "net_settings.h"
#include "net_tz.h"
#include "netcfg.h"

using namespace netcfg;

void setUp() {}
void tearDown() {}

// --- Test rig ---------------------------------------------------------------------------

class MemStore : public modemcfg::Store {
public:
    size_t load(uint8_t *buf, size_t cap) override {
        size_t n = len < cap ? len : cap;
        memcpy(buf, data, n);
        return n;
    }
    bool save(const uint8_t *d, size_t n) override {
        if (fail) {
            return false;
        }
        memcpy(data, d, n);
        len = n;
        ++saves;
        return true;
    }
    bool erase() override {
        len = 0;
        return true;
    }
    uint8_t data[1024] = {};
    size_t len = 0;
    int saves = 0;
    bool fail = false;
};

class FakeRadio : public Radio {
public:
    void connect(const Settings &s) override {
        last = s;
        ++connects;
        info.state = LinkState::kConnecting;
        info.reason = Reason::kNone;
    }
    void leave() override {
        ++leaves;
        info.state = LinkState::kDown;
        info.reason = Reason::kNone;
    }
    void off() override {
        ++offs;
        info.state = LinkState::kOff;
    }
    LinkInfo link() override { return info; }
    bool scanStart() override {
        ++scans;
        scanDone = false;
        return scanOk;
    }
    int scanResult(ScanEntry *out, int max) override {
        if (!scanDone) {
            return -1;
        }
        if (scanFail) {
            return -2;
        }
        int n = 0;
        for (const ScanEntry &e : found) {
            if (n < max) {
                out[n++] = e;
            }
        }
        return n;
    }
    void startServices(const Settings &s) override {
        ++services;
        host = s.host;
        ntp = s.ntp;
    }
    int64_t utcNow() override { return utc; }
    int64_t lastSync() override { return sync; }
    void mac(uint8_t out[6]) override {
        const uint8_t m[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03};
        memcpy(out, m, 6);
    }

    void up(uint32_t ip = 0xC0A8012A) {  // 192.168.1.42
        info.state = LinkState::kUp;
        info.reason = Reason::kNone;
        info.rssi = -54;
        info.channel = 6;
        info.ip = ip;
        info.mask = 0xFFFFFF00;
        info.gateway = 0xC0A80101;
        info.dns = 0xC0A80101;
    }
    void fail(Reason r) {
        info.state = LinkState::kDown;
        info.reason = r;
    }

    LinkInfo info = {};
    Settings last = {};
    int connects = 0, leaves = 0, offs = 0, scans = 0, services = 0;
    bool scanOk = true, scanDone = false, scanFail = false;
    std::vector<ScanEntry> found;
    std::string host, ntp;
    int64_t utc = 0, sync = 0;
};

ScanEntry entry(const char *ssid, int rssi, unsigned ch, Security sec) {
    ScanEntry e = {};
    snprintf(e.ssid, sizeof e.ssid, "%s", ssid);
    e.rssi = static_cast<int8_t>(rssi);
    e.channel = static_cast<uint8_t>(ch);
    e.security = sec;
    return e;
}

Settings saved(const char *ssid, const char *pass) {
    Settings s = defaults();
    snprintf(s.ssid, sizeof s.ssid, "%s", ssid);
    snprintf(s.pass, sizeof s.pass, "%s", pass);
    return s;
}

#define CONTAINS(haystack, needle) \
    TEST_ASSERT_TRUE_MESSAGE((haystack).find(needle) != std::string::npos, (haystack).c_str())
#define NOT_CONTAINS(haystack, needle) \
    TEST_ASSERT_TRUE_MESSAGE((haystack).find(needle) == std::string::npos, (haystack).c_str())

// --- Saved settings ---------------------------------------------------------------------

void test_defaults() {
    Settings s = defaults();
    TEST_ASSERT_TRUE(s.enabled);
    TEST_ASSERT_TRUE(s.dhcp);
    TEST_ASSERT_FALSE(s.hasNetwork());
    TEST_ASSERT_EQUAL_STRING("usr-modem", s.host);
    TEST_ASSERT_EQUAL_STRING("pool.ntp.org", s.ntp);
    TEST_ASSERT_EQUAL_STRING("Pacific/Auckland", s.tz);
}

void test_settings_round_trip() {
    Settings s = defaults();
    snprintf(s.ssid, sizeof s.ssid, "%s", "A network name of 32 characters!");
    TEST_ASSERT_EQUAL(32, strlen(s.ssid));
    memset(s.pass, 'a', 64);  // a 64-digit hex key fills the field
    s.pass[64] = '\0';
    s.enabled = false;
    s.dhcp = false;
    s.ip = 0xC0A80132;
    s.mask = 0xFFFFFF00;
    s.gateway = 0xC0A80101;
    s.dns = 0x08080808;
    snprintf(s.host, sizeof s.host, "%s", "retro");
    snprintf(s.ntp, sizeof s.ntp, "%s", "nz.pool.ntp.org");
    snprintf(s.tz, sizeof s.tz, "%s", "Europe/London");
    MemStore st;
    TEST_ASSERT_TRUE(save(st, s));
    TEST_ASSERT_EQUAL(kBlobSize, st.len);
    Settings back;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kSaved), static_cast<int>(load(st, &back)));
    TEST_ASSERT_EQUAL_STRING(s.ssid, back.ssid);
    TEST_ASSERT_EQUAL_STRING(s.pass, back.pass);
    TEST_ASSERT_FALSE(back.enabled);
    TEST_ASSERT_FALSE(back.dhcp);
    TEST_ASSERT_EQUAL_HEX32(s.ip, back.ip);
    TEST_ASSERT_EQUAL_HEX32(s.mask, back.mask);
    TEST_ASSERT_EQUAL_HEX32(s.gateway, back.gateway);
    TEST_ASSERT_EQUAL_HEX32(s.dns, back.dns);
    TEST_ASSERT_EQUAL_STRING("retro", back.host);
    TEST_ASSERT_EQUAL_STRING("nz.pool.ntp.org", back.ntp);
    TEST_ASSERT_EQUAL_STRING("Europe/London", back.tz);
}

void test_missing_or_damaged_settings_give_defaults() {
    MemStore st;
    Settings s;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kEmpty), static_cast<int>(load(st, &s)));
    TEST_ASSERT_FALSE(s.hasNetwork());

    TEST_ASSERT_TRUE(save(st, saved("HomeNet", "password1")));
    st.data[10] ^= 0x40;  // bad CRC
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kDamaged), static_cast<int>(load(st, &s)));
    TEST_ASSERT_FALSE(s.hasNetwork());
    TEST_ASSERT_EQUAL_STRING("", s.pass);

    TEST_ASSERT_TRUE(save(st, saved("HomeNet", "password1")));
    st.len -= 1;  // wrong size
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kDamaged), static_cast<int>(load(st, &s)));

    TEST_ASSERT_TRUE(save(st, saved("HomeNet", "password1")));
    st.data[0] = 'X';  // not ours
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kDamaged), static_cast<int>(load(st, &s)));
}

void test_ssid_and_password_checks() {
    TEST_ASSERT_TRUE(validSsid("HomeNet"));
    TEST_ASSERT_TRUE(validSsid("My Home Wi-Fi 5G"));
    TEST_ASSERT_FALSE(validSsid(""));
    TEST_ASSERT_FALSE(validSsid("123456789012345678901234567890123"));  // 33
    TEST_ASSERT_FALSE(validSsid("tab\there"));

    TEST_ASSERT_TRUE(validPassword(""));  // open network
    TEST_ASSERT_FALSE(validPassword("1234567"));
    TEST_ASSERT_TRUE(validPassword("12345678"));
    TEST_ASSERT_TRUE(validPassword("a b c d e f g h ~!@#$%^&*()"));
    std::string p63(63, 'x');
    TEST_ASSERT_TRUE(validPassword(p63.c_str()));
    std::string hex64(64, 'F');
    TEST_ASSERT_TRUE(validPassword(hex64.c_str()));
    std::string notHex64(64, 'g');
    TEST_ASSERT_FALSE(validPassword(notHex64.c_str()));
    TEST_ASSERT_FALSE(validPassword("password\x01"));
}

void test_hostname_rules() {
    char h[kHostMax + 1];
    TEST_ASSERT_TRUE(normalizeHost("usr-modem", h, sizeof h));
    TEST_ASSERT_EQUAL_STRING("usr-modem", h);
    TEST_ASSERT_TRUE(normalizeHost("Retro-Modem2.LOCAL", h, sizeof h));
    TEST_ASSERT_EQUAL_STRING("retro-modem2", h);
    TEST_ASSERT_FALSE(normalizeHost("", h, sizeof h));
    TEST_ASSERT_FALSE(normalizeHost("-modem", h, sizeof h));
    TEST_ASSERT_FALSE(normalizeHost("modem-", h, sizeof h));
    TEST_ASSERT_FALSE(normalizeHost("bad_name", h, sizeof h));
    TEST_ASSERT_FALSE(normalizeHost("two.parts", h, sizeof h));
    TEST_ASSERT_FALSE(normalizeHost("abcdefghijklmnopqrstuvwxyz0123456", h, sizeof h));  // 33

    TEST_ASSERT_TRUE(validServer("pool.ntp.org"));
    TEST_ASSERT_TRUE(validServer("192.168.1.1"));
    TEST_ASSERT_FALSE(validServer(""));
    TEST_ASSERT_FALSE(validServer("pool ntp"));
    TEST_ASSERT_FALSE(validServer(".pool"));
}

void test_ip_parse_and_format() {
    uint32_t ip = 0;
    const char *rest = nullptr;
    TEST_ASSERT_TRUE(parseIp("192.168.1.50", &ip, &rest));
    TEST_ASSERT_EQUAL_HEX32(0xC0A80132, ip);
    TEST_ASSERT_EQUAL_STRING("", rest);
    TEST_ASSERT_TRUE(parseIp("10.0.0.1, 255.0.0.0", &ip, &rest));
    TEST_ASSERT_EQUAL_HEX32(0x0A000001, ip);
    TEST_ASSERT_EQUAL_STRING("255.0.0.0", rest);
    TEST_ASSERT_FALSE(parseIp("256.1.1.1", &ip));
    TEST_ASSERT_FALSE(parseIp("1.2.3", &ip));
    TEST_ASSERT_FALSE(parseIp("1.2.3.4x", &ip));
    TEST_ASSERT_FALSE(parseIp("1..2.3", &ip));
    TEST_ASSERT_FALSE(parseIp("0001.2.3.4", &ip));
    char buf[16];
    formatIp(0xC0A8012A, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("192.168.1.42", buf);
    formatIp(0, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("0.0.0.0", buf);
}

void test_static_address_checks() {
    TEST_ASSERT_TRUE(validStatic(0xC0A80132, 0xFFFFFF00, 0xC0A80101));
    TEST_ASSERT_TRUE(validStatic(0x0A000005, 0xFF000000, 0x0A000001));
    TEST_ASSERT_FALSE(validStatic(0xC0A80132, 0xFFFFFF00, 0xC0A80201));  // gateway elsewhere
    TEST_ASSERT_FALSE(validStatic(0xC0A80132, 0xFF00FF00, 0xC0A80101));  // not contiguous
    TEST_ASSERT_FALSE(validStatic(0xC0A80100, 0xFFFFFF00, 0xC0A80101));  // network address
    TEST_ASSERT_FALSE(validStatic(0xC0A801FF, 0xFFFFFF00, 0xC0A80101));  // broadcast
    TEST_ASSERT_FALSE(validStatic(0xC0A80101, 0xFFFFFF00, 0xC0A80101));  // = gateway
    TEST_ASSERT_FALSE(validStatic(0xC0A80132, 0, 0xC0A80101));
    TEST_ASSERT_FALSE(validStatic(0x7F000002, 0xFF000000, 0x7F000001));  // loopback
}

// --- Time zones -------------------------------------------------------------------------

int64_t utcOf(int y, unsigned m, unsigned d, int hh, int mm, int ss) {
    return tz::daysFromCivil(y, m, d) * 86400 + hh * 3600 + mm * 60 + ss;
}

void test_calendar() {
    TEST_ASSERT_EQUAL_INT64(0, tz::daysFromCivil(1970, 1, 1));
    TEST_ASSERT_EQUAL_INT64(20454, tz::daysFromCivil(2026, 1, 1));
    TEST_ASSERT_EQUAL_INT64(1792800000LL, utcOf(2026, 10, 24, 0, 0, 0));
    int y;
    unsigned m, d;
    tz::civilFromDays(tz::daysFromCivil(2028, 2, 29), &y, &m, &d);
    TEST_ASSERT_EQUAL(2028, y);
    TEST_ASSERT_EQUAL(2, m);
    TEST_ASSERT_EQUAL(29, d);
}

void test_parse_nz_rule() {
    tz::Rule r;
    TEST_ASSERT_TRUE(tz::parse("NZST-12NZDT,M9.5.0,M4.1.0/3", &r));
    TEST_ASSERT_EQUAL_STRING("NZST", r.stdName);
    TEST_ASSERT_EQUAL_STRING("NZDT", r.dstName);
    TEST_ASSERT_EQUAL(12 * 3600, r.stdOffset);
    TEST_ASSERT_EQUAL(13 * 3600, r.dstOffset);
    TEST_ASSERT_TRUE(r.hasDst);
    TEST_ASSERT_EQUAL(9, r.start.month);
    TEST_ASSERT_EQUAL(5, r.start.week);
    TEST_ASSERT_EQUAL(0, r.start.wday);
    TEST_ASSERT_EQUAL(2 * 3600, r.start.time);
    TEST_ASSERT_EQUAL(3 * 3600, r.end.time);
}

void test_bad_rules_are_refused() {
    tz::Rule r;
    TEST_ASSERT_FALSE(tz::parse("", &r));
    TEST_ASSERT_FALSE(tz::parse("NZST", &r));                  // no offset
    TEST_ASSERT_FALSE(tz::parse("NZ-12", &r));                 // name too short
    TEST_ASSERT_FALSE(tz::parse("NZST-12NZDT", &r));           // no change dates
    TEST_ASSERT_FALSE(tz::parse("NZST-12NZDT,J60,J300", &r));  // only Mm.w.d
    TEST_ASSERT_FALSE(tz::parse("NZST-12NZDT,M13.1.0,M4.1.0", &r));
    TEST_ASSERT_FALSE(tz::parse("NZST-12 extra", &r));
    TEST_ASSERT_FALSE(tz::resolve("Mars/Olympus_Mons", &r, nullptr, 0));
}

void test_nz_daylight_saving_changes() {
    tz::Rule r;
    TEST_ASSERT_TRUE(tz::resolve("Pacific/Auckland", &r, nullptr, 0));
    const char *abbrev = nullptr;
    // Starts last Sunday of September (27 Sep 2026) at 02:00 NZST = 26 Sep 14:00 UTC.
    TEST_ASSERT_EQUAL(12 * 3600, tz::offsetAt(r, utcOf(2026, 9, 26, 13, 59, 59), &abbrev));
    TEST_ASSERT_EQUAL_STRING("NZST", abbrev);
    TEST_ASSERT_EQUAL(13 * 3600, tz::offsetAt(r, utcOf(2026, 9, 26, 14, 0, 0), &abbrev));
    TEST_ASSERT_EQUAL_STRING("NZDT", abbrev);
    // Ends first Sunday of April (5 Apr 2026) at 03:00 NZDT = 4 Apr 14:00 UTC.
    TEST_ASSERT_EQUAL(13 * 3600, tz::offsetAt(r, utcOf(2026, 4, 4, 13, 59, 59)));
    TEST_ASSERT_EQUAL(12 * 3600, tz::offsetAt(r, utcOf(2026, 4, 4, 14, 0, 0)));
    // Summer across the new year, winter in July.
    TEST_ASSERT_EQUAL(13 * 3600, tz::offsetAt(r, utcOf(2026, 12, 31, 12, 0, 0)));
    TEST_ASSERT_EQUAL(13 * 3600, tz::offsetAt(r, utcOf(2027, 1, 15, 0, 0, 0)));
    TEST_ASSERT_EQUAL(12 * 3600, tz::offsetAt(r, utcOf(2026, 7, 1, 0, 0, 0)));
}

void test_northern_zones_and_utc() {
    tz::Rule r;
    TEST_ASSERT_TRUE(tz::resolve("europe/london", &r, nullptr, 0));  // any case
    // BST from the last Sunday of March (29 Mar 2026) 01:00 UTC.
    TEST_ASSERT_EQUAL(0, tz::offsetAt(r, utcOf(2026, 3, 29, 0, 59, 59)));
    TEST_ASSERT_EQUAL(3600, tz::offsetAt(r, utcOf(2026, 3, 29, 1, 0, 0)));
    // Back to GMT on the last Sunday of October (25 Oct 2026) at 02:00 BST = 01:00 UTC.
    TEST_ASSERT_EQUAL(3600, tz::offsetAt(r, utcOf(2026, 10, 25, 0, 59, 59)));
    TEST_ASSERT_EQUAL(0, tz::offsetAt(r, utcOf(2026, 10, 25, 1, 0, 0)));

    TEST_ASSERT_TRUE(tz::resolve("America/New_York", &r, nullptr, 0));
    // EDT from the second Sunday of March (8 Mar 2026) 02:00 EST = 07:00 UTC.
    TEST_ASSERT_EQUAL(-5 * 3600, tz::offsetAt(r, utcOf(2026, 3, 8, 6, 59, 59)));
    TEST_ASSERT_EQUAL(-4 * 3600, tz::offsetAt(r, utcOf(2026, 3, 8, 7, 0, 0)));

    TEST_ASSERT_TRUE(tz::resolve("Pacific/Chatham", &r, nullptr, 0));
    TEST_ASSERT_EQUAL(12 * 3600 + 45 * 60, tz::offsetAt(r, utcOf(2026, 7, 1, 0, 0, 0)));

    TEST_ASSERT_TRUE(tz::resolve("UTC", &r, nullptr, 0));
    TEST_ASSERT_EQUAL(0, tz::offsetAt(r, utcOf(2026, 7, 1, 0, 0, 0)));
}

void test_resolve_gives_the_stored_name() {
    tz::Rule r;
    char name[64];
    TEST_ASSERT_TRUE(tz::resolve("PACIFIC/AUCKLAND", &r, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("Pacific/Auckland", name);
    TEST_ASSERT_TRUE(tz::resolve("AEST-10", &r, name, sizeof name));
    TEST_ASSERT_EQUAL_STRING("AEST-10", name);
    TEST_ASSERT_TRUE(tz::zoneCount() >= 10);
    for (size_t i = 0; i < tz::zoneCount(); ++i) {
        TEST_ASSERT_TRUE_MESSAGE(tz::parse(tz::zoneRule(i), &r), tz::zoneName(i));
    }
}

void test_format_local_time() {
    tz::Rule r;
    TEST_ASSERT_TRUE(tz::resolve("Pacific/Auckland", &r, nullptr, 0));
    char buf[48];
    tz::formatLocal(r, utcOf(2026, 10, 24, 8, 15, 7), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("2026-10-24 21:15:07 NZDT", buf);
    tz::formatLocal(r, utcOf(2026, 12, 31, 11, 30, 0), buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("2027-01-01 00:30:00 NZDT", buf);
    tz::formatOffset(13 * 3600, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("+13:00", buf);
    tz::formatOffset(-5 * 3600, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("-05:00", buf);
    tz::formatOffset(12 * 3600 + 45 * 60, buf, sizeof buf);
    TEST_ASSERT_EQUAL_STRING("+12:45", buf);
}

// --- Controller: connecting, retrying, lights -----------------------------------------------

struct NetRig {
    MemStore store;
    FakeRadio radio;
    Net net{store, radio};
    explicit NetRig(const Settings *s = nullptr) {
        if (s != nullptr) {
            save(store, *s);
            store.saves = 0;
        }
    }
};

void test_nothing_saved_red_light_no_join() {
    NetRig r;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kEmpty), static_cast<int>(r.net.begin(0)));
    r.net.tick(10);
    TEST_ASSERT_EQUAL(0, r.radio.connects);
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kRed), static_cast<int>(r.net.rgb(10)));
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kRed), static_cast<int>(r.net.rgb(510)));
    TEST_ASSERT_EQUAL(static_cast<int>(Arq::kOff), static_cast<int>(r.net.arq()));
    TEST_ASSERT_EQUAL_STRING("not set up", r.net.stateText());
}

void test_saved_network_flashes_green_then_solid() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    TEST_ASSERT_EQUAL(1, r.radio.connects);
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.radio.last.ssid);
    r.net.tick(100);
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kGreen), static_cast<int>(r.net.rgb(100)));
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kOff), static_cast<int>(r.net.rgb(600)));
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kGreen), static_cast<int>(r.net.rgb(1000)));
    TEST_ASSERT_EQUAL(static_cast<int>(Arq::kBlink), static_cast<int>(r.net.arq()));
    TEST_ASSERT_EQUAL_STRING("connecting", r.net.stateText());

    r.radio.up();
    r.net.tick(1200);
    TEST_ASSERT_TRUE(r.net.up());
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kGreen), static_cast<int>(r.net.rgb(1600)));
    TEST_ASSERT_EQUAL(static_cast<int>(Arq::kOn), static_cast<int>(r.net.arq()));
    TEST_ASSERT_EQUAL(1, r.radio.services);
    TEST_ASSERT_EQUAL_STRING("usr-modem", r.radio.host.c_str());
    char n[Net::kNoticeMax];
    TEST_ASSERT_TRUE(r.net.takeNotice(n, sizeof n));
    TEST_ASSERT_EQUAL_STRING("Wi-Fi: connected to HomeNet, IP 192.168.1.42", n);
    TEST_ASSERT_FALSE(r.net.takeNotice(n, sizeof n));
}

void test_lost_wifi_reconnects_at_once() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(100);
    char n[Net::kNoticeMax];
    r.net.takeNotice(n, sizeof n);
    r.radio.fail(Reason::kLost);
    r.net.tick(5000);
    TEST_ASSERT_TRUE(r.net.takeNotice(n, sizeof n));
    TEST_ASSERT_EQUAL_STRING("Wi-Fi: lost HomeNet, reconnecting", n);
    TEST_ASSERT_EQUAL(2, r.radio.connects);
    TEST_ASSERT_EQUAL(static_cast<int>(Arq::kBlink), static_cast<int>(r.net.arq()));
}

void test_failed_attempts_back_off() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.fail(Reason::kWrongPassword);
    r.net.tick(3000);
    char n[Net::kNoticeMax];
    TEST_ASSERT_TRUE(r.net.takeNotice(n, sizeof n));
    TEST_ASSERT_EQUAL_STRING("Wi-Fi: can't join HomeNet: wrong password. Trying again", n);
    TEST_ASSERT_EQUAL_STRING("not connected (wrong password), trying again", r.net.stateText());
    r.net.tick(7999);
    TEST_ASSERT_EQUAL(1, r.radio.connects);  // 5 s after the failure
    r.net.tick(8000);
    TEST_ASSERT_EQUAL(2, r.radio.connects);
    r.radio.fail(Reason::kWrongPassword);
    r.net.tick(9000);
    TEST_ASSERT_FALSE(r.net.takeNotice(n, sizeof n));  // same reason: no new notice
    r.net.tick(18999);
    TEST_ASSERT_EQUAL(2, r.radio.connects);  // then 10 s
    r.net.tick(19000);
    TEST_ASSERT_EQUAL(3, r.radio.connects);
}

void test_attempt_with_no_answer_gives_up_after_30_s() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.net.tick(29999);
    TEST_ASSERT_EQUAL(0, r.radio.leaves);
    r.net.tick(30000);
    TEST_ASSERT_EQUAL(1, r.radio.leaves);
    r.net.tick(35000);
    TEST_ASSERT_EQUAL(2, r.radio.connects);
}

void test_switched_off_lights_off() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(100);
    TEST_ASSERT_TRUE(r.net.setEnabled(false));
    TEST_ASSERT_EQUAL(1, r.radio.offs);
    r.net.tick(200);
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kOff), static_cast<int>(r.net.rgb(200)));
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kOff), static_cast<int>(r.net.rgb(700)));
    TEST_ASSERT_EQUAL(static_cast<int>(Arq::kOff), static_cast<int>(r.net.arq()));
    TEST_ASSERT_EQUAL_STRING("off", r.net.stateText());
    Settings back;
    load(r.store, &back);
    TEST_ASSERT_FALSE(back.enabled);
    TEST_ASSERT_EQUAL_STRING("HomeNet", back.ssid);  // network kept

    NetRig again(&back);
    again.net.begin(0);
    TEST_ASSERT_EQUAL(0, again.radio.connects);
    TEST_ASSERT_EQUAL(1, again.radio.offs);
    TEST_ASSERT_TRUE(again.net.setEnabled(true));
    TEST_ASSERT_EQUAL(1, again.radio.connects);
}

void test_forget_wipes_network_and_password() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(100);
    TEST_ASSERT_TRUE(r.net.forget());
    r.net.tick(200);
    TEST_ASSERT_FALSE(r.net.settings().hasNetwork());
    TEST_ASSERT_EQUAL_STRING("", r.net.settings().pass);
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kRed), static_cast<int>(r.net.rgb(200)));
    Settings back;
    load(r.store, &back);
    TEST_ASSERT_FALSE(back.hasNetwork());
    TEST_ASSERT_EQUAL_STRING("", back.pass);
}

void test_static_address_and_names_apply() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(100);
    TEST_ASSERT_FALSE(r.net.setStatic(0xC0A80132, 0xFFFFFF00, 0xC0A80201, 0));
    TEST_ASSERT_TRUE(r.net.setStatic(0xC0A80132, 0xFFFFFF00, 0xC0A80101, 0));
    TEST_ASSERT_FALSE(r.radio.last.dhcp);
    TEST_ASSERT_EQUAL_HEX32(0xC0A80132, r.radio.last.ip);
    TEST_ASSERT_EQUAL_HEX32(0xC0A80101, r.radio.last.dns);  // DNS defaults to the gateway
    TEST_ASSERT_EQUAL(2, r.radio.connects);                 // reconnected
    TEST_ASSERT_TRUE(r.net.setDhcp());
    TEST_ASSERT_TRUE(r.radio.last.dhcp);

    r.radio.up();
    r.net.tick(200);
    TEST_ASSERT_TRUE(r.net.setHost("Retro.local"));
    TEST_ASSERT_EQUAL_STRING("retro", r.net.settings().host);
    TEST_ASSERT_EQUAL_STRING("retro", r.radio.host.c_str());  // mDNS restarted
    TEST_ASSERT_FALSE(r.net.setHost("no_way"));
    TEST_ASSERT_TRUE(r.net.setNtp("nz.pool.ntp.org"));
    TEST_ASSERT_EQUAL_STRING("nz.pool.ntp.org", r.radio.ntp.c_str());
    TEST_ASSERT_TRUE(r.net.setTz("europe/london"));
    TEST_ASSERT_EQUAL_STRING("Europe/London", r.net.settings().tz);
    TEST_ASSERT_FALSE(r.net.setTz("Mars/Base"));
}

void test_clock_notice_once_set() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(100);
    char n[Net::kNoticeMax];
    r.net.takeNotice(n, sizeof n);
    r.radio.utc = utcOf(2026, 10, 24, 8, 15, 7);
    r.radio.sync = r.radio.utc;
    r.net.tick(200);
    TEST_ASSERT_TRUE(r.net.takeNotice(n, sizeof n));
    TEST_ASSERT_EQUAL_STRING("Clock: set by NTP, 2026-10-24 21:15:07 NZDT", n);
    r.net.tick(300);
    TEST_ASSERT_FALSE(r.net.takeNotice(n, sizeof n));
}

// --- Controller: join and scan --------------------------------------------------------------

void test_join_saves_only_when_connected() {
    NetRig r;
    r.net.begin(0);
    TEST_ASSERT_TRUE(r.net.join("NewNet", "password2", 0));
    TEST_ASSERT_EQUAL_STRING("NewNet", r.radio.last.ssid);
    TEST_ASSERT_EQUAL_STRING("password2", r.radio.last.pass);
    r.net.tick(100);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kRunning), static_cast<int>(r.net.joinResult()));
    TEST_ASSERT_EQUAL(static_cast<int>(Arq::kBlink), static_cast<int>(r.net.arq()));
    TEST_ASSERT_EQUAL(0, r.store.saves);
    TEST_ASSERT_FALSE(r.net.settings().hasNetwork());
    r.radio.up();
    r.net.tick(3000);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kJoined), static_cast<int>(r.net.joinResult()));
    TEST_ASSERT_EQUAL(1, r.store.saves);
    TEST_ASSERT_EQUAL_STRING("NewNet", r.net.settings().ssid);
    TEST_ASSERT_EQUAL(1, r.radio.services);
    Settings back;
    load(r.store, &back);
    TEST_ASSERT_EQUAL_STRING("password2", back.pass);
}

void test_failed_join_keeps_the_old_network() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(100);
    TEST_ASSERT_TRUE(r.net.join("NewNet", "wrongpass", 200));
    r.radio.fail(Reason::kWrongPassword);
    r.net.tick(4000);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kFailed), static_cast<int>(r.net.joinResult()));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::kWrongPassword),
                      static_cast<int>(r.net.joinReason()));
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.net.settings().ssid);
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.radio.last.ssid);  // rejoined
    TEST_ASSERT_EQUAL(0, r.store.saves);
}

void test_join_retries_a_missing_network_then_gives_up() {
    NetRig r;
    r.net.begin(0);
    TEST_ASSERT_TRUE(r.net.join("FarNet", "password2", 0));
    r.radio.fail(Reason::kNotFound);
    r.net.tick(1000);
    TEST_ASSERT_EQUAL(1, r.radio.connects);
    r.net.tick(2000);
    TEST_ASSERT_EQUAL(2, r.radio.connects);  // tried again
    r.radio.fail(Reason::kNotFound);
    r.net.tick(10000);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kFailed), static_cast<int>(r.net.joinResult()));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::kNotFound), static_cast<int>(r.net.joinReason()));
    TEST_ASSERT_EQUAL(static_cast<int>(Rgb::kRed), static_cast<int>(r.net.rgb(10000)));
}

void test_join_with_no_answer_times_out() {
    NetRig r;
    r.net.begin(0);
    TEST_ASSERT_TRUE(r.net.join("SlowNet", "password2", 0));
    r.net.tick(19999);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kRunning), static_cast<int>(r.net.joinResult()));
    r.net.tick(20000);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kFailed), static_cast<int>(r.net.joinResult()));
    TEST_ASSERT_EQUAL(static_cast<int>(Reason::kNoAnswer), static_cast<int>(r.net.joinReason()));
}

void test_scan_sorts_and_merges() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.net.tick(100);  // still connecting
    TEST_ASSERT_TRUE(r.net.scanStart(200));
    TEST_ASSERT_EQUAL(1, r.radio.leaves);  // the attempt pauses for the scan
    r.radio.found = {entry("Weak", -85, 1, Security::kWpa2), entry("", -40, 6, Security::kWpa2),
                     entry("HomeNet", -60, 6, Security::kWpa2),
                     entry("HomeNet", -50, 11, Security::kWpa2),
                     entry("Cafe", -70, 3, Security::kOpen)};
    r.net.tick(1000);
    TEST_ASSERT_EQUAL(static_cast<int>(ScanState::kRunning), static_cast<int>(r.net.scanState()));
    TEST_ASSERT_EQUAL(1, r.radio.connects);  // no retry during the scan
    r.radio.scanDone = true;
    r.net.tick(3000);
    TEST_ASSERT_EQUAL(static_cast<int>(ScanState::kDone), static_cast<int>(r.net.scanState()));
    TEST_ASSERT_EQUAL(3, r.net.scanCount());
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.net.scanEntry(0).ssid);
    TEST_ASSERT_EQUAL(-50, r.net.scanEntry(0).rssi);
    TEST_ASSERT_EQUAL_STRING("Cafe", r.net.scanEntry(1).ssid);
    TEST_ASSERT_EQUAL_STRING("Weak", r.net.scanEntry(2).ssid);
    TEST_ASSERT_EQUAL(2, r.radio.connects);  // back to joining the saved network
}

// --- AT* commands -----------------------------------------------------------------------

class StrSink : public atcmd::Sink {
public:
    void write(const char *d, size_t n) override { s.append(d, n); }
    std::string s;
};

uint32_t gNow = 0;
uint32_t fakeMillis() {
    return gNow;
}

atcmd::Reply cmdHelp(atcmd::Port &port, const char *) {
    atcmd::showStarHelp(port);
    return atcmd::Reply::kOk;
}

const atcmd::StarCommand kStar[] = {
    {"HELP", "AT*HELP", "this list", cmdHelp, false},
    NETCFG_STAR_COMMANDS,
};

struct CmdRig {
    MemStore modemStore;
    MemStore netStore;
    FakeRadio radio;
    Net net{netStore, radio};
    atcmd::Modem modem{modemStore, kStar, sizeof kStar / sizeof kStar[0]};
    StrSink out;
    atcmd::Port port{modem, out, 115200};
    explicit CmdRig(const Settings *s = nullptr) {
        gNow = 0;
        if (s != nullptr) {
            save(netStore, *s);
            netStore.saves = 0;
        }
        modem.begin();
        net.begin(0);
        attachCommands(net, fakeMillis);
    }
    std::string type(const char *typed) {
        out.s.clear();
        for (const char *p = typed; *p != '\0'; ++p) {
            port.feed(*p);
        }
        return out.s;
    }
    // Lets time pass: the controller and the commands' late results run.
    std::string run(uint32_t ms) {
        out.s.clear();
        gNow += ms;
        net.tick(gNow);
        pollCommands();
        return out.s;
    }
};

void test_wifi_status_shows_password_set_not_the_password() {
    Settings s = saved("HomeNet", "Canary-pw-9f3K");
    CmdRig r(&s);
    r.radio.up();
    r.run(100);
    std::string o = r.type("AT*WIFI?\r");
    CONTAINS(o, "Wi-Fi:    connected");
    CONTAINS(o, "Network:  HomeNet");
    CONTAINS(o, "Password: set");
    CONTAINS(o, "Signal:   -54 dBm, channel 6");
    CONTAINS(o, "Address:  192.168.1.42 (DHCP)");
    CONTAINS(o, "Gateway:  192.168.1.1");
    CONTAINS(o, "MAC:      24:6F:28:01:02:03");
    CONTAINS(o, "Name:     usr-modem.local");
    CONTAINS(o, "\r\nOK\r\n");
    NOT_CONTAINS(o, "Canary");
}

void test_wifi_on_off() {
    Settings s = saved("HomeNet", "password1");
    CmdRig r(&s);
    CONTAINS(r.type("AT*WIFI=OFF\r"), "OK");
    TEST_ASSERT_FALSE(r.net.settings().enabled);
    CONTAINS(r.type("AT*WIFI?\r"), "Wi-Fi:    off");
    CONTAINS(r.type("AT*WIFI=ON\r"), "OK");
    TEST_ASSERT_TRUE(r.net.settings().enabled);
    CONTAINS(r.type("AT*WIFI=MAYBE\r"), "ERROR");
}

void test_scan_command_lists_later() {
    CmdRig r;
    r.radio.found = {entry("HomeNet", -54, 6, Security::kWpa2),
                     entry("Cafe", -70, 11, Security::kOpen)};
    std::string o = r.type("AT*WIFISCAN\r\n");  // the LF doesn't cancel it
    CONTAINS(o, "Scanning...");
    NOT_CONTAINS(o, "OK");
    NOT_CONTAINS(o, "ERROR");
    TEST_ASSERT_EQUAL_STRING("", r.run(500).c_str());
    r.radio.scanDone = true;
    o = r.run(500);
    CONTAINS(o, " 1  HomeNet");
    CONTAINS(o, " -54 dBm  ch  6  WPA2");
    CONTAINS(o, " 2  Cafe");
    CONTAINS(o, "open");
    CONTAINS(o, "\r\nOK\r\n");
}

void test_scan_cancelled_by_a_key() {
    CmdRig r;
    r.type("AT*WIFISCAN\r");
    CONTAINS(r.type("x"), "ERROR");
    r.radio.scanDone = true;
    TEST_ASSERT_EQUAL_STRING("", r.run(500).c_str());  // nothing more
    CONTAINS(r.type("AT\r"), "OK");
}

void test_join_by_number_with_masked_password() {
    CmdRig r;
    r.radio.found = {entry("HomeNet", -54, 6, Security::kWpa2)};
    r.type("AT*WIFISCAN\r");
    r.radio.scanDone = true;
    r.run(500);
    std::string o = r.type("AT*WIFIJOIN=1\r");
    CONTAINS(o, "Password (Enter for none): ");
    o = r.type("Canary-pw-9f3K");
    TEST_ASSERT_EQUAL_STRING("**************", o.c_str());
    o = r.type("\r");
    CONTAINS(o, "Joining HomeNet...");
    NOT_CONTAINS(o, "OK");
    TEST_ASSERT_EQUAL_STRING("Canary-pw-9f3K", r.radio.last.pass);
    r.radio.up();
    o = r.run(2000);
    CONTAINS(o, "Connected, IP 192.168.1.42");
    CONTAINS(o, "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.net.settings().ssid);
    CONTAINS(r.type("AT\r"), "OK");  // the port is back to commands
}

void test_join_by_quoted_name_and_failure() {
    Settings s = saved("HomeNet", "password1");
    CmdRig r(&s);
    r.radio.up();
    r.run(100);
    r.type("AT*WIFIJOIN=\"My Other Net\"\r");
    CONTAINS(r.type("wrongpass\r"), "Joining My Other Net...");
    r.radio.fail(Reason::kWrongPassword);
    std::string o = r.run(3000);
    CONTAINS(o, "Not connected: wrong password. Settings not changed");
    CONTAINS(o, "ERROR");
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.net.settings().ssid);
}

void test_join_open_network_from_scan_needs_no_password() {
    CmdRig r;
    r.radio.found = {entry("Cafe", -60, 1, Security::kOpen)};
    r.type("AT*WIFISCAN\r");
    r.radio.scanDone = true;
    r.run(500);
    std::string o = r.type("AT*WIFIJOIN=1\r");
    NOT_CONTAINS(o, "Password");
    CONTAINS(o, "Joining Cafe...");
    TEST_ASSERT_EQUAL_STRING("", r.radio.last.pass);
}

void test_join_refuses_bad_input() {
    CmdRig r;
    CONTAINS(r.type("AT*WIFIJOIN\r"), "ERROR");
    CONTAINS(r.type("AT*WIFIJOIN=3\r"), "Run AT*WIFISCAN first");
    std::string o = r.type("AT*WIFIJOIN=\"x\"\r");
    CONTAINS(o, "Password");
    o = r.type("short\r");
    CONTAINS(o, "8-63 characters");
    CONTAINS(o, "ERROR");
    TEST_ASSERT_EQUAL(0, r.radio.connects);
    r.type("AT*WIFIJOIN=\"x\"\r");
    CONTAINS(r.type("abc\x1b"), "ERROR");  // Esc cancels
    TEST_ASSERT_EQUAL(0, r.radio.connects);
    CONTAINS(r.type("AT\r"), "OK");
}

void test_repeat_asks_for_the_password_again() {
    CmdRig r;
    r.type("AT*WIFIJOIN=\"HomeNet\"\r");
    r.type("password1\r");
    r.radio.fail(Reason::kWrongPassword);
    r.run(3000);
    std::string o = r.type("A/");
    CONTAINS(o, "Password (Enter for none): ");
    NOT_CONTAINS(o, "password1");
    NOT_CONTAINS(o, "Joining");
}

void test_wifiip_command() {
    Settings s = saved("HomeNet", "password1");
    CmdRig r(&s);
    CONTAINS(r.type("AT*WIFIIP?\r"), "DHCP");
    CONTAINS(r.type("AT*WIFIIP=192.168.1.50,255.255.255.0,192.168.1.1\r"), "OK");
    std::string o = r.type("AT*WIFIIP?\r");
    CONTAINS(o, "Static 192.168.1.50, mask 255.255.255.0, gateway 192.168.1.1, DNS 192.168.1.1");
    CONTAINS(r.type("AT*WIFIIP=192.168.1.50, 255.255.255.0, 192.168.1.1, 8.8.8.8\r"), "OK");
    CONTAINS(r.type("AT*WIFIIP?\r"), "DNS 8.8.8.8");
    CONTAINS(r.type("AT*WIFIIP=192.168.1.50,255.255.255.0\r"), "ERROR");
    CONTAINS(r.type("AT*WIFIIP=192.168.1.50,255.255.255.0,10.0.0.1\r"), "Not a usable");
    CONTAINS(r.type("AT*WIFIIP=1.2.3.4,255.255.255.0,1.2.3.1,x\r"), "ERROR");
    CONTAINS(r.type("AT*WIFIIP=DHCP\r"), "OK");
    CONTAINS(r.type("AT*WIFIIP?\r"), "DHCP");
}

void test_hostname_tz_ntp_commands() {
    CmdRig r;
    CONTAINS(r.type("AT*HOSTNAME?\r"), "usr-modem (usr-modem.local)");
    CONTAINS(r.type("AT*HOSTNAME=Retro-Modem\r"), "OK");
    CONTAINS(r.type("AT*HOSTNAME?\r"), "retro-modem (retro-modem.local)");
    CONTAINS(r.type("AT*HOSTNAME=bad_name\r"), "ERROR");

    std::string o = r.type("AT*TZ=?\r");
    CONTAINS(o, "Pacific/Auckland     NZST-12NZDT,M9.5.0,M4.1.0/3");
    CONTAINS(o, "Europe/London");
    CONTAINS(r.type("AT*TZ?\r"), "Pacific/Auckland (NZST-12NZDT,M9.5.0,M4.1.0/3)");
    CONTAINS(r.type("AT*TZ=europe/london\r"), "OK");
    CONTAINS(r.type("AT*TZ?\r"), "Europe/London (GMT0BST");
    CONTAINS(r.type("AT*TZ=AEST-10\r"), "OK");
    CONTAINS(r.type("AT*TZ?\r"), "AEST-10");
    CONTAINS(r.type("AT*TZ=Nowhere\r"), "ERROR");

    CONTAINS(r.type("AT*NTP?\r"), "pool.ntp.org");
    CONTAINS(r.type("AT*NTP=nz.pool.ntp.org\r"), "OK");
    CONTAINS(r.type("AT*NTP?\r"), "nz.pool.ntp.org");
    CONTAINS(r.type("AT*NTP=bad server\r"), "ERROR");
}

void test_time_command() {
    CmdRig r;
    std::string o = r.type("AT*TIME\r");
    CONTAINS(o, "Clock not set");
    CONTAINS(o, "Zone:     Pacific/Auckland");
    CONTAINS(o, "last update none yet");
    r.radio.utc = utcOf(2026, 10, 24, 8, 15, 7);
    r.radio.sync = utcOf(2026, 10, 24, 8, 0, 0);
    o = r.type("AT*TIME?\r");
    CONTAINS(o, "2026-10-24 21:15:07 NZDT (UTC+13:00)");
    CONTAINS(o, "NTP:      pool.ntp.org, last update 2026-10-24 21:00:00 NZDT");
    CONTAINS(r.type("AT*TIME=12:00\r"), "ERROR");
}

void test_help_lists_wifi_and_clock_commands() {
    CmdRig r;
    std::string o = r.type("AT*HELP\r");
    for (const char *c : {"AT*WIFI[?]", "AT*WIFISCAN", "AT*WIFIJOIN=", "AT*WIFIFORGET",
                          "AT*WIFIIP=", "AT*HOSTNAME=", "AT*TIME", "AT*TZ=", "AT*NTP="}) {
        CONTAINS(o, c);
    }
}

// The Wi-Fi password never appears in any output (brief 6.6, Q46).
void test_password_never_shown() {
    const char *kCanary = "Canary-pw-9f3K";
    Settings s = saved("HomeNet", kCanary);
    CmdRig r(&s);
    r.radio.up();
    r.radio.found = {entry("HomeNet", -54, 6, Security::kWpa2)};
    std::string all = r.run(100);
    const char *commands[] = {
        "AT*HELP\r",    "AT*WIFI\r", "AT*WIFI?\r", "AT*WIFIIP?\r",  "AT*HOSTNAME?\r",
        "AT*TIME\r",    "AT*TZ?\r",  "AT*TZ=?\r",  "AT*NTP?\r",     "ATI0\r",
        "ATI1\r",       "ATI2\r",    "ATI3\r",     "ATI4\r",        "ATI5\r",
        "ATI6\r",       "ATI7\r",    "AT&V\r",     "AT*WIFI=OFF\r", "AT*WIFI?\r",
        "AT*WIFI=ON\r",
    };
    for (const char *c : commands) {
        all += r.type(c);
        all += r.run(100);
    }
    all += r.type("AT*WIFISCAN\r");
    r.radio.scanDone = true;
    all += r.run(500);
    // Typing it at the prompt echoes only stars, and the join result doesn't repeat it.
    all += r.type("AT*WIFIJOIN=1\r");
    all += r.type(kCanary);
    all += r.type("\r");
    all += r.run(2000);
    all += r.type("A/");
    all += r.type("\x1b");
    all += r.type("AT*WIFIFORGET\r");
    all += r.type("AT*WIFI?\r");
    TEST_ASSERT_TRUE(all.find("Password: set") != std::string::npos);
    TEST_ASSERT_TRUE(all.find("Password: not set") != std::string::npos);
    const bool found =
        all.find(kCanary) != std::string::npos || all.find("Canary") != std::string::npos;
    TEST_ASSERT_FALSE_MESSAGE(found, "the Wi-Fi password appeared in the output");
}

void test_browser_join_keeps_old_network_until_candidate_connects() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(1);
    r.net.guardBrowserChange(10);
    TEST_ASSERT_TRUE(r.net.join("Candidate", "password2", 10, 60000));
    Settings disk;
    load(r.store, &disk);
    TEST_ASSERT_EQUAL_STRING("HomeNet", disk.ssid);
    r.net.tick(30000);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kRunning), static_cast<int>(r.net.joinResult()));
    r.radio.up();
    r.net.tick(40000);
    r.net.tick(40001);
    load(r.store, &disk);
    TEST_ASSERT_EQUAL_STRING("Candidate", disk.ssid);
}
void test_browser_ip_change_rolls_back_after_sixty_seconds() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(1);
    r.net.guardBrowserChange(10);
    TEST_ASSERT_TRUE(r.net.setStatic(0x0a000032, 0xffffff00, 0x0a000001, 0x0a000001));
    r.net.tick(60009);
    TEST_ASSERT_FALSE(r.net.settings().dhcp);
    r.net.tick(60010);
    TEST_ASSERT_TRUE(r.net.settings().dhcp);
    TEST_ASSERT_TRUE(r.radio.last.dhcp);
    Settings disk;
    load(r.store, &disk);
    TEST_ASSERT_TRUE(disk.dhcp);
    TEST_ASSERT_EQUAL_STRING("HomeNet", disk.ssid);
}
void test_browser_join_storage_failure_restores_previous_before_reporting_success() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(1);
    r.net.guardBrowserChange(10);
    TEST_ASSERT_TRUE(r.net.join("Candidate", "password2", 10, 60000));
    r.store.fail = true;
    r.radio.up();
    r.net.tick(100);
    TEST_ASSERT_EQUAL(static_cast<int>(JoinResult::kFailed), static_cast<int>(r.net.joinResult()));
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.net.settings().ssid);
    TEST_ASSERT_EQUAL_STRING("HomeNet", r.radio.last.ssid);
}
void test_browser_network_loss_actions_require_cancellable_confirmation() {
    Settings s = saved("HomeNet", "password1");
    CmdRig r(&s);
    r.port.setBrowser(true);
    auto out = r.type("AT*WIFIFORGET\r");
    CONTAINS(out, "disconnect browser");
    TEST_ASSERT_TRUE(r.net.settings().hasNetwork());
    r.type("N\r");
    TEST_ASSERT_TRUE(r.net.settings().hasNetwork());
    r.type("AT*WIFI=OFF\r");
    TEST_ASSERT_TRUE(r.net.settings().enabled);
    r.run(30000);
    TEST_ASSERT_FALSE(r.port.busy());
    TEST_ASSERT_TRUE(r.net.settings().enabled);
    r.type("AT*WIFIFORGET\r");
    r.type("Y\r");
    TEST_ASSERT_FALSE(r.net.settings().hasNetwork());
}

void test_browser_guard_rejects_stale_tick_and_keeps_full_window() {
    for (uint32_t start : {101u, 0xfffffff0u}) {
        Settings s = saved("HomeNet", "password1");
        NetRig r(&s);
        r.net.begin(start - 1);
        r.radio.up();
        r.net.tick(start - 1);
        r.net.guardBrowserChange(start);
        TEST_ASSERT_TRUE(r.net.setStatic(0xc0a80132, 0xffffff00, 0xc0a80101, 0xc0a80101));
        r.net.tick(start - 1);
        TEST_ASSERT_FALSE(r.net.settings().dhcp);
        r.net.tick(start + 59999u);
        TEST_ASSERT_FALSE(r.net.settings().dhcp);
        r.net.tick(start + 60000u);
        TEST_ASSERT_TRUE(r.net.settings().dhcp);
    }
}
void test_password_prompt_reserves_ssid_and_releases_on_cancel_timeout_disconnect() {
    CmdRig r;
    StrSink out;
    atcmd::Port second(r.modem, out, 38400);
    auto typeSecond = [&](const char *text) {
        out.s.clear();
        while (*text)
            second.feed(*text++);
    };
    r.type("AT*WIFIJOIN=\"FirstNet\"\r");
    typeSecond("AT*WIFIJOIN=\"SecondNet\"\r");
    CONTAINS(out.s, "ERROR");
    typeSecond("AT*WIFISCAN\r");
    CONTAINS(out.s, "ERROR");
    r.type("password1\r");
    TEST_ASSERT_EQUAL_STRING("FirstNet", r.radio.last.ssid);
    TEST_ASSERT_EQUAL(1, r.radio.connects);
    r.type("x");
    r.type("AT*WIFIJOIN=\"Cancelled\"\r\x1b");
    typeSecond("AT*WIFIJOIN=\"SecondNet\"\r");
    CONTAINS(out.s, "Password");
    second.feed(27);
    pollCommands();
    r.type("AT*WIFIJOIN=\"Timeout\"\r");
    r.run(120000);
    TEST_ASSERT_FALSE(r.port.busy());
    r.type("AT*WIFIJOIN=\"Detached\"\r");
    detachCommands(r.port);
    TEST_ASSERT_FALSE(r.port.busy());
    typeSecond("AT*WIFIJOIN=\"FinalNet\"\r");
    CONTAINS(out.s, "Password");
    typeSecond("password2\r");
    TEST_ASSERT_EQUAL_STRING("FinalNet", r.radio.last.ssid);
}
void test_invalid_password_releases_prompt_reservation() {
    CmdRig r;
    r.type("AT*WIFIJOIN=\"FirstNet\"\rshort\r");
    TEST_ASSERT_FALSE(r.port.busy());
    CONTAINS(r.type("AT*WIFIJOIN=\"NextNet\"\r"), "Password");
}
void test_all_failed_network_setters_keep_live_and_saved_state() {
    for (unsigned action = 0; action < 7; ++action) {
        Settings s = saved("HomeNet", "password1");
        s.dhcp = false;
        s.ip = 0xc0a8012a;
        s.mask = 0xffffff00;
        s.gateway = s.dns = 0xc0a80101;
        NetRig r(&s);
        r.net.begin(0);
        r.radio.up();
        r.net.tick(100);
        uint8_t before[kBlobSize], after[kBlobSize];
        serialize(r.net.settings(), before);
        int connects = r.radio.connects, leaves = r.radio.leaves, offs = r.radio.offs,
            services = r.radio.services;
        r.store.fail = true;
        bool result = true;
        switch (action) {
            case 0:
                result = r.net.setEnabled(false);
                break;
            case 1:
                result = r.net.forget();
                break;
            case 2:
                result = r.net.setDhcp();
                break;
            case 3:
                result = r.net.setStatic(0xc0a80132, 0xffffff00, 0xc0a80101, 0xc0a80101);
                break;
            case 4:
                result = r.net.setHost("other");
                break;
            case 5:
                result = r.net.setNtp("other.example");
                break;
            case 6:
                result = r.net.setTz("UTC");
                break;
        }
        TEST_ASSERT_FALSE(result);
        serialize(r.net.settings(), after);
        TEST_ASSERT_EQUAL_MEMORY(before, after, sizeof before);
        TEST_ASSERT_EQUAL(connects, r.radio.connects);
        TEST_ASSERT_EQUAL(leaves, r.radio.leaves);
        TEST_ASSERT_EQUAL(offs, r.radio.offs);
        TEST_ASSERT_EQUAL(services, r.radio.services);
        r.store.fail = false;
        TEST_ASSERT_TRUE(r.net.setNtp("saved.example"));
        Settings disk;
        TEST_ASSERT_EQUAL(LoadResult::kSaved, load(r.store, &disk));
        TEST_ASSERT_TRUE(disk.enabled);
        TEST_ASSERT_FALSE(disk.dhcp);
        TEST_ASSERT_EQUAL_HEX32(s.ip, disk.ip);
        TEST_ASSERT_EQUAL_STRING(s.ssid, disk.ssid);
        TEST_ASSERT_EQUAL_STRING(s.host, disk.host);
        TEST_ASSERT_EQUAL_STRING(s.tz, disk.tz);
    }
}
void test_wifi_off_during_browser_join_commits_only_after_successful_save() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.net.guardBrowserChange(100);
    TEST_ASSERT_TRUE(r.net.join("Candidate", "password2", 100));
    r.store.fail = true;
    TEST_ASSERT_FALSE(r.net.setEnabled(false));
    TEST_ASSERT_EQUAL(JoinResult::kRunning, r.net.joinResult());
    r.store.fail = false;
    TEST_ASSERT_TRUE(r.net.setEnabled(false));
    r.net.tick(101);
    TEST_ASSERT_FALSE(r.net.settings().enabled);
    TEST_ASSERT_EQUAL(JoinResult::kNone, r.net.joinResult());
    Settings disk;
    load(r.store, &disk);
    TEST_ASSERT_FALSE(disk.enabled);
    TEST_ASSERT_EQUAL_STRING("HomeNet", disk.ssid);
}

void test_browser_change_after_failed_browser_join_is_not_rolled_back() {
    Settings s = saved("HomeNet", "password1");
    CmdRig r(&s);
    r.radio.up();
    r.run(100);
    r.net.guardBrowserChange(gNow);
    TEST_ASSERT_TRUE(r.net.join("Other", "password2", gNow, 60000));
    r.radio.fail(Reason::kWrongPassword);
    r.net.tick(gNow + 10);  // the join fails; the browser has already gone
    detachCommands(r.port);
    r.radio.up();
    r.run(1000);
    r.net.guardBrowserChange(gNow);
    TEST_ASSERT_TRUE(r.net.setStatic(0x0a000032, 0xffffff00, 0x0a000001, 0x0a000001));
    r.run(1000);
    TEST_ASSERT_FALSE(r.net.settings().dhcp);
}

void test_wifi_off_in_browser_window_does_not_save_the_unverified_change() {
    Settings s = saved("HomeNet", "password1");
    NetRig r(&s);
    r.net.begin(0);
    r.radio.up();
    r.net.tick(1);
    r.net.guardBrowserChange(10);
    TEST_ASSERT_TRUE(r.net.setStatic(0x0a000032, 0xffffff00, 0x0a000001, 0x0a000001));
    TEST_ASSERT_TRUE(r.net.setEnabled(false));
    r.net.tick(70000);
    Settings disk;
    load(r.store, &disk);
    TEST_ASSERT_FALSE(disk.enabled);
    TEST_ASSERT_TRUE(disk.dhcp);
    TEST_ASSERT_TRUE(r.net.settings().dhcp);
}

void test_scan_list_replaces_control_bytes_in_names() {
    CmdRig r;
    r.radio.found = {entry("Evil\x1b[2J\x07Net", -54, 6, Security::kWpa2)};
    r.type("AT*WIFISCAN\r");
    r.radio.scanDone = true;
    std::string o = r.run(500);
    CONTAINS(o, "Evil?[2J?Net");
    NOT_CONTAINS(o, "\x1b[2J");
    NOT_CONTAINS(o, "\x07");
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_browser_guard_rejects_stale_tick_and_keeps_full_window);
    RUN_TEST(test_browser_change_after_failed_browser_join_is_not_rolled_back);
    RUN_TEST(test_wifi_off_in_browser_window_does_not_save_the_unverified_change);
    RUN_TEST(test_scan_list_replaces_control_bytes_in_names);
    RUN_TEST(test_password_prompt_reserves_ssid_and_releases_on_cancel_timeout_disconnect);
    RUN_TEST(test_invalid_password_releases_prompt_reservation);
    RUN_TEST(test_all_failed_network_setters_keep_live_and_saved_state);
    RUN_TEST(test_wifi_off_during_browser_join_commits_only_after_successful_save);

    RUN_TEST(test_browser_network_loss_actions_require_cancellable_confirmation);
    RUN_TEST(test_browser_join_storage_failure_restores_previous_before_reporting_success);
    RUN_TEST(test_browser_join_keeps_old_network_until_candidate_connects);
    RUN_TEST(test_browser_ip_change_rolls_back_after_sixty_seconds);
    RUN_TEST(test_defaults);
    RUN_TEST(test_settings_round_trip);
    RUN_TEST(test_missing_or_damaged_settings_give_defaults);
    RUN_TEST(test_ssid_and_password_checks);
    RUN_TEST(test_hostname_rules);
    RUN_TEST(test_ip_parse_and_format);
    RUN_TEST(test_static_address_checks);
    RUN_TEST(test_calendar);
    RUN_TEST(test_parse_nz_rule);
    RUN_TEST(test_bad_rules_are_refused);
    RUN_TEST(test_nz_daylight_saving_changes);
    RUN_TEST(test_northern_zones_and_utc);
    RUN_TEST(test_resolve_gives_the_stored_name);
    RUN_TEST(test_format_local_time);
    RUN_TEST(test_nothing_saved_red_light_no_join);
    RUN_TEST(test_saved_network_flashes_green_then_solid);
    RUN_TEST(test_lost_wifi_reconnects_at_once);
    RUN_TEST(test_failed_attempts_back_off);
    RUN_TEST(test_attempt_with_no_answer_gives_up_after_30_s);
    RUN_TEST(test_switched_off_lights_off);
    RUN_TEST(test_forget_wipes_network_and_password);
    RUN_TEST(test_static_address_and_names_apply);
    RUN_TEST(test_clock_notice_once_set);
    RUN_TEST(test_join_saves_only_when_connected);
    RUN_TEST(test_failed_join_keeps_the_old_network);
    RUN_TEST(test_join_retries_a_missing_network_then_gives_up);
    RUN_TEST(test_join_with_no_answer_times_out);
    RUN_TEST(test_scan_sorts_and_merges);
    RUN_TEST(test_wifi_status_shows_password_set_not_the_password);
    RUN_TEST(test_wifi_on_off);
    RUN_TEST(test_scan_command_lists_later);
    RUN_TEST(test_scan_cancelled_by_a_key);
    RUN_TEST(test_join_by_number_with_masked_password);
    RUN_TEST(test_join_by_quoted_name_and_failure);
    RUN_TEST(test_join_open_network_from_scan_needs_no_password);
    RUN_TEST(test_join_refuses_bad_input);
    RUN_TEST(test_repeat_asks_for_the_password_again);
    RUN_TEST(test_wifiip_command);
    RUN_TEST(test_hostname_tz_ntp_commands);
    RUN_TEST(test_time_command);
    RUN_TEST(test_help_lists_wifi_and_clock_commands);
    RUN_TEST(test_password_never_shown);
    return UNITY_END();
}
