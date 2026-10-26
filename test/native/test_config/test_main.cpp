#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#include "config.h"
#include "registry.h"
using namespace config;
void setUp() {}
void tearDown() {}
class Store : public modemcfg::Store {
public:
    std::vector<uint8_t> bytes;
    bool fail = false;
    size_t load(uint8_t *p, size_t n) override {
        n = n < bytes.size() ? n : bytes.size();
        memcpy(p, bytes.data(), n);
        return n;
    }
    bool save(const uint8_t *p, size_t n) override {
        if (fail) {
            return false;
        }
        bytes.assign(p, p + n);
        return true;
    }
    bool erase() override {
        if (fail) {
            return false;
        }
        bytes.clear();
        return true;
    }
};
class Sink : public atcmd::Sink {
public:
    std::string text;
    void write(const char *p, size_t n) override { text.append(p, n); }
};
unsigned stubCalls = 0;
atcmd::Reply stub(atcmd::Port &, const char *) {
    ++stubCalls;
    return atcmd::Reply::kOk;
}
Registry registry(Hooks{stub, stub, stub, stub, stub, stub, stub, stub, stub});
struct Rig {
    Store store;
    Sink sink;
    atcmd::Modem modem{store, registry.data(), registry.size()};
    hostport::CallSlot slot;
    hostport::Session session{modem, sink, 115200, slot};
    Terminal terminal{session, false};
    explicit Rig(bool rs232 = false) : terminal(session, rs232) { modem.begin(); }
    std::string drain(size_t chunk = 1) {
        std::string out;
        uint8_t bytes[256];
        size_t n;
        while ((n = terminal.read(bytes, chunk)) != 0) {
            out.append(reinterpret_cast<char *>(bytes), n);
            TEST_ASSERT_LESS_THAN(40000, out.size());
        }
        return out;
    }
    void feed(const char *s) {
        while (*s) {
            uint8_t c = *s++;
            terminal.write(&c, 1);
        }
    }
};
void test_menu_actions_have_production_at_star_handlers() {
    Rig r;
    size_t n;
    const auto *a = actions(n);
    TEST_ASSERT_GREATER_THAN(50, n);
    for (size_t i = 0; i < n; ++i) {
        if (!*a[i].command) {
            continue;
        }
        TEST_ASSERT_EQUAL('*', a[i].command[0]);
        const char *args;
        auto *entry = r.modem.findStar(a[i].command + 1, &args);
        TEST_ASSERT_NOT_NULL(entry);
        TEST_ASSERT_NOT_NULL(entry->fn);
        TEST_ASSERT_TRUE(*entry->help);
        TEST_ASSERT_TRUE(*a[i].help);
    }
    for (size_t i = 0; i < registry.size(); ++i) {
        TEST_ASSERT_NOT_NULL(registry.data()[i].fn);
    }
}
void test_direct_menu_reserves_without_carrier_and_releases() {
    Rig r;
    TEST_ASSERT_TRUE(r.terminal.open());
    TEST_ASSERT_TRUE(r.session.startLocal(r.terminal));
    TEST_ASSERT_TRUE(r.slot.busy());
    TEST_ASSERT_FALSE(r.session.callUp());
    TEST_ASSERT_FALSE(r.session.dcd());
    Sink other;
    hostport::Session session(r.modem, other, 38400, r.slot);
    hostport::EchoConnection echo;
    echo.open(0, 0);
    TEST_ASSERT_FALSE(session.startCall(echo));
    r.drain();
    r.feed("q\r\n");
    r.session.tick(10, true);
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_FALSE(r.session.localOpen());
    TEST_ASSERT_TRUE(session.startCall(echo));
}
void test_renderers_and_paging_work_with_one_byte_output() {
    Rig r;
    r.terminal.setStyle("PLAIN");
    r.terminal.open();
    auto out = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find('\x1b'));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("14.4"));
    r.feed("n\r");
    out = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Client Tests"));
    r.terminal.close();
    r.terminal.setStyle("CP437");
    r.terminal.open();
    out = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find(char(0xda)));
}
void test_response_pagination_does_not_leak_next_page() {
    Rig r;
    r.terminal.open();
    r.drain();
    for (unsigned i = 0; i < 60; ++i) {
        r.terminal.write("row\r\n", 5);
    }
    auto first = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, first.find("More:"));
    TEST_ASSERT_LESS_THAN(150, first.size());
    TEST_ASSERT_TRUE(r.drain().empty());
    r.feed("\r");
    TEST_ASSERT_FALSE(r.drain().empty());
}
void test_menu_secret_input_is_wiped_and_never_echoed() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("10\r");
    r.drain();
    r.feed("4\r");
    r.drain();
    r.feed("SecretCanary123");
    auto out = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("SecretCanary"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("***"));
    r.feed("\x1b");
    r.drain();
}
void test_ring_wrap_has_only_structured_events_and_can_disable() {
    Log log;
    char data[42];
    log.begin(data, sizeof data);
    for (unsigned i = 0; i < 100; ++i) {
        log.event(i, Event::WifiUp);
    }
    TEST_ASSERT_EQUAL(sizeof data, log.size());
    std::string out;
    for (size_t i = 0; i < log.size(); ++i) {
        out += log.at(i);
    }
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("99 wifi up"));
    log.level = 0;
    log.event(100, Event::Menu);
    TEST_ASSERT_EQUAL(sizeof data, log.size());
    log.clear();
    TEST_ASSERT_EQUAL(0, log.size());
    log.begin(nullptr, 0);
    log.event(1, Event::Boot);
    TEST_ASSERT_EQUAL(0, log.size());
}
void test_history_eviction_reload_and_atomic_rollback() {
    Store store;
    History history(store);
    TEST_ASSERT_TRUE(history.begin());
    for (unsigned i = 0; i < 52; ++i) {
        CallRecord r;
        r.duration = i;
        strcpy(r.host, "bbs.example.com");
        TEST_ASSERT_TRUE(history.append(r));
    }
    TEST_ASSERT_EQUAL(50, history.size());
    TEST_ASSERT_EQUAL(2, history.at(0).duration);
    History loaded(store);
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_EQUAL(51, loaded.at(49).duration);
    store.fail = true;
    CallRecord record;
    record.duration = 999;
    TEST_ASSERT_FALSE(history.append(record));
    TEST_ASSERT_EQUAL(2, history.at(0).duration);
    TEST_ASSERT_EQUAL(51, history.at(49).duration);
    TEST_ASSERT_FALSE(history.clear());
    TEST_ASSERT_EQUAL(50, history.size());
    store.fail = false;
    TEST_ASSERT_TRUE(history.clear());
    TEST_ASSERT_TRUE(store.bytes.empty());
}
void test_history_rejects_corruption_without_overwriting_flash() {
    Store store;
    History history(store);
    CallRecord r;
    history.append(r);
    store.bytes[10] ^= 1;
    auto before = store.bytes;
    History loaded(store);
    TEST_ASSERT_FALSE(loaded.begin());
    TEST_ASSERT_EQUAL(0, loaded.size());
    TEST_ASSERT_TRUE(before == store.bytes);
}
void test_display_sanitizes_untrusted_escape_sequences() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.terminal.write("\x1b[2Jhello", 10);
    auto text = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, text.find('\x1b'));
}
void test_direct_menu_dtr_drop_closes_and_releases_reservation() {
    Rig r;
    r.terminal.open();
    r.session.startLocal(r.terminal);
    r.session.tick(0, true);
    r.session.tick(10, false);
    r.session.tick(1000, false);
    TEST_ASSERT_FALSE(r.terminal.isOpen());
    TEST_ASSERT_FALSE(r.slot.busy());
}
void test_overlong_output_is_bounded_and_reported() {
    Rig r;
    r.terminal.open();
    r.drain();
    std::string huge(100000, 'x');
    r.terminal.write(huge.data(), huge.size());
    size_t count = 0;
    for (unsigned page = 0; page < 100; ++page) {
        auto out = r.drain(64);
        count += out.size();
        if (count > 40000) {
            TEST_FAIL_MESSAGE("Unbounded output");
        }
        if (out.find("More:") == std::string::npos) {
            break;
        }
        r.feed("\r");
    }
    TEST_ASSERT_LESS_THAN(40000, count);
}
void test_rs232_menu_lists_and_blocks_takeover_actions() {
    Rig r(true);
    r.terminal.open();
    r.drain();
    r.feed("12\r");
    auto out = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("unavailable: takes over RS232"));
    r.feed("n\r");
    r.drain();
    stubCalls = 0;
    r.feed("1\r");
    r.drain();
    TEST_ASSERT_EQUAL(0, stubCalls);
}
void test_oversized_input_is_rejected_without_dispatching_prefix() {
    Rig r;
    r.terminal.open();
    r.drain();
    stubCalls = 0;
    std::string input(200, '1');
    input += '\r';
    r.feed(input.c_str());
    auto out = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Input too long"));
    TEST_ASSERT_EQUAL(0, stubCalls);
    TEST_ASSERT_TRUE(r.terminal.isOpen());
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_menu_actions_have_production_at_star_handlers);
    RUN_TEST(test_direct_menu_reserves_without_carrier_and_releases);
    RUN_TEST(test_renderers_and_paging_work_with_one_byte_output);
    RUN_TEST(test_response_pagination_does_not_leak_next_page);
    RUN_TEST(test_menu_secret_input_is_wiped_and_never_echoed);
    RUN_TEST(test_ring_wrap_has_only_structured_events_and_can_disable);
    RUN_TEST(test_history_eviction_reload_and_atomic_rollback);
    RUN_TEST(test_history_rejects_corruption_without_overwriting_flash);
    RUN_TEST(test_display_sanitizes_untrusted_escape_sequences);
    RUN_TEST(test_direct_menu_dtr_drop_closes_and_releases_reservation);
    RUN_TEST(test_overlong_output_is_bounded_and_reported);
    RUN_TEST(test_rs232_menu_lists_and_blocks_takeover_actions);
    RUN_TEST(test_oversized_input_is_rejected_without_dispatching_prefix);
    return UNITY_END();
}
