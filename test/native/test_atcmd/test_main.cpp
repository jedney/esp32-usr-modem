// Unit tests for the AT command parser, result codes and ATI screens (spec 3.2, 3.5,
// 3.6, 3.7, 3.8). Run with: pio test -e native
#include <unity.h>

#include <cstring>
#include <string>

#define FAKES_STORE
#define FAKES_SINK
#include "../support/fakes.h"
#include "atcmd.h"

using namespace atcmd;
using modemcfg::Profile;

void setUp() {}
void tearDown() {}

// --- Test rig ---------------------------------------------------------------------------

using StrSink = fakes::TextSink;

std::string lastArgs;

Reply cmdVer(Port &port, const char *) {
    port.beginInfo();
    port.line("v9.9");
    return Reply::kOk;
}
Reply cmdHw(Port &, const char *args) {
    lastArgs = args;
    return Reply::kOk;
}
Reply cmdHelp(Port &port, const char *) {
    showStarHelp(port);
    return Reply::kOk;
}
Reply cmdOwn(Port &port, const char *) {
    port.print("own result\r\n");
    return Reply::kDone;
}
Reply cmdFail(Port &, const char *) {
    return Reply::kError;
}

// AT*SECRET asks for a secret; AT*LATER sends its result later (finish()).
std::string secretGot;
int secretCalls = 0;
int cancels = 0;
Reply onSecret(Port &port, const char *text, void *) {
    secretGot = text;
    ++secretCalls;
    if (strcmp(text, "later") == 0) {
        port.wait(nullptr, nullptr);
        return Reply::kPending;
    }
    return strlen(text) >= 3 ? Reply::kOk : Reply::kError;
}
Reply cmdSecret(Port &port, const char *) {
    port.askSecret("Secret: ", onSecret, nullptr);
    return Reply::kPending;
}
void onCancel(void *) {
    ++cancels;
}
Reply cmdLater(Port &port, const char *) {
    port.wait(onCancel, nullptr);
    return Reply::kPending;
}

const StarCommand kStar[] = {
    {"MODEL", "AT*MODEL=<14400|28800|33600|56000>", "model", atcmd::cmdModel, false},
    {"VER", "AT*VER", "firmware version", cmdVer, false},
    {"HELP", "AT*HELP", "this list", cmdHelp, false},
    {"?", "AT*?", "this list", cmdHelp, false},
    {"HW", "AT*HW...", "hardware tests", cmdHw, true},
    {"OWN", "AT*OWN", "prints its own result", cmdOwn, false},
    {"FAIL", "AT*FAIL", "always fails", cmdFail, false},
    {"SECRET", "AT*SECRET", "asks for a secret", cmdSecret, false},
    {"LATER", "AT*LATER", "result later", cmdLater, false},
};

struct Rig {
    fakes::MemoryStore &store;
    StrSink out;
    Modem modem;
    Port port;
    explicit Rig(fakes::MemoryStore &s)
        : store(s), modem(s, kStar, sizeof kStar / sizeof kStar[0]), port(modem, out, 115200) {
        modem.begin();
    }
    // Types `typed` into the port; returns what the modem sent back.
    std::string type(const char *typed) {
        out.text.clear();
        for (const char *p = typed; *p != '\0'; ++p) {
            port.feed(*p);
        }
        return out.text;
    }
    const Profile &a() const { return modem.active(); }
};

#define CONTAINS(haystack, needle) \
    TEST_ASSERT_TRUE_MESSAGE((haystack).find(needle) != std::string::npos, (haystack).c_str())
#define NOT_CONTAINS(haystack, needle) \
    TEST_ASSERT_TRUE_MESSAGE((haystack).find(needle) == std::string::npos, (haystack).c_str())

bool endsWith(const std::string &s, const std::string &tail) {
    return s.size() >= tail.size() && s.compare(s.size() - tail.size(), tail.size(), tail) == 0;
}
#define ENDS_WITH(s, tail) TEST_ASSERT_TRUE_MESSAGE(endsWith((s), (tail)), (s).c_str())

// --- Line input, echo and result formats -------------------------------------------------

void test_at_gives_ok_with_echo() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("AT\r\r\nOK\r\n", r.type("AT\r").c_str());
}

void test_lower_case_works_mixed_case_is_ignored() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("at\r"), "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_STRING("At\r", r.type("At\r").c_str());  // no result
    TEST_ASSERT_EQUAL_STRING("aT\r", r.type("aT\r").c_str());
}

void test_text_before_at_is_ignored() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("hello\rxxAT\r"), "\r\nOK\r\n");
}

void test_lf_after_cr_is_ignored() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("AT\r\r\nOK\r\n", r.type("AT\r\n").c_str());
}

void test_echo_off() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("ATE0\r"), "OK\r\n");
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", r.type("AT\r").c_str());
    r.type("ATE1\r");
    TEST_ASSERT_EQUAL(1, r.a().get(modemcfg::kE));
}

void test_numeric_results() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("ATV0\r0\r", r.type("ATV0\r").c_str());
    TEST_ASSERT_EQUAL_STRING("ATX9\r4\r", r.type("ATX9\r").c_str());
}

void test_quiet_mode() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_UINT32(0, r.port.errorCount());
    TEST_ASSERT_EQUAL_STRING("ATQ1\r", r.type("ATQ1\r").c_str());
    TEST_ASSERT_EQUAL_STRING("ATX9\r", r.type("ATX9\r").c_str());  // even ERROR is silent
    TEST_ASSERT_EQUAL_UINT32(1, r.port.errorCount());
    r.port.unsolicited(Result::kNoCarrier);
    TEST_ASSERT_EQUAL_UINT32(1, r.port.errorCount());
    ENDS_WITH(r.type("ATQ0\r"), "\r\nOK\r\n");
    TEST_ASSERT_EQUAL_UINT32(1, r.port.errorCount());
}

void test_quiet_still_shows_information() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATQ1\r");
    CONTAINS(r.type("ATI3\r"), "Sportster 14.4k Series");
}

void test_backspace_and_delete() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string out = r.type("ATE5\b0\r");
    CONTAINS(out, "\b \b");
    ENDS_WITH(out, "OK\r\n");
    TEST_ASSERT_EQUAL(0, r.a().get(modemcfg::kE));
    r.type(
        "ATE1X7\x7f"
        "3\r");
    TEST_ASSERT_EQUAL(3, r.a().get(modemcfg::kX));
}

void test_backspace_cannot_delete_at() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("AT\b\b\b\r"), "\r\nOK\r\n");
}

void test_repeat_last_line_with_a_slash() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATI3\r");
    std::string out = r.type("A/");  // no Enter
    CONTAINS(out, "Sportster 14.4k Series");
    ENDS_WITH(out, "OK\r\n");
    ENDS_WITH(r.type("a/"), "OK\r\n");
}

void test_a_slash_with_nothing_before_is_plain_ok() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("A/\r\r\nOK\r\n", r.type("A/").c_str());
}

void test_line_too_long_is_error() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string cmd = "AT" + std::string(kLineMax + 5, 'E') + "\r";
    ENDS_WITH(r.type(cmd.c_str()), "ERROR\r\n");
    std::string ok = "AT" + std::string(kLineMax, ' ') + "\r";
    ENDS_WITH(r.type(ok.c_str()), "OK\r\n");
}

void test_s3_and_s4_change_the_line_end() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATS3=30S4=33\r");
    std::string out = r.type("ATE0\x1e");
    TEST_ASSERT_EQUAL_STRING("ATE0\x1e\x1e!OK\x1e!", out.c_str());
    TEST_ASSERT_EQUAL_STRING("", r.type("AT\r").c_str());  // CR no longer ends a line
    r.type("\x1e");
    r.type("ATS3=13S4=10\x1e");
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", r.type("AT\r").c_str());
}

void test_s3_s4_s5_are_limited_to_0_127_and_s3_is_never_printable() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("ATS3=49\r"), "ERROR\r\n");  // '1' would end every line
    ENDS_WITH(r.type("ATS3=32\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATS3=126\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATS3=200\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATS4=128\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATS5=255\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATS4.7=1\r"), "ERROR\r\n");  // bit 7 would make 138
    ENDS_WITH(r.type("ATS4=127S4=10S5=0S5=8\r"), "OK\r\n");
    ENDS_WITH(r.type("ATS3?\r"), "013\r\n\r\nOK\r\n");
}

void test_s5_changes_backspace() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATS5=35\r");  // '#'
    r.type("ATX2#1\r");
    TEST_ASSERT_EQUAL(1, r.a().get(modemcfg::kX));
}

// --- Several commands per line --------------------------------------------------------------

void test_several_commands_per_line() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("ATE1 V1 X3 M2 L3 S7=45 &C0 &N16 Q0\r"), "\r\nOK\r\n");
    TEST_ASSERT_EQUAL(3, r.a().get(modemcfg::kX));
    TEST_ASSERT_EQUAL(2, r.a().get(modemcfg::kM));
    TEST_ASSERT_EQUAL(3, r.a().get(modemcfg::kL));
    TEST_ASSERT_EQUAL(45, r.a().s[7]);
    TEST_ASSERT_EQUAL(0, r.a().get(modemcfg::kAmpC));
    TEST_ASSERT_EQUAL(16, r.a().get(modemcfg::kAmpN));
}

void test_first_bad_command_stops_the_line() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("ATX3K1E0\r"), "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL(3, r.a().get(modemcfg::kX));  // before the error: done
    TEST_ASSERT_EQUAL(1, r.a().get(modemcfg::kE));  // after it: not run
}

void test_missing_number_is_zero() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATE\r");
    TEST_ASSERT_EQUAL(0, r.a().get(modemcfg::kE));
}

void test_out_of_range_settings_are_errors() {
    fakes::MemoryStore st;
    Rig r(st);
    const char *bad[] = {"ATX5\r",  "ATQ3\r",  "ATM4\r", "AT&N39\r", "AT&S1\r", "AT&D1\r",
                         "AT&R0\r", "AT&M2\r", "ATH2\r", "AT&Q0\r",  "ATB2\r"};
    for (const char *cmd : bad) {
        std::string out = r.type(cmd);
        TEST_ASSERT_TRUE_MESSAGE(endsWith(out, "ERROR\r\n"), cmd);
    }
}

void test_hang_up_and_dial_method() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("ATH0\r"), "OK\r\n");
    ENDS_WITH(r.type("ATH\r"), "OK\r\n");
    ENDS_WITH(r.type("ATP\r"), "OK\r\n");
    TEST_ASSERT_EQUAL(0, r.a().get(modemcfg::kTone));
    CONTAINS(r.type("ATI4\r"), "DIAL=PULSE  ON HOOK");
    r.type("ATT\r");
    TEST_ASSERT_EQUAL(1, r.a().get(modemcfg::kTone));
}

void test_dial_answer_online_wait_for_later_versions() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("ATDT1\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATA\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATO\r"), "ERROR\r\n");  // no call
}

// A stand-in for lib/hostport's session.
class FakeLink : public Link {
public:
    explicit FakeLink(Port &p) : port(p) {}
    bool callUp() const override { return up; }
    void resumeOnline() override {
        ++resumes;
        port.result(Result::kConnect33600);
    }
    void hangUp() override {
        ++hangUps;
        up = false;
    }
    Port &port;
    bool up = true;
    int resumes = 0;
    int hangUps = 0;
};

void test_o_h_and_z_go_to_the_link() {
    fakes::MemoryStore st;
    Rig r(st);
    FakeLink link(r.port);
    r.port.setLink(&link);
    CONTAINS(r.type("ATI4\r"), "OFF HOOK");
    std::string out = r.type("ATO E0\r");  // the rest of the line is ignored
    TEST_ASSERT_EQUAL_STRING("ATO E0\r\r\nCONNECT 33600\r\n", out.c_str());
    TEST_ASSERT_EQUAL(1, link.resumes);
    TEST_ASSERT_EQUAL(1, r.a().get(modemcfg::kE));
    ENDS_WITH(r.type("ATO1\r"), "CONNECT 33600\r\n");
    ENDS_WITH(r.type("ATO2\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATH1\r"), "OK\r\n");
    TEST_ASSERT_EQUAL(0, link.hangUps);
    ENDS_WITH(r.type("ATH0\r"), "OK\r\n");
    TEST_ASSERT_EQUAL(1, link.hangUps);
    ENDS_WITH(r.type("ATO\r"), "ERROR\r\n");  // hung up
    link.up = true;
    ENDS_WITH(r.type("ATZ\r"), "OK\r\n");
    TEST_ASSERT_EQUAL(2, link.hangUps);
    CONTAINS(r.type("ATI4\r"), "ON HOOK");
}

void test_w_keeps_the_port_speed_in_the_template() {
    fakes::MemoryStore st;
    Rig r(st);
    r.port.setBaud(9600);
    r.type("AT&W0\r");
    TEST_ASSERT_EQUAL_UINT32(9600, r.modem.nvram().tmpl[0].baud);
    CONTAINS(r.type("ATI5\r"), "BAUD=9600  PARITY=N  WORDLEN=8");
    CONTAINS(r.type("ATI4\r"), "BAUD=9600  PARITY=N  WORDLEN=8");
    TEST_ASSERT_EQUAL_UINT32(38400, r.modem.nvram().tmpl[1].baud);  // Y1 untouched
}

void test_host_baud_is_stored_at_once() {
    fakes::MemoryStore st;
    {
        Rig r(st);
        TEST_ASSERT_EQUAL_UINT32(0, r.modem.hostBaud());
        TEST_ASSERT_TRUE(r.modem.setHostBaud(2400));
        TEST_ASSERT_FALSE(r.modem.setHostBaud(2401));
        TEST_ASSERT_FALSE(r.modem.setHostBaud(230400));
    }
    Rig after(st);
    TEST_ASSERT_EQUAL_UINT32(2400, after.modem.hostBaud());
    TEST_ASSERT_TRUE(after.modem.setHostBaud(0));
}

// --- S-registers ------------------------------------------------------------------------

void test_s_register_set_and_query() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("ATS7?\r\r\n060\r\n\r\nOK\r\n", r.type("ATS7?\r").c_str());
    r.type("ATS7=45\r");
    TEST_ASSERT_EQUAL(45, r.a().s[7]);
    ENDS_WITH(r.type("ATS7=256\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATS64=1\r"), "ERROR\r\n");
    TEST_ASSERT_EQUAL(45, r.a().s[7]);
}

void test_s_register_select_then_equals_and_query() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATS7\r");
    r.type("AT=30\r");
    TEST_ASSERT_EQUAL(30, r.a().s[7]);
    CONTAINS(r.type("AT?\r"), "\r\n030\r\n");
}

void test_s_register_bit() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATS13.7=1\r");
    TEST_ASSERT_EQUAL(128, r.a().s[13]);
    r.type("ATS13.0=1S13.7=0\r");
    TEST_ASSERT_EQUAL(1, r.a().s[13]);
    ENDS_WITH(r.type("ATS13.8=1\r"), "ERROR\r\n");
}

// --- Result codes -------------------------------------------------------------------------

void test_result_texts_and_numbers() {
    TEST_ASSERT_EQUAL_STRING("NO DIAL TONE", resultText(Result::kNoDialTone));
    TEST_ASSERT_EQUAL(6, static_cast<int>(Result::kNoDialTone));
    TEST_ASSERT_EQUAL(7, static_cast<int>(Result::kBusy));
    TEST_ASSERT_EQUAL(155, static_cast<int>(Result::kConnect33600));
    TEST_ASSERT_EQUAL_STRING("CONNECT 33600", resultText(Result::kConnect33600));
}

void test_x_levels() {
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect),
                      static_cast<int>(filterForX(Result::kConnect33600, 0)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect33600),
                      static_cast<int>(filterForX(Result::kConnect33600, 1)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kNoCarrier),
                      static_cast<int>(filterForX(Result::kBusy, 2)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kBusy),
                      static_cast<int>(filterForX(Result::kBusy, 3)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kNoDialTone),
                      static_cast<int>(filterForX(Result::kNoDialTone, 2)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kNoCarrier),
                      static_cast<int>(filterForX(Result::kNoDialTone, 3)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kNoDialTone),
                      static_cast<int>(filterForX(Result::kNoDialTone, 4)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kNoCarrier),
                      static_cast<int>(filterForX(Result::kNoAnswer, 1)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kNoAnswer),
                      static_cast<int>(filterForX(Result::kNoAnswer, 4)));
}

void test_port_result_uses_v_q_and_x() {
    fakes::MemoryStore st;
    Rig r(st);
    r.out.text.clear();
    r.port.result(Result::kBusy);
    TEST_ASSERT_EQUAL_STRING("\r\nBUSY\r\n", r.out.text.c_str());  // X4 by default
    r.type("ATX0V0\r");
    r.out.text.clear();
    r.port.result(Result::kBusy);
    TEST_ASSERT_EQUAL_STRING("3\r", r.out.text.c_str());
    r.type("ATX4\r");
    r.out.text.clear();
    r.port.result(Result::kConnect33600);
    TEST_ASSERT_EQUAL_STRING("155\r", r.out.text.c_str());
}

void test_connect_codes_for_speeds() {
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect33600), static_cast<int>(connectFor(33600)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect2400), static_cast<int>(connectFor(2400)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect), static_cast<int>(connectFor(300)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect33600), static_cast<int>(connectFor(33601)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect9600), static_cast<int>(connectFor(9601)));
    TEST_ASSERT_EQUAL_UINT32(0, lineSpeedForN(0));
    TEST_ASSERT_EQUAL_UINT32(300, lineSpeedForN(1));
    TEST_ASSERT_EQUAL_UINT32(14400, lineSpeedForN(8));
    TEST_ASSERT_EQUAL_UINT32(33600, lineSpeedForN(16));
    TEST_ASSERT_EQUAL_UINT32(0, lineSpeedForN(39));
}

void test_extended_line_speed_commands_and_results() {
    // Published USR V.90 &N indexes and numeric results. These deliberately are not
    // sorted by bps: &N17-&N20 fall below &N16.
    const struct {
        uint32_t bps;
        unsigned code;
    } speeds[] = {
        {300, 1},     {1200, 5},    {2400, 10},   {4800, 18},   {7200, 20},   {9600, 13},
        {12000, 21},  {14400, 25},  {16800, 43},  {19200, 85},  {21600, 91},  {24000, 99},
        {26400, 103}, {28800, 107}, {31200, 151}, {33600, 155}, {28000, 256}, {29333, 260},
        {30666, 264}, {32000, 268}, {33333, 180}, {34666, 272}, {36000, 276}, {37333, 184},
        {38666, 280}, {40000, 284}, {41333, 188}, {42666, 192}, {44000, 196}, {45333, 200},
        {46666, 204}, {48000, 208}, {49333, 212}, {50666, 216}, {52000, 220}, {53333, 224},
        {54666, 228}, {56000, 232},
    };
    fakes::MemoryStore st;
    Rig r(st);
    for (unsigned i = 0; i < sizeof speeds / sizeof speeds[0]; ++i) {
        char cmd[32];
        snprintf(cmd, sizeof cmd, "AT&N%u&U%u\r", i + 1, i + 1);
        ENDS_WITH(r.type(cmd), "OK\r\n");
        TEST_ASSERT_EQUAL(i + 1, r.a().get(modemcfg::kAmpN));
        TEST_ASSERT_EQUAL(i + 1, r.a().get(modemcfg::kAmpU));
        TEST_ASSERT_EQUAL_UINT32(speeds[i].bps, lineSpeedForN(i + 1));
        Result code = connectFor(speeds[i].bps);
        TEST_ASSERT_EQUAL(speeds[i].code, static_cast<unsigned>(code));
        char expected[32];
        snprintf(expected, sizeof expected, "CONNECT %u", static_cast<unsigned>(speeds[i].bps));
        TEST_ASSERT_EQUAL_STRING(i == 0 ? "CONNECT" : expected, resultText(code));
        r.type("ATX4V0\r");
        r.out.text.clear();
        r.port.result(code);
        snprintf(expected, sizeof expected, "%u\r", speeds[i].code);
        TEST_ASSERT_EQUAL_STRING(expected, r.out.text.c_str());
        TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect),
                          static_cast<int>(filterForX(code, 0)));
        TEST_ASSERT_EQUAL(static_cast<int>(code), static_cast<int>(filterForX(code, 4)));
        r.type("ATV1\r");
    }
    ENDS_WITH(r.type("AT&N39\r"), "ERROR\r\n");
    ENDS_WITH(r.type("AT&U39\r"), "ERROR\r\n");
    ENDS_WITH(r.type("AT&N64\r"), "ERROR\r\n");
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect33600), static_cast<int>(connectFor(33999)));
    TEST_ASSERT_EQUAL(static_cast<int>(Result::kConnect56000), static_cast<int>(connectFor(57600)));
    r.type("ATQ1\r");
    r.out.text.clear();
    r.port.result(Result::kConnect56000);
    TEST_ASSERT_EQUAL_STRING("", r.out.text.c_str());
}

// --- Factory, stored templates, reset ---------------------------------------------------

void test_ampersand_f_loads_factory_templates() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATX2S7=1\r");
    r.type("AT&F\r");  // = &F0, as on the Sportster
    TEST_ASSERT_TRUE(r.a() == modemcfg::factoryProfile(0));
    r.type("AT&F1\r");
    TEST_ASSERT_TRUE(r.a() == modemcfg::factoryProfile(1));
    r.type("AT&F2\r");
    TEST_ASSERT_TRUE(r.a() == modemcfg::factoryProfile(2));
    ENDS_WITH(r.type("AT&F3\r"), "ERROR\r\n");
}

void test_write_then_reset_and_restart() {
    fakes::MemoryStore st;
    {
        Rig r(st);
        ENDS_WITH(r.type("ATS7=45X3&W\r"), "OK\r\n");
        TEST_ASSERT_EQUAL(1, st.saves);
        r.type("ATS7=10\r");
        r.type("ATZ\r");
        TEST_ASSERT_EQUAL(45, r.a().s[7]);
    }
    Rig after(st);  // a restart: new modem on the same store
    TEST_ASSERT_EQUAL(45, after.a().s[7]);
    TEST_ASSERT_EQUAL(3, after.a().get(modemcfg::kX));
    CONTAINS(after.type("AT&V\r"), "S07=045");
}

void test_z_ignores_the_rest_of_the_line() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("ATZE0\r"), "\r\nOK\r\n");
    TEST_ASSERT_EQUAL(1, r.a().get(modemcfg::kE));
}

void test_z_numbers() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATZ2\r");  // NVRAM template 1 = &F2 as shipped
    TEST_ASSERT_TRUE(r.a() == modemcfg::factoryProfile(2));
    r.type("ATZ3\r");
    TEST_ASSERT_TRUE(r.a() == modemcfg::factoryProfile(0));
    r.type("ATZ4\r");
    TEST_ASSERT_TRUE(r.a() == modemcfg::factoryProfile(1));
    ENDS_WITH(r.type("ATZ6\r"), "ERROR\r\n");
}

void test_y_selects_power_on_template_and_saves() {
    fakes::MemoryStore st;
    {
        Rig r(st);
        r.type("AT&F2X1&W1Y1\r");
        TEST_ASSERT_EQUAL(1, r.modem.nvram().y);
    }
    Rig after(st);
    TEST_ASSERT_EQUAL(1, after.a().get(modemcfg::kX));
    TEST_ASSERT_EQUAL(2, after.a().get(modemcfg::kAmpH));  // from &F2
    CONTAINS(after.type("ATI4\r"), "X1  Y1");
    ENDS_WITH(after.type("ATY5\r"), "ERROR\r\n");
}

void test_w_out_of_range() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("AT&W2\r"), "ERROR\r\n");
    TEST_ASSERT_EQUAL(0, st.saves);
}

void test_stored_phone_numbers() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("AT&Z1=555-1234\r"), "OK\r\n");
    TEST_ASSERT_EQUAL_STRING("555-1234", r.modem.nvram().phone[1]);
    TEST_ASSERT_EQUAL_STRING("AT&Z1?\r\r\n555-1234\r\n\r\nOK\r\n", r.type("AT&Z1?\r").c_str());
    ENDS_WITH(r.type("AT&Z4=1\r"), "ERROR\r\n");
    std::string longer = "AT&Z0=" + std::string(modemcfg::kPhoneLen + 1, '9') + "\r";
    ENDS_WITH(r.type(longer.c_str()), "ERROR\r\n");
    std::string full = "AT&Z0=" + std::string(modemcfg::kPhoneLen, '9') + "\r";
    ENDS_WITH(r.type(full.c_str()), "OK\r\n");
    CONTAINS(r.type("ATI5\r"), "#1: 555-1234");
    r.type("AT&Z1=\r");  // empty clears it
    TEST_ASSERT_EQUAL_STRING("", r.modem.nvram().phone[1]);
}

// --- +FCLASS -----------------------------------------------------------------------------

void test_fclass() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("AT+FCLASS=0\r"), "\r\nOK\r\n");
    ENDS_WITH(r.type("at+fclass=0\r"), "\r\nOK\r\n");
    ENDS_WITH(r.type("AT+FCLASS=1\r"), "ERROR\r\n");
    ENDS_WITH(r.type("AT+FCLASS=2.0\r"), "ERROR\r\n");
    ENDS_WITH(r.type("AT+FCLASS=2\r"), "ERROR\r\n");
    ENDS_WITH(r.type("AT+FCLASS=\r"), "ERROR\r\n");
    TEST_ASSERT_EQUAL_STRING("AT+FCLASS?\r\r\n0\r\n\r\nOK\r\n", r.type("AT+FCLASS?\r").c_str());
    CONTAINS(r.type("AT+FCLASS=?\r"), "\r\n0\r\n");
    ENDS_WITH(r.type("AT+FAA=1\r"), "ERROR\r\n");
}

// --- ATI and settings screens -----------------------------------------------------------

void test_ati_identity_lines() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("ATI3\r\r\nSportster 14.4k Series\r\n\r\nOK\r\n",
                             r.type("ATI3\r").c_str());
    TEST_ASSERT_EQUAL_STRING("ATI0\r\r\n1441\r\n\r\nOK\r\n", r.type("ATI0\r").c_str());
    TEST_ASSERT_EQUAL_STRING("ATI2\r\r\nOK\r\n\r\nOK\r\n", r.type("ATI2\r").c_str());
    CONTAINS(r.type("ATI9\r"), "Sportster 14.4k Series");
    ENDS_WITH(r.type("ATI8\r"), "ERROR\r\n");
    ENDS_WITH(r.type("ATI10\r"), "ERROR\r\n");
}

void test_every_inquiry_0_to_7_answers() {
    fakes::MemoryStore st;
    Rig r(st);
    for (int i = 0; i <= 7; ++i) {
        char cmd[8];
        snprintf(cmd, sizeof cmd, "ATI%d\r", i);
        std::string out = r.type(cmd);
        TEST_ASSERT_TRUE_MESSAGE(endsWith(out, "\r\nOK\r\n"), cmd);
    }
}

void test_ati4_current_settings_layout() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string out = r.type("ATI4\r");
    const char *expected =
        "ATI4\r"
        "\r\n"
        "\r\n"
        "Sportster 14.4k Series Settings...\r\n"
        "\r\n"
        "B0  E1  F1  L2  M1  Q0  V1  X4  Y0\r\n"
        "BAUD=115200  PARITY=N  WORDLEN=8\r\n"
        "DIAL=TONE  ON HOOK\r\n"
        "\r\n"
        "&A0  &B1  &C1  &D2  &G0  &H1  &I0  &K1  &M4  &N8\r\n"
        "&P1  &R2  &S0  &T5  &U0  &Y1\r\n"
        "\r\n"
        "S00=000  S01=000  S02=043  S03=013  S04=010  S05=008  S06=002\r\n"
        "S07=060  S08=002  S09=006  S10=014  S11=070  S12=050  S13=000\r\n"
        "S15=000  S16=000  S18=000  S19=000  S21=010  S22=017  S23=019\r\n"
        "S25=005  S27=000  S28=008  S29=020  S30=000  S31=128  S32=002\r\n"
        "S33=000  S34=000  S36=014  S38=000\r\n"
        "\r\n"
        "LAST DIALED #:\r\n"
        "\r\n"
        "OK\r\n";
    TEST_ASSERT_EQUAL_STRING(expected, out.c_str());
}

void test_ati5_stored_settings_layout() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("AT&Z0=170064142064\r");
    std::string out = r.type("ATI5\r");
    CONTAINS(out,
             "\r\nSportster 14.4k Series NVRAM Settings...\r\n\r\nTemplate Y0\r\n\r\n"
             "DIAL=TONE  B0  E1  F1  L2  M1  Q0  V1  X4\r\n"
             "BAUD=38400  PARITY=N  WORDLEN=8\r\n\r\n"
             "&A0  &B1  &C1  &D2  &G0  &H1  &I0  &K1  &M4  &N8\r\n");
    CONTAINS(out, "S00=000  S02=043  S03=013  S04=010  S05=008  S06=002  S07=060\r\n");
    CONTAINS(out, "S38=000\r\n\r\n\r\nTemplate Y1\r\n");
    CONTAINS(out, "&A0  &B1  &C1  &D2  &G0  &H2  &I2  &K1  &M4  &N8\r\n&P1  &R1");
    CONTAINS(out, "\r\nSTORED PHONE #0: 170064142064\r\n#1:\r\n#2:\r\n#3:\r\n\r\nOK\r\n");
}

void test_ati6_and_ati7_layout() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string i6 = r.type("ATI6\r");
    CONTAINS(i6, "\r\n\r\nSportster 14.4k Series Link Diagnostics...\r\n\r\n");
    CONTAINS(i6, "Chars sent                    0      Chars Received                0\r\n");
    CONTAINS(i6, "Chars lost                    0\r\n");
    CONTAINS(i6, "Speed                  0/0\r\n");
    std::string i7 = r.type("ATI7\r");
    CONTAINS(i7,
             "\r\nConfiguration Profile...\r\n\r\nProduct type           US/Canada External\r\n");
    CONTAINS(i7, "DSP rev                N/A\r\n\r\nOK\r\n");
}

void test_several_inquiries_on_one_line() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string out = r.type("ATI3I7\r");
    CONTAINS(out, "14.4k Series\r\n\r\nConfiguration Profile...");
}

void test_ampersand_v_shows_current_and_stored() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string out = r.type("AT&V\r");
    CONTAINS(out, "Series Settings...");
    CONTAINS(out, "Series NVRAM Settings...");
    ENDS_WITH(out, "\r\nOK\r\n");
    ENDS_WITH(r.type("AT&V1\r"), "ERROR\r\n");
}

void test_numeric_mode_has_no_leading_blank_line() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATV0\r");
    TEST_ASSERT_EQUAL_STRING("ATI3\rSportster 14.4k Series\r\n0\r", r.type("ATI3\r").c_str());
}

// --- AT* commands ---------------------------------------------------------------------------

void test_star_command_runs_handler() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("AT*VER\r\r\nv9.9\r\n\r\nOK\r\n", r.type("AT*VER\r").c_str());
    ENDS_WITH(r.type("at*ver\r"), "OK\r\n");
}

void test_star_name_must_end_unless_prefix() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("AT*VERX\r"), "ERROR\r\n");
    ENDS_WITH(r.type("AT*VER?\r"), "OK\r\n");
    ENDS_WITH(r.type("AT*HWLED=CD\r"), "OK\r\n");
    TEST_ASSERT_EQUAL_STRING("LED=CD", lastArgs.c_str());
    r.type("AT*hw\r");
    TEST_ASSERT_EQUAL_STRING("", lastArgs.c_str());
}

void test_star_unknown_is_error() {
    fakes::MemoryStore st;
    Rig r(st);
    ENDS_WITH(r.type("AT*NOPE\r"), "ERROR\r\n");
    ENDS_WITH(r.type("AT*\r"), "ERROR\r\n");
}

void test_star_takes_rest_of_line_after_other_commands() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string out = r.type("ATE0*HW E1\r");
    TEST_ASSERT_EQUAL(0, r.a().get(modemcfg::kE));
    TEST_ASSERT_EQUAL_STRING(" E1", lastArgs.c_str());
    ENDS_WITH(out, "OK\r\n");
}

void test_star_reply_kinds() {
    fakes::MemoryStore st;
    Rig r(st);
    TEST_ASSERT_EQUAL_STRING("AT*OWN\rown result\r\n", r.type("AT*OWN\r").c_str());
    ENDS_WITH(r.type("AT*FAIL\r"), "\r\nERROR\r\n");
}

void test_star_help_lists_every_command() {
    fakes::MemoryStore st;
    Rig r(st);
    std::string out = r.type("AT*HELP\r");
    for (const StarCommand &c : kStar) {
        CONTAINS(out, c.usage);
        CONTAINS(out, c.help);
    }
    ENDS_WITH(r.type("AT*?\r"), "OK\r\n");
}

void test_find_star_prefers_longest_name() {
    fakes::MemoryStore st;
    Rig r(st);
    const char *args = nullptr;
    const StarCommand *c = r.modem.findStar("HELP", &args);
    TEST_ASSERT_NOT_NULL(c);
    TEST_ASSERT_EQUAL_STRING("HELP", c->name);
    TEST_ASSERT_NULL(r.modem.findStar("HEL", &args));
}

// --- Masked prompt and late results (AT*WIFIJOIN, AT*WIFISCAN) ------------------------------

void test_secret_prompt_echoes_stars() {
    fakes::MemoryStore st;
    Rig r(st);
    secretCalls = 0;
    std::string o = r.type("AT*SECRET\r");
    ENDS_WITH(o, "Secret: ");
    TEST_ASSERT_TRUE(r.port.busy());
    TEST_ASSERT_EQUAL_STRING("*******", r.type("hunter2").c_str());
    TEST_ASSERT_EQUAL_STRING("\b \b", r.type("\b").c_str());  // Backspace deletes
    o = r.type("x\r\n");
    NOT_CONTAINS(o, "hunter");
    ENDS_WITH(o, "\r\nOK\r\n");
    TEST_ASSERT_EQUAL(1, secretCalls);
    TEST_ASSERT_EQUAL_STRING("hunterx", secretGot.c_str());
    TEST_ASSERT_FALSE(r.port.busy());
    ENDS_WITH(r.type("AT\r"), "\r\nOK\r\n");  // the LF after it was ignored
}

void test_secret_prompt_with_echo_off_shows_nothing() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("ATE0\r");
    r.type("AT*SECRET\r");
    TEST_ASSERT_EQUAL_STRING("", r.type("abcdef").c_str());
    ENDS_WITH(r.type("\r"), "\r\nOK\r\n");
}

void test_secret_prompt_cancel_and_overflow() {
    fakes::MemoryStore st;
    Rig r(st);
    secretCalls = 0;
    r.type("AT*SECRET\r");
    ENDS_WITH(r.type("abc\x1b"), "\r\nERROR\r\n");  // Esc
    r.type("AT*SECRET\r");
    ENDS_WITH(r.type("abc\x03"), "\r\nERROR\r\n");  // Ctrl-C
    TEST_ASSERT_EQUAL(0, secretCalls);
    r.type("AT*SECRET\r");
    std::string longer(kSecretMax + 1, 'z');
    r.type(longer.c_str());
    ENDS_WITH(r.type("\r"), "\r\nERROR\r\n");
    TEST_ASSERT_EQUAL(0, secretCalls);
}

void test_secret_is_not_repeated_by_a_slash() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("AT*SECRET\r");
    r.type("topsecret\r");
    secretCalls = 0;
    std::string o = r.type("A/");
    ENDS_WITH(o, "Secret: ");  // the command runs again and asks again
    NOT_CONTAINS(o, "topsecret");
    TEST_ASSERT_EQUAL(0, secretCalls);
    r.type("\x1b");
}

void test_late_result_and_cancel() {
    fakes::MemoryStore st;
    Rig r(st);
    cancels = 0;
    std::string o = r.type("AT*LATER\r\n");
    NOT_CONTAINS(o, "OK");
    TEST_ASSERT_TRUE(r.port.busy());
    TEST_ASSERT_FALSE(r.port.lineEmpty());
    TEST_ASSERT_EQUAL_STRING("", r.type("\n").c_str());  // LF is not a key press
    r.out.text.clear();
    TEST_ASSERT_TRUE(r.port.finish(Reply::kOk));
    TEST_ASSERT_EQUAL_STRING("\r\nOK\r\n", r.out.text.c_str());
    TEST_ASSERT_FALSE(r.port.finish(Reply::kOk));  // only once
    TEST_ASSERT_TRUE(r.port.lineEmpty());

    r.type("AT*LATER\r");
    ENDS_WITH(r.type("x"), "\r\nERROR\r\n");  // a key cancels
    TEST_ASSERT_EQUAL(1, cancels);
    TEST_ASSERT_FALSE(r.port.finish(Reply::kOk));  // too late: nothing is sent
    ENDS_WITH(r.type("AT\r"), "\r\nOK\r\n");
}

void test_secret_handler_can_wait_for_a_late_result() {
    fakes::MemoryStore st;
    Rig r(st);
    r.type("AT*SECRET\r");
    std::string o = r.type("later\r");
    NOT_CONTAINS(o, "OK");
    TEST_ASSERT_TRUE(r.port.busy());
    r.out.text.clear();
    TEST_ASSERT_TRUE(r.port.finish(Reply::kError));
    TEST_ASSERT_EQUAL_STRING("\r\nERROR\r\n", r.out.text.c_str());
}

void test_model_commands_persist_outside_templates() {
    fakes::MemoryStore st;
    Rig r(st);
    for (unsigned speed : {14400u, 28800u, 33600u, 56000u}) {
        char cmd[40];
        snprintf(cmd, sizeof cmd, "AT*MODEL=%u\r", speed);
        ENDS_WITH(r.type(cmd), "OK\r\n");
        TEST_ASSERT_EQUAL_UINT32(speed, r.modem.model());
        CONTAINS(r.type("ATI3\r"), modemcfg::modelName(speed));
        CONTAINS(r.type("ATI4\r"), modemcfg::modelName(speed));
        CONTAINS(r.type("ATI5\r"), modemcfg::modelName(speed));
        CONTAINS(r.type("ATI9\r"), modemcfg::modelName(speed));
        Rig restarted(st);
        TEST_ASSERT_EQUAL_UINT32(speed, restarted.modem.model());
    }
    r.type("AT&F1\r");
    TEST_ASSERT_EQUAL_UINT32(56000, r.modem.model());
    r.type("ATZ\r");
    TEST_ASSERT_EQUAL_UINT32(56000, r.modem.model());
    for (const char *invalid :
         {"ATMODEL=14400\r", "AT*MODEL=1\r", "AT*MODEL=56001\r", "AT*MODEL=14400x\r"}) {
        ENDS_WITH(r.type(invalid), "ERROR\r\n");
        TEST_ASSERT_EQUAL_UINT32(56000, r.modem.model());
    }
    CONTAINS(r.type("AT*MODEL=?\r"), "14400,28800,33600,56000");
    CONTAINS(r.type("AT*MODEL?\r"), "56000 (Sportster 56k Series)");
    TEST_ASSERT_FALSE(r.modem.active().lineExplicit);
    r.type("AT&N8&W\r");
    TEST_ASSERT_TRUE(r.modem.active().lineExplicit);
    Rig again(st);
    TEST_ASSERT_TRUE(again.modem.active().lineExplicit);
    st.erase();
    Rig factory(st);
    TEST_ASSERT_EQUAL_UINT32(14400, factory.modem.model());
}

void test_connect_detail_uses_operational_speed_and_respects_result_modes() {
    auto profile = modemcfg::factoryProfile(1);
    char result[96];
    profile.v[modemcfg::kAmpA] = 3;
    atcmd::formatConnect(56000, 14400, profile, result, sizeof result);
    TEST_ASSERT_EQUAL_STRING("\r\nCONNECT 56000/ARQ/V32/NONE\r\n", result);
    profile.v[modemcfg::kAmpM] = 0;
    atcmd::formatConnect(14400, 300, profile, result, sizeof result);
    TEST_ASSERT_EQUAL_STRING("\r\nCONNECT 14400/V21/NONE\r\n", result);
    profile.v[modemcfg::kV] = 0;
    atcmd::formatConnect(56000, 14400, profile, result, sizeof result);
    TEST_ASSERT_EQUAL_STRING("232\r", result);
    profile.v[modemcfg::kV] = 1;
    profile.v[modemcfg::kX] = 0;
    atcmd::formatConnect(56000, 14400, profile, result, sizeof result);
    TEST_ASSERT_EQUAL_STRING("\r\nCONNECT\r\n", result);
    profile.v[modemcfg::kQ] = 1;
    TEST_ASSERT_EQUAL(0, atcmd::formatConnect(56000, 14400, profile, result, sizeof result));
    TEST_ASSERT_EQUAL_STRING("", result);
}

void test_failed_nvram_updates_do_not_leak_into_later_saves() {
    fakes::MemoryStore store;
    Modem modem(store, nullptr, 0);
    modem.begin();
    TEST_ASSERT_TRUE(modem.writeTemplate(0, 38400));
    const auto before = modem.nvram();
    modem.active().v[modemcfg::kE] = 0;
    store.failSave = true;
    TEST_ASSERT_FALSE(modem.writeTemplate(0, 9600));
    TEST_ASSERT_TRUE(before.tmpl[0] == modem.nvram().tmpl[0]);
    TEST_ASSERT_FALSE(modem.setHostBaud(9600));
    TEST_ASSERT_EQUAL(before.hostBaud, modem.hostBaud());
    TEST_ASSERT_FALSE(modem.setPowerOn(1));
    TEST_ASSERT_EQUAL(before.y, modem.nvram().y);
    TEST_ASSERT_FALSE(modem.setPhone(0, "test.example"));
    TEST_ASSERT_EQUAL_STRING(before.phone[0], modem.nvram().phone[0]);
    // the modem reads it through its reference
    // cppcheck-suppress redundantAssignment
    store.failSave = false;
    TEST_ASSERT_TRUE(modem.setModel(56000));
    modemcfg::Nvram restored;
    TEST_ASSERT_EQUAL(modemcfg::LoadResult::kOk, modemcfg::loadNvram(store, &restored));
    TEST_ASSERT_TRUE(before.tmpl[0] == restored.tmpl[0]);
    TEST_ASSERT_EQUAL(before.hostBaud, restored.hostBaud);
    TEST_ASSERT_EQUAL(before.y, restored.y);
    TEST_ASSERT_EQUAL_STRING(before.phone[0], restored.phone[0]);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_failed_nvram_updates_do_not_leak_into_later_saves);

    RUN_TEST(test_connect_detail_uses_operational_speed_and_respects_result_modes);
    RUN_TEST(test_model_commands_persist_outside_templates);
    RUN_TEST(test_secret_prompt_echoes_stars);
    RUN_TEST(test_secret_prompt_with_echo_off_shows_nothing);
    RUN_TEST(test_secret_prompt_cancel_and_overflow);
    RUN_TEST(test_secret_is_not_repeated_by_a_slash);
    RUN_TEST(test_late_result_and_cancel);
    RUN_TEST(test_secret_handler_can_wait_for_a_late_result);
    RUN_TEST(test_at_gives_ok_with_echo);
    RUN_TEST(test_lower_case_works_mixed_case_is_ignored);
    RUN_TEST(test_text_before_at_is_ignored);
    RUN_TEST(test_lf_after_cr_is_ignored);
    RUN_TEST(test_echo_off);
    RUN_TEST(test_numeric_results);
    RUN_TEST(test_quiet_mode);
    RUN_TEST(test_quiet_still_shows_information);
    RUN_TEST(test_backspace_and_delete);
    RUN_TEST(test_backspace_cannot_delete_at);
    RUN_TEST(test_repeat_last_line_with_a_slash);
    RUN_TEST(test_a_slash_with_nothing_before_is_plain_ok);
    RUN_TEST(test_line_too_long_is_error);
    RUN_TEST(test_s3_and_s4_change_the_line_end);
    RUN_TEST(test_s3_s4_s5_are_limited_to_0_127_and_s3_is_never_printable);
    RUN_TEST(test_s5_changes_backspace);
    RUN_TEST(test_several_commands_per_line);
    RUN_TEST(test_first_bad_command_stops_the_line);
    RUN_TEST(test_missing_number_is_zero);
    RUN_TEST(test_out_of_range_settings_are_errors);
    RUN_TEST(test_hang_up_and_dial_method);
    RUN_TEST(test_dial_answer_online_wait_for_later_versions);
    RUN_TEST(test_o_h_and_z_go_to_the_link);
    RUN_TEST(test_w_keeps_the_port_speed_in_the_template);
    RUN_TEST(test_host_baud_is_stored_at_once);
    RUN_TEST(test_s_register_set_and_query);
    RUN_TEST(test_s_register_select_then_equals_and_query);
    RUN_TEST(test_s_register_bit);
    RUN_TEST(test_result_texts_and_numbers);
    RUN_TEST(test_x_levels);
    RUN_TEST(test_port_result_uses_v_q_and_x);
    RUN_TEST(test_connect_codes_for_speeds);
    RUN_TEST(test_extended_line_speed_commands_and_results);
    RUN_TEST(test_ampersand_f_loads_factory_templates);
    RUN_TEST(test_write_then_reset_and_restart);
    RUN_TEST(test_z_ignores_the_rest_of_the_line);
    RUN_TEST(test_z_numbers);
    RUN_TEST(test_y_selects_power_on_template_and_saves);
    RUN_TEST(test_w_out_of_range);
    RUN_TEST(test_stored_phone_numbers);
    RUN_TEST(test_fclass);
    RUN_TEST(test_ati_identity_lines);
    RUN_TEST(test_every_inquiry_0_to_7_answers);
    RUN_TEST(test_ati4_current_settings_layout);
    RUN_TEST(test_ati5_stored_settings_layout);
    RUN_TEST(test_ati6_and_ati7_layout);
    RUN_TEST(test_several_inquiries_on_one_line);
    RUN_TEST(test_ampersand_v_shows_current_and_stored);
    RUN_TEST(test_numeric_mode_has_no_leading_blank_line);
    RUN_TEST(test_star_command_runs_handler);
    RUN_TEST(test_star_name_must_end_unless_prefix);
    RUN_TEST(test_star_unknown_is_error);
    RUN_TEST(test_star_takes_rest_of_line_after_other_commands);
    RUN_TEST(test_star_reply_kinds);
    RUN_TEST(test_star_help_lists_every_command);
    RUN_TEST(test_find_star_prefers_longest_name);
    return UNITY_END();
}
