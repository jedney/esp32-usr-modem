// Unit tests for the host port logic (brief 5.4, 6.2): speed detection from "AT", 8N1
// decoding of captured waveforms, the +++ escape, DTR/DCD rules, flow control, the
// session modes and the front-panel LED rules. Run with: pio test -e native
#include <unity.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "autobaud.h"
#include "connection.h"
#include "hwtest.h"
#include "linectl.h"
#include "panel.h"
#include "serial_state.h"
#include "session.h"
#include "sink_gate.h"
#include "usb_names.h"

using autobaud::Run;
using modemcfg::Profile;
using namespace hostport;

void setUp() {}
void tearDown() {}

// --- Waveform generator -------------------------------------------------------------------
// Builds the runs the RMT receiver would capture for `text` sent 8N1 at `baud`. `clock`
// scales the sender's bit time (1.03 = 3 % slow); `jitterUs` moves each edge by up to
// that much (deterministic); edges are rounded to whole microseconds like the RMT.

struct Wave {
    double clock = 1.0;
    int jitterUs = 0;
    uint32_t idleBeforeUs = 0;
    unsigned gapBits = 0;  // idle bits between characters
};

std::vector<Run> wave(const std::string &text, uint32_t baud, const Wave &w = Wave()) {
    const double bitUs = 1e6 / baud * w.clock;
    std::vector<uint8_t> bits;  // one entry per bit time
    for (unsigned char c : text) {
        bits.push_back(0);
        for (int k = 0; k < 8; ++k) {
            bits.push_back((c >> k) & 1);
        }
        bits.push_back(1);
        for (unsigned g = 0; g < w.gapBits; ++g) {
            bits.push_back(1);
        }
    }
    // Edge times (us) where the level changes.
    std::vector<double> edges;
    std::vector<uint8_t> levels;
    uint32_t seed = 12345;
    for (size_t i = 0; i < bits.size(); ++i) {
        if (i == 0 || bits[i] != bits[i - 1]) {
            double t = static_cast<double>(i) * bitUs;
            if (w.jitterUs != 0 && i != 0) {
                seed = seed * 1103515245u + 12345u;
                int j = static_cast<int>((seed >> 16) % (2 * w.jitterUs + 1)) - w.jitterUs;
                t += j;
            }
            edges.push_back(t);
            levels.push_back(bits[i]);
        }
    }
    std::vector<Run> runs;
    if (w.idleBeforeUs != 0) {
        runs.push_back(Run{w.idleBeforeUs, 1});
    }
    for (size_t i = 0; i < edges.size(); ++i) {
        uint32_t us = 0;  // the last run lasts until the burst ends
        if (i + 1 < edges.size()) {
            us = static_cast<uint32_t>(std::lround(edges[i + 1]) - std::lround(edges[i]));
        }
        runs.push_back(Run{us, levels[i]});
    }
    if (runs.back().level == 0) {  // ended on a 0 data bit? can't: stop bits are 1
        runs.push_back(Run{0, 1});
    }
    return runs;
}

std::string decodeAt(const std::vector<Run> &r, uint32_t baud) {
    uint8_t out[512];
    size_t n = autobaud::decode(r.data(), r.size(), baud, out, sizeof out);
    return std::string(reinterpret_cast<char *>(out), n);
}

// --- Speed detection ---------------------------------------------------------------------

void test_detects_at_every_rate_from_any_other() {
    for (uint32_t rate : modemcfg::kHostRates) {
        std::vector<Run> r = wave("AT\r", rate);
        TEST_ASSERT_EQUAL_UINT32(rate, autobaud::rateOfA(r.data(), r.size()));
        uint32_t other = rate == 38400 ? 2400 : 38400;
        uint8_t out[16];
        autobaud::Burst b = autobaud::decodeBurst(r.data(), r.size(), other, out, sizeof out);
        TEST_ASSERT_TRUE(b.changed);
        TEST_ASSERT_EQUAL_UINT32(rate, b.baud);
        TEST_ASSERT_EQUAL_STRING_LEN("AT\r", reinterpret_cast<char *>(out), 3);
        TEST_ASSERT_EQUAL(3, b.len);
    }
}

void test_lower_case_at_is_detected() {
    for (uint32_t rate : {300u, 9600u, 115200u}) {
        std::vector<Run> r = wave("at\r", rate);
        TEST_ASSERT_EQUAL_UINT32(rate, autobaud::rateOfA(r.data(), r.size()));
    }
}

void test_detection_allows_clock_error_and_jitter() {
    for (double clock : {0.97, 1.03}) {
        Wave w;
        w.clock = clock;
        for (uint32_t rate : {300u, 2400u, 57600u, 115200u}) {
            std::vector<Run> r = wave("ATZ\r", rate, w);
            TEST_ASSERT_EQUAL_UINT32(rate, autobaud::rateOfA(r.data(), r.size()));
            TEST_ASSERT_EQUAL_STRING("ATZ\r", decodeAt(r, rate).c_str());
        }
    }
    Wave j;
    j.jitterUs = 2;  // the RMT's 1 us steps plus interrupt-free capture: well within this
    std::vector<Run> r = wave("AT&F1&W\r", 115200, j);
    TEST_ASSERT_EQUAL_UINT32(115200, autobaud::rateOfA(r.data(), r.size()));
    TEST_ASSERT_EQUAL_STRING("AT&F1&W\r", decodeAt(r, 115200).c_str());
}

void test_lone_a_switches_speed_for_typing() {
    std::vector<Run> r = wave("A", 1200);
    uint8_t out[4];
    autobaud::Burst b = autobaud::decodeBurst(r.data(), r.size(), 38400, out, sizeof out);
    TEST_ASSERT_TRUE(b.changed);
    TEST_ASSERT_EQUAL_UINT32(1200, b.baud);
    TEST_ASSERT_EQUAL(1, b.len);
    TEST_ASSERT_EQUAL('A', out[0]);
}

void test_other_text_does_not_change_speed() {
    uint8_t out[16];
    for (const char *s : {"AB\r", "aT\r", "XYZ\r", "+++"}) {
        std::vector<Run> r = wave(s, 2400);
        autobaud::Burst b = autobaud::decodeBurst(r.data(), r.size(), 38400, out, sizeof out);
        TEST_ASSERT_FALSE_MESSAGE(b.changed, s);
        TEST_ASSERT_EQUAL_UINT32(38400, b.baud);
    }
}

void test_same_speed_decodes_without_change() {
    std::vector<Run> r = wave("ATI3\r", 19200);
    uint8_t out[16];
    autobaud::Burst b = autobaud::decodeBurst(r.data(), r.size(), 19200, out, sizeof out);
    TEST_ASSERT_FALSE(b.changed);
    TEST_ASSERT_EQUAL_STRING_LEN("ATI3\r", reinterpret_cast<char *>(out), 5);
}

void test_nonstandard_speed_is_not_detected() {
    std::vector<Run> r = wave("AT\r", 74880);  // between 57600 and 115200
    TEST_ASSERT_EQUAL_UINT32(0, autobaud::rateOfA(r.data(), r.size()));
    TEST_ASSERT_EQUAL_UINT32(115200, autobaud::snapRate(1000000000ull / 115200 * 105 / 100));
    TEST_ASSERT_EQUAL_UINT32(0, autobaud::snapRate(1000000000ull / 115200 * 108 / 100));
}

void test_leading_idle_is_skipped() {
    Wave w;
    w.idleBeforeUs = 20000;
    std::vector<Run> r = wave("AT\r", 4800, w);
    TEST_ASSERT_EQUAL_UINT32(4800, autobaud::rateOfA(r.data(), r.size()));
    TEST_ASSERT_EQUAL_STRING("AT\r", decodeAt(r, 4800).c_str());
}

void test_long_line_decodes_exactly() {
    std::string line = "AT*";
    while (line.size() < 140) {
        line += "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    }
    line.resize(140);
    line += "\r";
    Wave w;
    w.jitterUs = 1;
    std::vector<Run> r = wave(line, 115200, w);
    TEST_ASSERT_EQUAL_STRING(line.c_str(), decodeAt(r, 115200).c_str());
}

void test_gaps_between_characters_decode() {
    Wave w;
    w.gapBits = 7;
    std::vector<Run> r = wave("ATDT1\r", 600, w);
    TEST_ASSERT_EQUAL_STRING("ATDT1\r", decodeAt(r, 600).c_str());
}

void test_all_byte_values_decode() {
    std::string all;
    for (int c = 1; c < 256; ++c) {
        all += static_cast<char>(c);
    }
    std::vector<Run> r = wave(all, 57600);
    TEST_ASSERT_EQUAL(255, decodeAt(r, 57600).size());
    TEST_ASSERT_TRUE(decodeAt(r, 57600) == all);
}

void test_break_is_dropped() {
    std::vector<Run> r = wave("A", 9600);
    r.back().us = 500;           // stop bit and a little idle
    r.push_back(Run{20000, 0});  // a 20 ms break (no stop bit)
    r.push_back(Run{1000, 1});   // idle
    std::vector<Run> t = wave("T", 9600);
    r.insert(r.end(), t.begin(), t.end());
    TEST_ASSERT_EQUAL_STRING("AT", decodeAt(r, 9600).c_str());
}

void test_wrong_speed_decode_is_harmless() {
    std::vector<Run> r = wave("ATZ\r", 300);
    std::string s = decodeAt(r, 115200);  // garbage, but no crash and bounded
    TEST_ASSERT_TRUE(s.size() < 20);
    uint8_t out[2];
    TEST_ASSERT_TRUE(autobaud::decode(r.data(), r.size(), 115200, out, sizeof out) <= 2);
    TEST_ASSERT_EQUAL(0, autobaud::decode(r.data(), 0, 9600, out, sizeof out));
}

// --- +++ escape ---------------------------------------------------------------------------

Profile f1() {
    return modemcfg::factoryProfile(1);
}

void plus(EscapeDetector &e, uint32_t t, const Profile &p, char c = '+') {
    e.onByte(static_cast<uint8_t>(c), t, p);
}

void test_escape_with_guard_times() {
    Profile p = f1();  // S2 = '+', S12 = 50 (1 s)
    EscapeDetector e;
    e.reset(0);
    plus(e, 1000, p);
    plus(e, 1100, p);
    plus(e, 1200, p);
    TEST_ASSERT_FALSE(e.poll(2199, p));
    TEST_ASSERT_TRUE(e.poll(2200, p));
    TEST_ASSERT_FALSE(e.poll(3000, p));  // once only
}

void test_escape_needs_guard_before() {
    Profile p = f1();
    EscapeDetector e;
    e.reset(0);
    plus(e, 500, p, 'x');
    plus(e, 1200, p);  // only 0.7 s after data
    plus(e, 1300, p);
    plus(e, 1400, p);
    TEST_ASSERT_FALSE(e.poll(5000, p));
}

void test_escape_cancelled_by_data_after() {
    Profile p = f1();
    EscapeDetector e;
    e.reset(0);
    plus(e, 1000, p);
    plus(e, 1100, p);
    plus(e, 1200, p);
    plus(e, 1500, p, 'x');
    TEST_ASSERT_FALSE(e.poll(5000, p));
}

void test_fourth_plus_cancels() {
    Profile p = f1();
    EscapeDetector e;
    e.reset(0);
    for (uint32_t t : {1000u, 1100u, 1200u, 1300u}) {
        plus(e, t, p);
    }
    TEST_ASSERT_FALSE(e.poll(5000, p));
}

void test_slow_plus_starts_again() {
    Profile p = f1();
    EscapeDetector e;
    e.reset(0);
    plus(e, 1000, p);
    plus(e, 2100, p);  // too slow for the first; starts a new sequence
    plus(e, 2200, p);
    plus(e, 2300, p);
    TEST_ASSERT_TRUE(e.poll(3300, p));
}

void test_escape_s12_zero_and_s2_off() {
    Profile p = f1();
    p.s[12] = 0;
    EscapeDetector e;
    e.reset(0);
    plus(e, 1, p, 'x');
    plus(e, 2, p);
    plus(e, 3, p);
    plus(e, 4, p);
    TEST_ASSERT_TRUE(e.poll(4, p));

    p = f1();
    p.s[2] = 128;  // escape off
    e.reset(0);
    plus(e, 1000, p);
    plus(e, 1100, p);
    plus(e, 1200, p);
    TEST_ASSERT_FALSE(e.poll(9000, p));

    p = f1();
    p.s[2] = '-';
    e.reset(0);
    plus(e, 1000, p, '-');
    plus(e, 1100, p, '-');
    plus(e, 1200, p, '-');
    TEST_ASSERT_TRUE(e.poll(2200, p));
}

// --- DTR, DCD, flow control ------------------------------------------------------------------

void test_dtr_filter_uses_s25() {
    Profile p = f1();  // S25 = 5 (50 ms)
    DtrFilter d;
    d.reset(true);
    TEST_ASSERT_FALSE(d.update(false, 0, p));
    TEST_ASSERT_FALSE(d.update(false, 49, p));
    TEST_ASSERT_TRUE(d.update(false, 50, p));
    TEST_ASSERT_FALSE(d.on());
    TEST_ASSERT_FALSE(d.update(false, 60, p));  // reported once

    d.reset(true);
    d.update(false, 100, p);
    d.update(true, 130, p);  // a 30 ms glitch
    TEST_ASSERT_FALSE(d.update(true, 200, p));
    TEST_ASSERT_TRUE(d.on());

    p.s[25] = 0;
    d.reset(true);
    TEST_ASSERT_TRUE(d.update(false, 0, p));
}

void test_dtr_actions_follow_amp_d() {
    Profile p = f1();
    p.v[modemcfg::kAmpD] = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(DtrAction::kNone), static_cast<int>(dtrAction(p)));
    p.v[modemcfg::kAmpD] = 2;
    TEST_ASSERT_EQUAL(static_cast<int>(DtrAction::kHangUp), static_cast<int>(dtrAction(p)));
    p.v[modemcfg::kAmpD] = 3;
    TEST_ASSERT_EQUAL(static_cast<int>(DtrAction::kReset), static_cast<int>(dtrAction(p)));
}

void test_dcd_follows_amp_c() {
    Profile p = f1();
    TEST_ASSERT_FALSE(dcdOn(p, false));
    TEST_ASSERT_TRUE(dcdOn(p, true));
    p.v[modemcfg::kAmpC] = 0;
    TEST_ASSERT_TRUE(dcdOn(p, false));
}

void test_flow_modes_of_the_factory_templates() {
    FlowMode f = flowMode(modemcfg::factoryProfile(1));  // &H1 &R2
    TEST_ASSERT_TRUE(f.ctsFromBuffer);
    TEST_ASSERT_TRUE(f.obeyRts);
    TEST_ASSERT_FALSE(f.sendXonXoff);
    TEST_ASSERT_FALSE(f.obeyXonXoff);

    f = flowMode(modemcfg::factoryProfile(0));  // &H0 &R1
    TEST_ASSERT_FALSE(f.ctsFromBuffer || f.obeyRts || f.sendXonXoff || f.obeyXonXoff);

    f = flowMode(modemcfg::factoryProfile(2));  // &H2 &R1 &I2
    TEST_ASSERT_FALSE(f.ctsFromBuffer || f.obeyRts);
    TEST_ASSERT_TRUE(f.sendXonXoff && f.obeyXonXoff && f.stripXonXoff);

    Profile p = f1();
    p.v[modemcfg::kAmpH] = 3;
    p.v[modemcfg::kAmpI] = 1;
    f = flowMode(p);
    TEST_ASSERT_TRUE(f.ctsFromBuffer && f.sendXonXoff && f.obeyXonXoff);
    TEST_ASSERT_FALSE(f.stripXonXoff);
    TEST_ASSERT_TRUE(flowMode(p) == f);
    TEST_ASSERT_TRUE(flowMode(f1()) != f);
}

void test_xoff_sender_thresholds() {
    Profile p = modemcfg::factoryProfile(2);
    XoffSender x;
    TEST_ASSERT_EQUAL(-1, x.update(10, 100, p));
    TEST_ASSERT_EQUAL(19, x.update(75, 100, p));  // XOFF
    TEST_ASSERT_TRUE(x.paused());
    TEST_ASSERT_EQUAL(-1, x.update(80, 100, p));
    TEST_ASSERT_EQUAL(-1, x.update(50, 100, p));
    TEST_ASSERT_EQUAL(17, x.update(25, 100, p));  // XON
    TEST_ASSERT_EQUAL(-1, x.update(0, 100, p));

    TEST_ASSERT_EQUAL(19, x.update(90, 100, p));
    TEST_ASSERT_EQUAL(17, x.update(90, 100, f1()));  // &H off while paused: release
    TEST_ASSERT_EQUAL(-1, x.update(90, 100, f1()));
}

// --- Echo call ----------------------------------------------------------------------------

void test_echo_connection() {
    EchoConnection c;
    TEST_ASSERT_FALSE(c.isOpen());
    TEST_ASSERT_EQUAL(0, c.space());
    c.open(0, 0);
    const uint8_t hello[] = {'h', 'i'};
    TEST_ASSERT_EQUAL(2, c.write(hello, 2));
    uint8_t buf[8];
    TEST_ASSERT_EQUAL(2, c.read(buf, sizeof buf));
    TEST_ASSERT_EQUAL('h', buf[0]);
    std::vector<uint8_t> big(5000, 'x');
    TEST_ASSERT_EQUAL(EchoConnection::kSize, c.write(big.data(), big.size()));
    TEST_ASSERT_EQUAL(0, c.space());
    c.poll(1000000);
    TEST_ASSERT_TRUE(c.isOpen());  // no timer
    c.open(100, 500);
    c.poll(599);
    TEST_ASSERT_TRUE(c.isOpen());
    c.poll(600);
    TEST_ASSERT_FALSE(c.isOpen());
}

// --- Session ------------------------------------------------------------------------------

class MemStore : public modemcfg::Store {
public:
    size_t load(uint8_t *buf, size_t cap) override {
        size_t n = len < cap ? len : cap;
        memcpy(buf, data, n);
        return n;
    }
    bool save(const uint8_t *d, size_t n) override {
        memcpy(data, d, n);
        len = n;
        return true;
    }
    bool erase() override {
        len = 0;
        return true;
    }
    uint8_t data[1024] = {};
    size_t len = 0;
};

class StrSink : public atcmd::Sink {
public:
    void write(const char *d, size_t n) override { s.append(d, n); }
    std::string take() {
        std::string r = s;
        s.clear();
        return r;
    }
    std::string s;
};

struct Rig {
    MemStore store;
    atcmd::Modem modem{store, nullptr, 0};
    CallSlot slot;
    StrSink out;
    Session s{modem, out, 38400, slot};
    EchoConnection echo;
    uint32_t now = 0;

    Rig() {
        modem.begin();
        s.tick(now, true);
    }
    void send(const std::string &text) {
        s.fromHost(reinterpret_cast<const uint8_t *>(text.data()), text.size(), now);
    }
    std::string fromCall() {
        uint8_t buf[8192];
        size_t n = s.toHost(buf, sizeof buf);
        return std::string(reinterpret_cast<char *>(buf), n);
    }
    void wait(uint32_t ms, bool dtr = true) {
        for (uint32_t i = 0; i < ms; i += 10) {
            now += 10;
            s.tick(now, dtr);
        }
    }
    void call() {
        echo.open(now, 0);
        TEST_ASSERT_TRUE(s.startCall(echo));
        out.take();
    }
    void escape() {
        wait(1100);
        send("+++");
        wait(1100);
    }
};

void test_session_call_echo_and_connect_text() {
    Rig r;
    r.echo.open(0, 0);
    TEST_ASSERT_TRUE(r.s.startCall(r.echo));
    TEST_ASSERT_EQUAL_STRING("\r\nCONNECT 14400\r\n", r.out.take().c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::kOnline), static_cast<int>(r.s.mode()));
    TEST_ASSERT_TRUE(r.s.callUp());
    TEST_ASSERT_TRUE(r.s.dcd());
    r.send("hello");
    TEST_ASSERT_EQUAL_STRING("hello", r.fromCall().c_str());
    TEST_ASSERT_EQUAL_STRING("", r.out.take().c_str());  // data doesn't reach the parser
}

void test_session_connect_follows_amp_n() {
    Rig r;
    r.send("AT&N6\r");  // 9600
    r.out.take();
    r.echo.open(0, 0);
    r.s.startCall(r.echo);
    TEST_ASSERT_EQUAL_STRING("\r\nCONNECT 9600\r\n", r.out.take().c_str());
}

void test_session_56k_and_auto_connect_after_escape() {
    for (const char *cmd : {"AT&N38\r", "AT&N0\r"}) {
        Rig r;
        r.send(cmd);
        r.out.take();
        r.echo.open(0, 0);
        TEST_ASSERT_TRUE(r.s.startCall(r.echo));
        TEST_ASSERT_EQUAL_STRING("\r\nCONNECT 56000\r\n", r.out.take().c_str());
        r.escape();
        r.out.take();
        r.send("ATO\r");
        TEST_ASSERT_NOT_NULL(strstr(r.out.take().c_str(), "CONNECT 56000"));
    }
}

void test_session_escape_ato_and_ath() {
    Rig r;
    r.call();
    r.escape();
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", r.out.take().c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::kOnlineCommand), static_cast<int>(r.s.mode()));
    TEST_ASSERT_TRUE(r.s.dcd());                         // still connected
    TEST_ASSERT_EQUAL_STRING("", r.fromCall().c_str());  // call data is held meanwhile
    r.send("ATI4\r");
    TEST_ASSERT_NOT_NULL(strstr(r.out.take().c_str(), "OFF HOOK"));
    r.send("ATO\r");
    TEST_ASSERT_NOT_NULL(strstr(r.out.take().c_str(), "CONNECT 14400"));
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::kOnline), static_cast<int>(r.s.mode()));
    TEST_ASSERT_EQUAL_STRING("+++", r.fromCall().c_str());  // the escape went to the call
    r.escape();
    r.out.take();
    r.send("ATH\r");
    TEST_ASSERT_NOT_NULL(strstr(r.out.take().c_str(), "OK"));
    TEST_ASSERT_FALSE(r.s.callUp());
    TEST_ASSERT_FALSE(r.s.dcd());
    TEST_ASSERT_FALSE(r.echo.isOpen());
    r.send("ATO\r");
    TEST_ASSERT_NOT_NULL(strstr(r.out.take().c_str(), "ERROR"));  // no call
    r.send("ATI4\r");
    TEST_ASSERT_NOT_NULL(strstr(r.out.take().c_str(), "ON HOOK"));
}

void test_session_plus_without_guard_is_data() {
    Rig r;
    r.call();
    r.send("a+++");
    r.wait(2000);
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::kOnline), static_cast<int>(r.s.mode()));
    TEST_ASSERT_EQUAL_STRING("a+++", r.fromCall().c_str());
}

void test_session_dtr_drop() {
    {
        Rig r;  // &D2: hang up
        r.call();
        r.wait(100, false);
        TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n", r.out.take().c_str());
        TEST_ASSERT_FALSE(r.s.callUp());
        TEST_ASSERT_EQUAL(static_cast<int>(Mode::kCommand), static_cast<int>(r.s.mode()));
    }
    {
        Rig r;  // &D0: ignored
        r.send("AT&D0\r");
        r.call();
        r.wait(100, false);
        TEST_ASSERT_TRUE(r.s.callUp());
    }
    {
        Rig r;  // &D3: hang up and reset (ATZ)
        r.send("AT&D3S7=45\r");
        r.call();
        r.wait(100, false);
        TEST_ASSERT_FALSE(r.s.callUp());
        TEST_ASSERT_EQUAL(60, r.modem.active().s[7]);
        TEST_ASSERT_EQUAL(2, r.modem.active().get(modemcfg::kAmpD));
    }
    {
        Rig r;  // a DTR glitch shorter than S25 does nothing
        r.call();
        r.now += 10;
        r.s.tick(r.now, false);
        r.now += 20;
        r.s.tick(r.now, true);
        r.wait(100);
        TEST_ASSERT_TRUE(r.s.callUp());
    }
}

void test_session_far_end_hangs_up() {
    Rig r;
    r.echo.open(r.now, 500);
    r.s.startCall(r.echo);
    r.out.take();
    r.wait(600);
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n", r.out.take().c_str());
    TEST_ASSERT_FALSE(r.s.dcd());
}

void test_one_call_at_a_time() {
    Rig r;
    StrSink out2;
    Session other(r.modem, out2, 115200, r.slot);
    r.call();
    EchoConnection e2;
    e2.open(0, 0);
    TEST_ASSERT_FALSE(other.startCall(e2));
    TEST_ASSERT_FALSE(r.s.startCall(e2));  // this port already has one
    r.escape();
    r.send("ATH\r");
    TEST_ASSERT_TRUE(other.startCall(e2));
}

void test_session_local_echo_f0() {
    Rig r;
    r.send("ATF0\r");
    r.call();
    r.send("ab");
    TEST_ASSERT_EQUAL_STRING("abab", r.fromCall().c_str());
}

void test_session_xon_xoff_from_computer() {
    Rig r;
    r.send("AT&F2\r");  // &I2
    r.call();
    r.send(
        "\x13"
        "ab");  // XOFF, then data
    TEST_ASSERT_TRUE(r.s.heldByXoff());
    TEST_ASSERT_EQUAL_STRING("", r.fromCall().c_str());
    r.send("\x11");                                        // XON
    TEST_ASSERT_EQUAL_STRING("ab", r.fromCall().c_str());  // XON/XOFF not passed on
}

void test_session_full_call_holds_the_rest() {
    Rig r;
    r.call();
    std::vector<uint8_t> big(5000, 'x');
    TEST_ASSERT_EQUAL(EchoConnection::kSize, r.s.fromHost(big.data(), big.size(), r.now));
    TEST_ASSERT_EQUAL(0, r.s.hostSpace());
    r.fromCall();
    TEST_ASSERT_EQUAL(EchoConnection::kSize, r.s.hostSpace());
}

void test_session_atz_hangs_up() {
    Rig r;
    r.call();
    r.escape();
    r.out.take();
    r.send("ATZ\r");
    TEST_ASSERT_FALSE(r.s.callUp());
    TEST_ASSERT_NOT_NULL(strstr(r.out.take().c_str(), "OK"));
}

void test_session_amp_c0_dcd_always_on() {
    Rig r;
    r.send("AT&C0\r");
    TEST_ASSERT_TRUE(r.s.dcd());
}

// --- Front-panel LEDs ---------------------------------------------------------------------

uint8_t mask(hwtest::Led l) {
    return static_cast<uint8_t>(1u << l);
}

void test_panel_mirrors_lines() {
    Panel p;
    PanelInput in = {};
    TEST_ASSERT_EQUAL_HEX8(0, p.update(in, 0));
    in.autoAnswer = true;
    in.cd = true;
    in.tr = true;
    in.cs = true;
    TEST_ASSERT_EQUAL_HEX8(
        mask(hwtest::kAA) | mask(hwtest::kCD) | mask(hwtest::kTR) | mask(hwtest::kCS),
        p.update(in, 10));
}

void test_panel_stretches_rd_and_sd() {
    Panel p;
    PanelInput in = {};
    in.toComputer = 100;  // counts before the first update don't flash
    TEST_ASSERT_EQUAL_HEX8(0, p.update(in, 0));
    in.toComputer = 101;
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kRD), p.update(in, 1000));
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kRD), p.update(in, 1049));
    TEST_ASSERT_EQUAL_HEX8(0, p.update(in, 1050));
    in.fromComputer = 1;
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kSD), p.update(in, 2000));
    in.fromComputer = 2;
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kSD), p.update(in, 2040));
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kSD), p.update(in, 2080));  // restarted at 2040
    TEST_ASSERT_EQUAL_HEX8(0, p.update(in, 2090));
}

void test_panel_follows_either_host_port() {
    // RS232 idle, USB in a call: CD and TR light, and USB bytes flash RD/SD.
    auto input = [](HostPortState rs232, HostPortState usb) {
        PanelInput in = {};
        addHostPort(in, rs232);
        addHostPort(in, usb);
        return in;
    };
    Panel p;
    TEST_ASSERT_EQUAL_HEX8(0, p.update(input({false, false, 5, 7}, {false, false, 0, 0}), 0));
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kCD) | mask(hwtest::kTR),
                           p.update(input({false, false, 5, 7}, {true, true, 0, 0}), 10));
    TEST_ASSERT_EQUAL_HEX8(
        mask(hwtest::kCD) | mask(hwtest::kTR) | mask(hwtest::kRD) | mask(hwtest::kSD),
        p.update(input({false, false, 5, 7}, {true, true, 3, 2}), 20));
    // RS232 alone still works as before.
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kTR),
                           p.update(input({false, true, 5, 7}, {false, false, 3, 2}), 1000));
}

void test_panel_ring_and_wifi() {
    Panel p;
    PanelInput in = {};
    in.ringing = true;
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kAA), p.update(in, 0));
    TEST_ASSERT_EQUAL_HEX8(0, p.update(in, Panel::kRingFlashMs));
    in.ringing = false;
    in.wifi = WifiLed::kConnecting;
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kARQ), p.update(in, 0));
    TEST_ASSERT_EQUAL_HEX8(0, p.update(in, Panel::kWifiBlinkMs));
    in.wifi = WifiLed::kConnected;
    TEST_ASSERT_EQUAL_HEX8(mask(hwtest::kARQ), p.update(in, Panel::kWifiBlinkMs));
}

// --- Sink gate: a port nobody reads never stops the modem ------------------------
// A fake port with a fake clock. pause() is the only thing that moves time; the computer
// frees one byte of room every `drainEveryMs` (0 = never reads).

struct FakeDevice : SinkGate::Device {
    uint32_t t = 0;
    size_t free = 0;
    uint32_t drainEveryMs = 0;
    unsigned pauses = 0;
    std::string sent;
    size_t room() override { return free; }
    size_t put(const uint8_t *data, size_t len) override {
        size_t n = len < free ? len : free;
        sent.append(reinterpret_cast<const char *>(data), n);
        free -= n;
        return n;
    }
    uint32_t now() override { return t; }
    void pause() override {
        ++pauses;
        ++t;
        if (drainEveryMs && t % drainEveryMs == 0) {
            ++free;
        }
    }
};

void test_sink_gate_drops_unsolicited_at_once_when_full() {
    SinkGate gate;
    FakeDevice dev;
    TEST_ASSERT_EQUAL(0, gate.write(dev, "\r\nRING\r\n", 8, true));
    TEST_ASSERT_EQUAL(0, dev.t);  // no time passes in the drop path
    TEST_ASSERT_EQUAL(0, dev.pauses);
    // Some room but not enough: dropped whole, never half a result.
    dev.free = 5;
    TEST_ASSERT_EQUAL(0, gate.write(dev, "\r\nRING\r\n", 8, true));
    TEST_ASSERT_EQUAL(0, dev.t);
    TEST_ASSERT_TRUE(dev.sent.empty());
    TEST_ASSERT_FALSE(gate.stalled());  // only a reply marks the port stalled
    // Room for all of it: written.
    dev.free = 8;
    TEST_ASSERT_EQUAL(8, gate.write(dev, "\r\nRING\r\n", 8, true));
    TEST_ASSERT_EQUAL_STRING("\r\nRING\r\n", dev.sent.c_str());
}

void test_sink_gate_twenty_rings_to_a_full_port_cost_no_time() {
    SinkGate gate;
    FakeDevice dev;
    for (int i = 0; i < 20; ++i) {
        gate.write(dev, "\r\nRING\r\n", 8, true);
    }
    TEST_ASSERT_EQUAL(0, dev.t);
    TEST_ASSERT_EQUAL(0, dev.pauses);
}

void test_sink_gate_reply_waits_once_then_port_is_stalled() {
    SinkGate gate;
    FakeDevice dev;
    TEST_ASSERT_EQUAL(0, gate.write(dev, "\r\nOK\r\n", 6, false));
    TEST_ASSERT_EQUAL(SinkGate::kStallMs, dev.t);  // one bounded wait
    TEST_ASSERT_TRUE(gate.stalled());
    // From now on every write returns at once.
    const uint32_t before = dev.t;
    TEST_ASSERT_EQUAL(0, gate.write(dev, "\r\nOK\r\n", 6, false));
    TEST_ASSERT_EQUAL(0, gate.write(dev, "\r\nERROR\r\n", 9, false));
    TEST_ASSERT_EQUAL(0, gate.write(dev, "\r\nRING\r\n", 8, true));
    TEST_ASSERT_EQUAL(before, dev.t);
    TEST_ASSERT_TRUE(gate.stalled());
}

void test_sink_gate_room_clears_the_stall() {
    SinkGate gate;
    FakeDevice dev;
    gate.write(dev, "\r\nOK\r\n", 6, false);
    TEST_ASSERT_TRUE(gate.stalled());
    dev.free = 64;  // the computer reads again
    TEST_ASSERT_EQUAL(6, gate.write(dev, "\r\nOK\r\n", 6, false));
    TEST_ASSERT_FALSE(gate.stalled());
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", dev.sent.c_str());
}

void test_sink_gate_unsolicited_with_room_clears_the_stall() {
    SinkGate gate;
    FakeDevice dev;
    gate.write(dev, "\r\nOK\r\n", 6, false);
    dev.free = 64;
    TEST_ASSERT_EQUAL(8, gate.write(dev, "\r\nRING\r\n", 8, true));
    TEST_ASSERT_FALSE(gate.stalled());
}

void test_sink_gate_partial_reply_then_stall() {
    SinkGate gate;
    FakeDevice dev;
    dev.free = 4;
    TEST_ASSERT_EQUAL(4, gate.write(dev, "HELLO WORLD", 11, false));
    TEST_ASSERT_EQUAL_STRING("HELL", dev.sent.c_str());
    TEST_ASSERT_EQUAL(SinkGate::kStallMs, dev.t);
    TEST_ASSERT_TRUE(gate.stalled());
}

void test_sink_gate_slow_reader_gets_the_whole_reply() {
    // A computer that reads slowly (one byte per 100 ms, slower than 110 bps) is not a
    // stalled port: each new byte of room restarts the stall wait.
    SinkGate gate;
    FakeDevice dev;
    dev.drainEveryMs = 100;
    TEST_ASSERT_EQUAL(10, gate.write(dev, "0123456789", 10, false));
    TEST_ASSERT_EQUAL_STRING("0123456789", dev.sent.c_str());
    TEST_ASSERT_FALSE(gate.stalled());
    TEST_ASSERT_EQUAL(1000, dev.t);
}

void test_sink_gate_one_write_never_takes_longer_than_the_cap() {
    SinkGate gate;
    FakeDevice dev;
    dev.drainEveryMs = 100;
    char text[64];
    memset(text, 'x', sizeof text);
    size_t n = gate.write(dev, text, sizeof text, false);
    TEST_ASSERT_EQUAL(SinkGate::kWriteMs / 100, n);
    TEST_ASSERT_EQUAL(SinkGate::kWriteMs, dev.t);
    TEST_ASSERT_FALSE(gate.stalled());  // it was still reading: not stalled
}

void test_sink_gate_port_that_takes_nothing_does_not_spin() {
    // room() says yes but put() takes nothing: treated as no room, with pauses.
    struct Liar : FakeDevice {
        size_t room() override { return 16; }
        size_t put(const uint8_t *, size_t) override { return 0; }
    } dev;
    SinkGate gate;
    TEST_ASSERT_EQUAL(0, gate.write(dev, "OK", 2, false));
    TEST_ASSERT_EQUAL(SinkGate::kStallMs, dev.t);
    TEST_ASSERT_TRUE(gate.stalled());
}

void test_sink_gate_empty_write_is_free() {
    SinkGate gate;
    FakeDevice dev;
    TEST_ASSERT_EQUAL(0, gate.write(dev, "", 0, false));
    TEST_ASSERT_EQUAL(0, dev.t);
    TEST_ASSERT_FALSE(gate.stalled());
}

void test_call_slot_says_who_holds_it() {
    CallSlot slot;
    int a = 0, b = 0;
    TEST_ASSERT_FALSE(slot.heldBy(&a));
    TEST_ASSERT_FALSE(slot.heldBy(nullptr));  // an idle slot is not held by "nobody"
    TEST_ASSERT_TRUE(slot.claim(&a));
    TEST_ASSERT_TRUE(slot.heldBy(&a));
    TEST_ASSERT_FALSE(slot.heldBy(&b));
    TEST_ASSERT_TRUE(slot.transfer(&a, &b));
    TEST_ASSERT_FALSE(slot.heldBy(&a));
    TEST_ASSERT_TRUE(slot.heldBy(&b));
    slot.release(&b);
    TEST_ASSERT_FALSE(slot.heldBy(&b));
}

// --- The 2^32 ms clock wrap, throttle rates, S3/S5 limits -------------------------------
constexpr uint32_t kNearWrap = 0xffffff00u;  // 256 ms before the clock wraps to 0

void test_escape_guard_times_hold_across_the_clock_wrap() {
    Profile p = f1();  // S12 = 50 (1 s)
    EscapeDetector e;
    e.reset(0xfffffa00u);
    plus(e, kNearWrap, p);  // 1280 ms of quiet before it
    plus(e, kNearWrap + 100, p);
    plus(e, kNearWrap + 200, p);  // wraps: 0x000000c8
    TEST_ASSERT_FALSE(e.poll(kNearWrap + 200 + 999, p));
    TEST_ASSERT_TRUE(e.poll(kNearWrap + 200 + 1000, p));
    TEST_ASSERT_FALSE(e.poll(kNearWrap + 200 + 5000, p));  // once only

    // Data 768 ms before the first "+" (the wrap lies between them): not enough guard time.
    e.reset(0xfffffe00u);
    plus(e, kNearWrap, p, 'x');
    plus(e, kNearWrap + 0x300, p);
    plus(e, kNearWrap + 0x310, p);
    plus(e, kNearWrap + 0x320, p);
    TEST_ASSERT_FALSE(e.poll(kNearWrap + 0x320 + 5000, p));

    // Too slow between the "+" characters across the wrap: starts a new sequence.
    e.reset(0xfffffa00u);
    plus(e, kNearWrap, p);
    plus(e, kNearWrap + 1100, p);
    plus(e, kNearWrap + 1200, p);
    plus(e, kNearWrap + 1300, p);
    TEST_ASSERT_TRUE(e.poll(kNearWrap + 2300, p));
}

void test_session_escape_works_when_the_clock_wraps_during_the_guard_time() {
    Rig r;
    r.now = 0xfffffa00u;
    r.s.tick(r.now, true);
    r.call();
    r.wait(1100);  // guard time before; now = 0x0000004c is past the wrap
    r.send("+++");
    r.wait(1100);
    TEST_ASSERT_LESS_THAN_UINT32(0x10000u, r.now);
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", r.out.take().c_str());
    TEST_ASSERT_EQUAL(static_cast<int>(Mode::kOnlineCommand), static_cast<int>(r.s.mode()));
}

void test_dtr_hold_holds_across_the_clock_wrap() {
    Profile p = f1();  // S25 = 5 (50 ms)
    DtrFilter d;
    d.reset(true);
    TEST_ASSERT_FALSE(d.update(false, 0xffffffe0u, p));
    TEST_ASSERT_FALSE(d.update(false, 0xffffffe0u + 49, p));  // 0x00000011
    TEST_ASSERT_TRUE(d.update(false, 0xffffffe0u + 50, p));
    TEST_ASSERT_FALSE(d.on());
    // A glitch shorter than the hold, with the wrap in the middle, is ignored.
    d.reset(true);
    d.update(false, 0xfffffff0u, p);
    d.update(true, 0xfffffff0u + 30, p);
    TEST_ASSERT_FALSE(d.update(true, 0xfffffff0u + 200, p));
    TEST_ASSERT_TRUE(d.on());

    Rig r;  // and through a whole session: DTR low for 100 ms over the wrap hangs up
    r.now = 0xffffffd0u;
    r.s.tick(r.now, true);
    r.call();
    r.wait(100, false);
    TEST_ASSERT_FALSE(r.s.callUp());
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n", r.out.take().c_str());
}

// Bytes the throttle lets through after `ms` of quiet, starting at `start`.
size_t throttled(uint32_t speed, uint32_t start, uint32_t ms) {
    Rig r;
    TEST_ASSERT_TRUE(r.modem.setThrottle(true));
    r.now = start;
    r.s.tick(r.now, true);
    r.echo.open(r.now, 0);
    TEST_ASSERT_TRUE(r.s.startCall(r.echo, speed));
    r.out.take();
    r.wait(ms);
    static uint8_t data[4096];
    memset(data, 'x', sizeof data);
    return r.s.fromHost(data, sizeof data, r.now);
}

void test_throttle_allows_speed_over_ten_bytes_a_second_at_any_rate() {
    // 10 bits a byte: 2400 bps = 240 B/s, so 50 ms = 12 bytes and the burst cap is 24 bytes.
    TEST_ASSERT_EQUAL(12, throttled(2400, 5000, 50));
    TEST_ASSERT_EQUAL(24, throttled(2400, 5000, 100));
    TEST_ASSERT_EQUAL(24, throttled(2400, 5000, 2000));
    // 115200 bps = 11520 B/s, so 10 ms = 115 bytes; the cap is 1152 bytes.
    TEST_ASSERT_EQUAL(115, throttled(115200, 5000, 10));
    TEST_ASSERT_EQUAL(1152, throttled(115200, 5000, 100));
    TEST_ASSERT_EQUAL(1152, throttled(115200, 5000, 2000));
    // 300 bps: 30 B/s; the cap is 3 bytes.
    TEST_ASSERT_EQUAL(3, throttled(300, 5000, 1000));
}

void test_throttle_credit_is_the_same_across_the_clock_wrap() {
    TEST_ASSERT_EQUAL(12, throttled(2400, 0xffffffe0u, 50));  // 0xffffffe0 + 50 wraps
    TEST_ASSERT_EQUAL(24, throttled(2400, 0xffffffe0u, 2000));
    TEST_ASSERT_EQUAL(115, throttled(115200, 0xfffffffcu, 10));  // wraps after 4 ms
    TEST_ASSERT_EQUAL(1152, throttled(115200, 0xfffffffcu, 2000));
    TEST_ASSERT_EQUAL(throttled(9600, 1000, 70), throttled(9600, 0xffffffe0u, 70));
}

// S3 (end of line) and S5 (backspace) at both ends of their range, on the host port.
void test_s3_zero_and_127_end_the_line_on_the_host_port() {
    for (unsigned s3 : {0u, 127u}) {
        Rig r;
        char set[16];
        snprintf(set, sizeof set, "ATS3=%u\r", s3);
        r.send(set);
        r.out.take();
        TEST_ASSERT_EQUAL(s3, r.modem.active().s[3]);
        r.send(std::string("ATX3") + char(s3));
        const std::string out = r.out.take();
        TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("OK", 4));  // after the echo
        TEST_ASSERT_EQUAL(3, r.modem.active().get(modemcfg::kX));
        TEST_ASSERT_EQUAL(char(s3), out[out.size() - 2]);  // results end with S3, S4
    }
}

void test_s5_zero_and_127_erase_one_character_on_the_host_port() {
    for (unsigned s5 : {0u, 127u}) {
        Rig r;
        char set[16];
        snprintf(set, sizeof set, "ATS5=%u\r", s5);
        r.send(set);
        r.out.take();
        TEST_ASSERT_EQUAL(s5, r.modem.active().s[5]);
        r.send(std::string("ATX5") + char(s5) + "3\r");  // X5 typed, erased, X3 kept
        r.out.take();
        TEST_ASSERT_EQUAL(3, r.modem.active().get(modemcfg::kX));
    }
}
// --- USB serial state (DCD, DSR, RI notifications) ---------------------------------------

class FakeStatePort : public SerialStateSender::Port {
public:
    std::vector<SerialState> sent;
    bool busy = false;
    bool send(const SerialState &state) override {
        if (busy) {
            return false;
        }
        sent.push_back(state);
        return true;
    }
};

SerialState lines(bool dcd, bool dsr, bool ri) {
    SerialState s;
    s.dcd = dcd;
    s.dsr = dsr;
    s.ri = ri;
    return s;
}

void test_serial_state_sent_on_configure_and_on_change_only() {
    FakeStatePort port;
    SerialStateSender sender;
    sender.poll(port, false, false, true, false);  // not configured: nothing goes
    TEST_ASSERT_EQUAL(0, port.sent.size());
    // Port closed (DTR off): still sent, as some programs open the port without DTR.
    sender.poll(port, true, false, false, false);
    TEST_ASSERT_EQUAL(1, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[0] == lines(false, true, false));  // DSR always on
    sender.poll(port, true, false, false, false);
    TEST_ASSERT_EQUAL(1, port.sent.size());  // no change, nothing sent
    sender.poll(port, true, false, true, false);
    TEST_ASSERT_EQUAL(2, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[1] == lines(true, true, false));
    sender.poll(port, true, false, false, false);
    TEST_ASSERT_EQUAL(3, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[2] == lines(false, true, false));
}

void test_serial_state_sent_again_when_a_program_opens_the_port() {
    FakeStatePort port;
    SerialStateSender sender;
    sender.poll(port, true, false, true, false);
    TEST_ASSERT_EQUAL(1, port.sent.size());
    sender.poll(port, true, true, true, false);  // DTR on: the same state again
    TEST_ASSERT_EQUAL(2, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[1] == lines(true, true, false));
    sender.poll(port, true, true, true, false);
    sender.poll(port, true, false, true, false);  // DTR off: nothing to send
    TEST_ASSERT_EQUAL(2, port.sent.size());
    sender.poll(port, true, true, true, false);  // opened again
    TEST_ASSERT_EQUAL(3, port.sent.size());
}

void test_serial_state_sent_again_after_the_computer_configures_again() {
    FakeStatePort port;
    SerialStateSender sender;
    sender.poll(port, true, true, true, false);
    sender.poll(port, false, false, true, false);  // unplugged or reset
    sender.poll(port, true, true, true, false);
    TEST_ASSERT_EQUAL(2, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[1] == lines(true, true, false));
}

void test_serial_state_busy_endpoint_keeps_the_newest_state() {
    FakeStatePort port;
    SerialStateSender sender;
    sender.poll(port, true, true, false, false);
    port.busy = true;
    sender.poll(port, true, true, true, false);   // DCD on: can't go now
    sender.poll(port, true, true, false, false);  // back off before it went
    TEST_ASSERT_EQUAL(1, port.sent.size());
    port.busy = false;
    sender.poll(port, true, true, false, false);
    TEST_ASSERT_EQUAL(1, port.sent.size());  // the computer already has DCD off
    port.busy = true;
    sender.poll(port, true, true, true, false);
    port.busy = false;
    sender.poll(port, true, true, true, false);
    TEST_ASSERT_EQUAL(2, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[1] == lines(true, true, false));
}

void test_serial_state_short_ring_still_reaches_the_computer() {
    FakeStatePort port;
    SerialStateSender sender;
    sender.poll(port, true, true, false, false);
    port.busy = true;
    sender.poll(port, true, true, false, true);  // RI on for one pass, can't go
    port.busy = false;
    sender.poll(port, true, true, false, false);  // RI is off again by now
    TEST_ASSERT_EQUAL(2, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[1] == lines(false, true, true));  // still sent as on
    sender.poll(port, true, true, false, false);
    TEST_ASSERT_EQUAL(3, port.sent.size());
    TEST_ASSERT_TRUE(port.sent[2] == lines(false, true, false));  // then off
    sender.poll(port, true, true, false, false);
    TEST_ASSERT_EQUAL(3, port.sent.size());
}

// The USB serial number is the Wi-Fi MAC as 12 upper-case hex digits, no separators.
void test_usb_serial_number_is_the_mac_in_hex() {
    const uint8_t mac[6] = {0x0A, 0xB1, 0x00, 0xFF, 0x3C, 0x7d};
    char out[hostport::kUsbSerialLength + 1];
    memset(out, 'x', sizeof out);
    hostport::usbSerialNumber(mac, out);
    TEST_ASSERT_EQUAL_STRING("0AB100FF3C7D", out);
}

void test_serial_state_ring_cadence_sends_each_edge() {
    FakeStatePort port;
    SerialStateSender sender;
    const bool cadence[] = {false, true, true, false, true, false, false};
    for (bool ri : cadence) {
        sender.poll(port, true, true, false, ri);
    }
    TEST_ASSERT_EQUAL(5, port.sent.size());  // first state, on, off, on, off
    TEST_ASSERT_FALSE(port.sent[0].ri);
    TEST_ASSERT_TRUE(port.sent[1].ri);
    TEST_ASSERT_FALSE(port.sent[2].ri);
    TEST_ASSERT_TRUE(port.sent[3].ri);
    TEST_ASSERT_FALSE(port.sent[4].ri);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_call_slot_says_who_holds_it);
    RUN_TEST(test_detects_at_every_rate_from_any_other);
    RUN_TEST(test_lower_case_at_is_detected);
    RUN_TEST(test_detection_allows_clock_error_and_jitter);
    RUN_TEST(test_lone_a_switches_speed_for_typing);
    RUN_TEST(test_other_text_does_not_change_speed);
    RUN_TEST(test_same_speed_decodes_without_change);
    RUN_TEST(test_nonstandard_speed_is_not_detected);
    RUN_TEST(test_leading_idle_is_skipped);
    RUN_TEST(test_long_line_decodes_exactly);
    RUN_TEST(test_gaps_between_characters_decode);
    RUN_TEST(test_all_byte_values_decode);
    RUN_TEST(test_break_is_dropped);
    RUN_TEST(test_wrong_speed_decode_is_harmless);
    RUN_TEST(test_escape_with_guard_times);
    RUN_TEST(test_escape_needs_guard_before);
    RUN_TEST(test_escape_cancelled_by_data_after);
    RUN_TEST(test_fourth_plus_cancels);
    RUN_TEST(test_slow_plus_starts_again);
    RUN_TEST(test_escape_s12_zero_and_s2_off);
    RUN_TEST(test_dtr_filter_uses_s25);
    RUN_TEST(test_dtr_actions_follow_amp_d);
    RUN_TEST(test_dcd_follows_amp_c);
    RUN_TEST(test_flow_modes_of_the_factory_templates);
    RUN_TEST(test_xoff_sender_thresholds);
    RUN_TEST(test_echo_connection);
    RUN_TEST(test_session_call_echo_and_connect_text);
    RUN_TEST(test_session_connect_follows_amp_n);
    RUN_TEST(test_session_56k_and_auto_connect_after_escape);
    RUN_TEST(test_session_escape_ato_and_ath);
    RUN_TEST(test_session_plus_without_guard_is_data);
    RUN_TEST(test_session_dtr_drop);
    RUN_TEST(test_session_far_end_hangs_up);
    RUN_TEST(test_one_call_at_a_time);
    RUN_TEST(test_session_local_echo_f0);
    RUN_TEST(test_session_xon_xoff_from_computer);
    RUN_TEST(test_session_full_call_holds_the_rest);
    RUN_TEST(test_session_atz_hangs_up);
    RUN_TEST(test_session_amp_c0_dcd_always_on);
    RUN_TEST(test_panel_mirrors_lines);
    RUN_TEST(test_panel_stretches_rd_and_sd);
    RUN_TEST(test_panel_ring_and_wifi);
    RUN_TEST(test_panel_follows_either_host_port);
    RUN_TEST(test_sink_gate_drops_unsolicited_at_once_when_full);
    RUN_TEST(test_sink_gate_twenty_rings_to_a_full_port_cost_no_time);
    RUN_TEST(test_sink_gate_reply_waits_once_then_port_is_stalled);
    RUN_TEST(test_sink_gate_room_clears_the_stall);
    RUN_TEST(test_sink_gate_unsolicited_with_room_clears_the_stall);
    RUN_TEST(test_sink_gate_partial_reply_then_stall);
    RUN_TEST(test_sink_gate_slow_reader_gets_the_whole_reply);
    RUN_TEST(test_sink_gate_one_write_never_takes_longer_than_the_cap);
    RUN_TEST(test_sink_gate_port_that_takes_nothing_does_not_spin);
    RUN_TEST(test_sink_gate_empty_write_is_free);
    RUN_TEST(test_escape_guard_times_hold_across_the_clock_wrap);
    RUN_TEST(test_session_escape_works_when_the_clock_wraps_during_the_guard_time);
    RUN_TEST(test_dtr_hold_holds_across_the_clock_wrap);
    RUN_TEST(test_throttle_allows_speed_over_ten_bytes_a_second_at_any_rate);
    RUN_TEST(test_throttle_credit_is_the_same_across_the_clock_wrap);
    RUN_TEST(test_s3_zero_and_127_end_the_line_on_the_host_port);
    RUN_TEST(test_s5_zero_and_127_erase_one_character_on_the_host_port);
    RUN_TEST(test_serial_state_sent_on_configure_and_on_change_only);
    RUN_TEST(test_serial_state_sent_again_when_a_program_opens_the_port);
    RUN_TEST(test_serial_state_sent_again_after_the_computer_configures_again);
    RUN_TEST(test_serial_state_busy_endpoint_keeps_the_newest_state);
    RUN_TEST(test_serial_state_short_ring_still_reaches_the_computer);
    RUN_TEST(test_serial_state_ring_cadence_sends_each_edge);
    RUN_TEST(test_usb_serial_number_is_the_mac_in_hex);
    return UNITY_END();
}
