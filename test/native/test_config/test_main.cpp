#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#define FAKES_STORE
#define FAKES_SINK
#include "../support/fakes.h"
#include "config.h"
#include "registry.h"
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
#include "metadata_cases.h"
void test_menu_actions_have_production_at_star_handlers() {
    Rig r;
    size_t n;
    const auto *a = actions(n);
    TEST_ASSERT_EQUAL(91, n);
    for (size_t i = 0; i < n; ++i) {
        TEST_ASSERT_NOT_NULL(a[i].metadata);
        TEST_ASSERT_TRUE(*a[i].metadata->friendly);
        checkTemplates(*a[i].metadata, r.modem);
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
void test_history_persists_every_kind_and_refuses_another_layout() {
    Store store;
    History history(store);
    CallRecord record;
    record.kind = calls::Kind::Raw;
    TEST_ASSERT_TRUE(history.append(record));
    record.kind = calls::Kind::Ppp;
    TEST_ASSERT_TRUE(history.append(record));
    record.kind = calls::Kind::Slip;
    record.incoming = true;
    TEST_ASSERT_TRUE(history.append(record));
    History restored(store);
    TEST_ASSERT_TRUE(restored.begin());
    TEST_ASSERT_EQUAL(3, restored.size());
    TEST_ASSERT_EQUAL(calls::Kind::Raw, restored.at(0).kind);
    TEST_ASSERT_EQUAL(calls::Kind::Ppp, restored.at(1).kind);
    TEST_ASSERT_EQUAL(calls::Kind::Slip, restored.at(2).kind);
    TEST_ASSERT_TRUE(restored.at(2).incoming);
    // Another layout number with a good CRC is not read.
    store.bytes[4] = 2;
    uint32_t crc = modemcfg::crc32(store.bytes.data(), store.bytes.size() - 4);
    for (unsigned i = 0; i < 4; ++i)
        store.bytes[store.bytes.size() - 4 + i] = uint8_t(crc >> (8 * i));
    History other(store);
    TEST_ASSERT_FALSE(other.begin());
    TEST_ASSERT_EQUAL(0, other.size());
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
#include "list_cases.h"
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_metadata_json_matches_the_generated_source);
    RUN_TEST(test_metadata_enumerations_match_firmware_tables);
    RUN_TEST(test_metadata_profile_values_and_registers_match_firmware);
    RUN_TEST(test_metadata_limits_match_firmware);
    RUN_TEST(test_menu_actions_have_production_at_star_handlers);
    RUN_TEST(test_ring_wrap_has_only_structured_events_and_can_disable);
    RUN_TEST(test_history_eviction_reload_and_atomic_rollback);
    RUN_TEST(test_history_persists_every_kind_and_refuses_another_layout);
    RUN_TEST(test_history_rejects_corruption_without_overwriting_flash);
    RUN_TEST(test_lists_use_only_letters_and_sgr_at_every_size);
    RUN_TEST(test_every_section_and_profile_page_is_reachable);
    RUN_TEST(test_enum_choices_show_friendly_labels_and_confirm);
    RUN_TEST(test_forms_echo_only_new_text_and_mask_secrets);
    RUN_TEST(test_async_password_and_geometry_are_isolated);
    RUN_TEST(test_output_pages_are_complete_and_sanitized);
    RUN_TEST(test_disabled_actions_and_browser_diagnostics);
    RUN_TEST(test_idle_cancel_timeout_and_reservation);
    RUN_TEST(test_structured_phonebook_edit_and_confirmation);
    RUN_TEST(test_metadata_units_ranges_and_current_values);
    RUN_TEST(test_binary_diagnostics_have_no_menu_bytes);
    RUN_TEST(test_client_key_alias_preserves_standard_s5);
    RUN_TEST(test_optional_fields_variants_and_register_edits);
    RUN_TEST(test_invalid_numbers_and_terminal_reports_cannot_change_state);
    return UNITY_END();
}
