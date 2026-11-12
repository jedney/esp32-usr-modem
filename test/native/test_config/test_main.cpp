#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#define FAKES_STORE
#define FAKES_SINK
#include "../support/fakes.h"
#include "config.h"
#include "registry.h"
#include "screen.h"
using namespace config;
void setUp() {}
void tearDown() {}
using Store = fakes::MemoryStore;
using Sink = fakes::TextSink;
unsigned stubCalls = 0;
calls::Phonebook *activeBook = nullptr;
bool stubAsync = false, stubWait = false, waitCancelled = false;
std::string secretReceived;
atcmd::Reply receiveSecret(atcmd::Port &, const char *text, void *) {
    secretReceived = text;
    return atcmd::Reply::kOk;
}
atcmd::Reply stub(atcmd::Port &port, const char *) {
    ++stubCalls;
    if (stubWait) {
        stubWait = false;
        port.wait([](void *) { waitCancelled = true; }, nullptr);
        return atcmd::Reply::kPending;
    }
    if (stubAsync) {
        stubAsync = false;
        for (unsigned i = 0; i < 30; ++i)
            port.line("Earlier service output");
        port.askSecret("Password: ", receiveSecret, nullptr);
        return atcmd::Reply::kPending;
    }
    return atcmd::Reply::kOk;
}
atcmd::Reply phonebookCommand(atcmd::Port &port, const char *args) {
    return activeBook ? activeBook->command(port, args, false) : stub(port, args);
}
atcmd::Reply phonebookDelete(atcmd::Port &port, const char *args) {
    return activeBook ? activeBook->command(port, args, true) : stub(port, args);
}
Registry registry(Hooks{stub, phonebookCommand, phonebookDelete, stub, stub, stub, stub, stub,
                        stub});
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
struct BookServices : UiServices {
    Store store;
    calls::Phonebook book{store};
    BookServices() {
        book.begin();
        book.clear();
        activeBook = &book;
    }
    ~BookServices() { activeBook = nullptr; }
    calls::Phonebook *phonebook() override { return &book; }
    UiState state() const override {
        UiState result;
        strcpy(result.link, "Idle");
        result.hostBaud = 38400;
        return result;
    }
    void add(unsigned n) {
        calls::Entry entry;
        snprintf(entry.number, sizeof entry.number, "%u", n + 200);
        snprintf(entry.host, sizeof entry.host, "host%02u.example", n);
        entry.speed = 28800;
        TEST_ASSERT_TRUE(book.put(entry));
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
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Client Tests"));
    r.terminal.close();
    r.terminal.setStyle("CP437");
    r.terminal.open();
    out = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find(char(0xc9)));
    ScreenView screen;
    screen.accept(out);
    TEST_ASSERT_EQUAL(0, screen.outside);
    TEST_ASSERT_EQUAL(0, screen.scrolls);
    TEST_ASSERT_TRUE(screen.wrap && screen.cursor);
}
void test_response_pagination_does_not_leak_next_page() {
    Rig r;
    r.terminal.open();
    ScreenView view;  // one terminal for the whole test: later frames send only changes
    view.accept(r.drain());
    for (unsigned i = 0; i < 60; ++i) {
        char row[24];
        snprintf(row, sizeof row, "row%02u\n", i);
        r.terminal.write(row, strlen(row));
    }
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("More:"));
    TEST_ASSERT_FALSE(view.has("row15"));
    TEST_ASSERT_TRUE(r.drain().empty());
    r.feed("\r");
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("row15"));
    TEST_ASSERT_FALSE(view.has("row30"));
    TEST_ASSERT_FALSE(view.has("row14"));
}
void test_menu_secret_input_is_wiped_and_never_echoed() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("8\r4\rSecretCanary123");
    auto out = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("SecretCanary"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("***"));
    r.feed("\r");
    out = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("SecretCanary"));
    r.feed("\x1b");
    r.terminal.poll(61);
    out = r.drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("SecretCanary"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("cancelled"));
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
    store.failSave = store.failErase = true;
    CallRecord record;
    record.duration = 999;
    TEST_ASSERT_FALSE(history.append(record));
    TEST_ASSERT_EQUAL(2, history.at(0).duration);
    TEST_ASSERT_EQUAL(51, history.at(49).duration);
    TEST_ASSERT_FALSE(history.clear());
    TEST_ASSERT_EQUAL(50, history.size());
    store.failSave = store.failErase = false;
    TEST_ASSERT_TRUE(history.clear());
    TEST_ASSERT_TRUE(store.bytes.empty());
}
void test_history_reads_legacy_raw_and_persists_internet_kinds() {
    Store store;
    History history(store);
    CallRecord record;
    record.raw = true;
    TEST_ASSERT_TRUE(history.append(record));
    store.bytes[4] = 1;
    uint32_t crc = modemcfg::crc32(store.bytes.data(), store.bytes.size() - 4);
    for (unsigned i = 0; i < 4; ++i)
        store.bytes[store.bytes.size() - 4 + i] = uint8_t(crc >> (8 * i));
    History legacy(store);
    TEST_ASSERT_TRUE(legacy.begin());
    TEST_ASSERT_EQUAL(calls::Kind::Raw, legacy.at(0).kind);
    record.raw = false;
    record.kind = calls::Kind::Ppp;
    TEST_ASSERT_TRUE(legacy.append(record));
    History restored(store);
    TEST_ASSERT_TRUE(restored.begin());
    TEST_ASSERT_EQUAL(calls::Kind::Raw, restored.at(0).kind);
    TEST_ASSERT_EQUAL(calls::Kind::Ppp, restored.at(1).kind);
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
    ScreenView view;
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("?[2Jhello"));
    TEST_ASSERT_EQUAL(0, view.outside);
    TEST_ASSERT_EQUAL(0, view.scrolls);
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
    for (unsigned page = 0; page < 50; ++page) {
        auto out = r.drain(64);
        TEST_ASSERT_LESS_THAN(10000, out.size());
        ScreenView view;
        view.accept(out);
        TEST_ASSERT_EQUAL(0, view.outside);
        if (!view.has("More:"))
            return;
        r.feed("\r");
    }
    TEST_FAIL_MESSAGE("Output exceeded the bounded response buffer");
}
void test_rs232_menu_lists_and_blocks_takeover_actions() {
    Rig r(true);
    r.terminal.open();
    r.drain();
    r.feed("b\r");
    ScreenView screen;
    screen.accept(r.drain());
    TEST_ASSERT_TRUE(screen.has("RS232 lines"));
    TEST_ASSERT_TRUE(screen.has("unavailable: takes over RS232"));
    stubCalls = 0;
    r.feed("c\r");
    screen.accept(r.drain());
    TEST_ASSERT_TRUE(screen.has("Unavailable: hardware hook"));
    TEST_ASSERT_EQUAL(0, stubCalls);
    r.feed("\x1b[B");
    screen.accept(r.drain());
    TEST_ASSERT_FALSE(screen.has("> [C]"));
}
void test_oversized_input_is_rejected_without_dispatching_prefix() {
    Rig r;
    r.terminal.open();
    r.drain();
    r.feed("3\r3\r");
    r.drain();
    std::string input(200, '1');
    input += '\r';
    r.feed(input.c_str());
    auto out = r.drain();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("Input too long"));
    TEST_ASSERT_EQUAL(14400, r.modem.model());
    TEST_ASSERT_TRUE(r.terminal.isOpen());
}
#include "repaint_cases.h"
#include "review_cases.h"
#include "ui_cases.h"
void test_browser_lists_serial_diagnostics_without_dispatch() {
    Rig r;
    r.session.port().setBrowser(true);
    r.terminal.open();
    r.drain();
    stubCalls = 0;
    r.feed("c1");
    ScreenView view;
    view.accept(r.drain());
    TEST_ASSERT_TRUE(view.has("Client Tests run on the USB console"));
    TEST_ASSERT_EQUAL(0, stubCalls);
}
void test_client_key_alias_preserves_standard_s5() {
    Rig r;
    r.modem.active().s[5] = 21;
    r.session.port().clientBackspace(127);
    const char *command =
        "ATEX\x15"
        "0\r";
    while (*command)
        r.session.port().feed(*command++);
    TEST_ASSERT_EQUAL(0, r.modem.active().get(modemcfg::kE));
    TEST_ASSERT_TRUE(r.sink.text.find("OK") != std::string::npos);
}
void test_binary_service_has_no_geometry_or_redraw_bytes() {
    class Raw : public hostport::Connection {
    public:
        bool active = true;
        unsigned position = 0;
        size_t space() const override { return 256; }
        size_t write(const uint8_t *, size_t len) override { return len; }
        size_t read(uint8_t *out, size_t cap) override {
            size_t n = std::min<size_t>(cap, 256 - position);
            for (size_t i = 0; i < n; ++i)
                out[i] = position++;
            return n;
        }
        bool isOpen() const override { return active; }
        void close() override { active = false; }
        bool binaryService() const override { return true; }
    } raw;
    Rig r;
    r.terminal.open();
    r.drain();
    r.terminal.diagnostic(raw);
    r.terminal.poll(10000);
    auto bytes = r.drain(1);
    TEST_ASSERT_EQUAL(256, bytes.size());
    for (unsigned i = 0; i < 256; ++i)
        TEST_ASSERT_EQUAL_HEX8(i, uint8_t(bytes[i]));
    raw.close();
    r.terminal.poll(10001);
    TEST_ASSERT_FALSE(r.drain().empty());
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_idle_menus_do_not_repaint_but_input_and_resize_do);
    RUN_TEST(test_status_view_keeps_its_explicit_live_refresh);
    RUN_TEST(test_browser_lists_serial_diagnostics_without_dispatch);
    RUN_TEST(test_client_key_alias_preserves_standard_s5);
    RUN_TEST(test_binary_service_has_no_geometry_or_redraw_bytes);
    RUN_TEST(test_serial_reports_and_async_prompts_keep_input_isolated);
    RUN_TEST(test_phonebook_capacity_boundaries_and_wide_actions);
    RUN_TEST(test_geometry_inventory_and_compact_submenus);
    RUN_TEST(test_four_themes_unknown_live_refresh_and_failed_save);
    RUN_TEST(test_disabled_navigation_back_stack_and_form_focus);
    RUN_TEST(test_phonebook_paging_details_selection_and_storage_failure);
    RUN_TEST(test_phonebook_empty_missing_hook_and_changed_entry_confirmation);
    RUN_TEST(test_resize_mid_escape_and_secret_confirmation_timeout);
    RUN_TEST(test_esc_cancels_a_waiting_command_started_from_the_screen);
    RUN_TEST(test_idle_screen_closes_after_five_minutes_and_a_key_resets_it);
    RUN_TEST(test_idle_timeout_survives_the_clock_wrap);
    RUN_TEST(test_cell_text_has_fixed_width_and_cannot_inject_controls);
    RUN_TEST(test_menu_actions_have_production_at_star_handlers);
    RUN_TEST(test_direct_menu_reserves_without_carrier_and_releases);
    RUN_TEST(test_renderers_and_paging_work_with_one_byte_output);
    RUN_TEST(test_response_pagination_does_not_leak_next_page);
    RUN_TEST(test_menu_secret_input_is_wiped_and_never_echoed);
    RUN_TEST(test_ring_wrap_has_only_structured_events_and_can_disable);
    RUN_TEST(test_history_eviction_reload_and_atomic_rollback);
    RUN_TEST(test_history_reads_legacy_raw_and_persists_internet_kinds);
    RUN_TEST(test_history_rejects_corruption_without_overwriting_flash);
    RUN_TEST(test_display_sanitizes_untrusted_escape_sequences);
    RUN_TEST(test_direct_menu_dtr_drop_closes_and_releases_reservation);
    RUN_TEST(test_overlong_output_is_bounded_and_reported);
    RUN_TEST(test_rs232_menu_lists_and_blocks_takeover_actions);
    RUN_TEST(test_oversized_input_is_rejected_without_dispatching_prefix);
    RUN_TEST(test_measure_one_arrow_key_at_80x25);
    RUN_TEST(test_every_main_menu_move_sends_only_the_change);
    RUN_TEST(test_resize_refresh_style_colour_and_port_output_send_a_full_frame);
    RUN_TEST(test_changes_only_screen_matches_a_full_repaint);
    RUN_TEST(test_output_pages_step_by_the_lines_shown);
    RUN_TEST(test_alt_letter_neither_cancels_a_form_nor_runs_a_hotkey);
    RUN_TEST(test_input_too_long_is_shown_at_once_and_kept_until_cleared);
    return UNITY_END();
}
