#include <Arduino.h>
#include <LittleFS.h>
#include <nvs.h>
#include <unity.h>
#include "call_audio.h"
#include "devicecfg.h"
#include "hw.h"
#include "net_settings.h"
#include "nvs_store.h"
#include "phonebook.h"
#include "volume.h"

void setUp() {}
void tearDown() {}

void test_model_metadata_in_scratch_nvs() {
    hw::NvsStore store("v08modem", "cfg");
    store.erase();
    atcmd::Modem modem(store, nullptr, 0);
    modem.begin();
    TEST_ASSERT_EQUAL_UINT32(14400, modem.model());
    TEST_ASSERT_TRUE(modem.setModel(56000));
    TEST_ASSERT_TRUE(modem.setThrottle(true));
    TEST_ASSERT_TRUE(modem.setCallDelay(true, 4321));
    TEST_ASSERT_TRUE(modem.setCallDelay(false, 567));
    modem.active().lineExplicit = true;
    modem.active().v[modemcfg::kAmpN] = 38;
    TEST_ASSERT_TRUE(modem.writeTemplate(0, 115200));
    atcmd::Modem again(store, nullptr, 0);
    again.begin();
    TEST_ASSERT_EQUAL_UINT32(56000, again.model());
    TEST_ASSERT_TRUE(again.throttle());
    TEST_ASSERT_EQUAL_UINT32(4321, again.ringDelayMs());
    TEST_ASSERT_EQUAL_UINT32(567, again.connectDelayMs());
    TEST_ASSERT_TRUE(again.active().lineExplicit);
    TEST_ASSERT_EQUAL(38, again.active().get(modemcfg::kAmpN));
    again.reset(4);
    TEST_ASSERT_EQUAL_UINT32(56000, again.model());
    TEST_ASSERT_FALSE(again.active().lineExplicit);
    TEST_ASSERT_TRUE(store.erase());
}
void test_phonebook_in_scratch_nvs() {
    hw::NvsStore store("v08book", "cfg");
    store.erase();
    // Larger objects are on the heap, keeping the Arduino loop-task stack small.
    auto *book = new calls::Phonebook(store);
    TEST_ASSERT_TRUE(book->begin());
    calls::Entry entry = *book->find("1");
    strcpy(entry.number, "02");
    entry.raw = true;
    entry.speed = 56000;
    TEST_ASSERT_TRUE(book->put(entry));
    delete book;
    book = new calls::Phonebook(store);
    TEST_ASSERT_TRUE(book->begin());
    TEST_ASSERT_EQUAL_STRING("www.amigaretro.com", book->find("1")->host);
    TEST_ASSERT_TRUE(book->find("02")->raw);
    TEST_ASSERT_EQUAL_UINT32(56000, book->find("02")->speed);
    TEST_ASSERT_TRUE(book->remove("02"));
    delete book;
    TEST_ASSERT_TRUE(store.erase());
}
// STA-08: a full phonebook (64 entries, 5,321 bytes in one blob) must save, change and
// shrink with the modem's other settings in NVS too. NVS writes the new blob before it
// frees the old one, so a change needs room for both. The NVS figures are printed for
// the decisions log.
struct OtherSetting {
    const char *ns;  // the production namespace (src/main.cpp)
    size_t size;     // about what a set-up modem stores there
};
const OtherSetting kOtherSettings[] = {
    {"modem", modemcfg::kBlobSize},
    {"net", netcfg::kBlobSize},
    {"device", devicecfg::kBlobSize},
    {"web", 32},
    {"clients", 64},
    {"internet", 160},
    {"diagnostics", 3},
    {"phy", 1904},  // the radio's calibration data (ESP32-S3, ESP-IDF 4.4)
};
constexpr size_t kOtherCount = sizeof kOtherSettings / sizeof kOtherSettings[0];

size_t usedEntries(const char *ns) {
    nvs_handle_t handle;
    if (nvs_open(ns, NVS_READONLY, &handle) != ESP_OK) {
        return 0;
    }
    size_t used = 0;
    nvs_get_used_entry_count(handle, &used);
    nvs_close(handle);
    return used;
}
void printNvs(const char *when) {
    nvs_stats_t stats{};
    TEST_ASSERT_EQUAL(ESP_OK, nvs_get_stats(nullptr, &stats));
    char line[128];
    snprintf(line, sizeof line, "NVS %s: %u used, %u free of %u entries (32 bytes each)", when,
             unsigned(stats.used_entries), unsigned(stats.free_entries),
             unsigned(stats.total_entries));
    TEST_MESSAGE(line);
}
void eraseScratch() {
    for (size_t i = 0; i < kOtherCount; ++i) {
        char ns[16];
        snprintf(ns, sizeof ns, "v17o%u", unsigned(i));
        hw::NvsStore(ns, "cfg").erase();
    }
    hw::NvsStore("v17book", "cfg").erase();
}
calls::Entry longEntry(unsigned i) {
    calls::Entry e;
    snprintf(e.number, sizeof e.number, "5551234%04u", i);  // 11 digits, the longest
    snprintf(e.host, sizeof e.host, "bbs%02u.%s.com", i % 100,
             "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");  // 63 characters
    e.speed = 33600;
    return e;
}
void test_full_phonebook_fits_beside_the_other_settings() {
    eraseScratch();
    printNvs("at start");
    // A board that never stored a setting gets a stand-in of about its size, so NVS holds
    // what a fully set-up modem holds. Settings already stored are counted as they are.
    for (size_t i = 0; i < kOtherCount; ++i) {
        if (usedEntries(kOtherSettings[i].ns) == 0) {
            char ns[16];
            snprintf(ns, sizeof ns, "v17o%u", unsigned(i));
            static uint8_t fill[2048];
            memset(fill, 0x5a, sizeof fill);
            TEST_ASSERT_TRUE(hw::NvsStore(ns, "cfg").save(fill, kOtherSettings[i].size));
        }
    }
    printNvs("with the other settings");
    hw::NvsStore store("v17book", "cfg");
    auto *book = new calls::Phonebook(store);  // 6 KB: on the heap, not the loop stack
    TEST_ASSERT_TRUE(book->begin());           // the factory entry
    for (unsigned i = 1; i < calls::kPhonebookCount; ++i) {
        char message[32];
        snprintf(message, sizeof message, "entry %u", i);
        TEST_ASSERT_TRUE_MESSAGE(book->put(longEntry(i)), message);
    }
    printNvs("with 64 entries");
    calls::Entry edited = longEntry(7);
    edited.host[0] = 'x';
    edited.speed = 0;
    TEST_ASSERT_TRUE(book->put(edited));  // a change: old and new blob side by side
    TEST_ASSERT_TRUE(book->remove(longEntry(9).number));
    delete book;
    book = new calls::Phonebook(store);
    TEST_ASSERT_TRUE(book->begin());
    TEST_ASSERT_TRUE(book->healthy());
    TEST_ASSERT_NOT_NULL(book->find(longEntry(7).number));
    TEST_ASSERT_EQUAL_STRING(edited.host, book->find(longEntry(7).number)->host);
    TEST_ASSERT_NULL(book->find(longEntry(9).number));
    TEST_ASSERT_NOT_NULL(book->find(longEntry(63).number));
    delete book;
    eraseScratch();
    printNvs("after clean-up");
}
void test_littlefs_handshake_headers_and_streaming() {
    TEST_ASSERT_TRUE(LittleFS.begin(false));
    for (uint32_t speed : {1200u, 2400u, 14400u, 28800u, 33600u, 56000u}) {
        char path[48];
        snprintf(path, sizeof path, "/sounds/handshake-%u.wav", unsigned(speed));
        hw::FileReader reader;
        TEST_ASSERT_TRUE_MESSAGE(reader.open(path), path);
        sounds::WavSource source(reader);
        TEST_ASSERT_TRUE_MESSAGE(source.begin(), path);
        int16_t samples[256];
        TEST_ASSERT_EQUAL(256, source.fill(samples, 256));
    }
}
void test_nonblocking_i2s_and_pot_at_full_volume() {
    TEST_ASSERT_TRUE(hw::audioStreamBegin());
    int16_t samples[256];
    for (auto &sample : samples) {
        sample = 32767;
    }
    uint32_t before = millis();
    for (unsigned i = 0; i < 32; ++i) {
        TEST_ASSERT_LESS_OR_EQUAL(256, hw::audioStreamWrite(samples, 256, 1));
    }
    TEST_ASSERT_LESS_THAN_UINT32(500, millis() - before);
    hw::audioStreamEnd();
    TEST_ASSERT_FALSE(hw::ampIsOn());
    TEST_ASSERT_LESS_OR_EQUAL_UINT32(hw::kPotMax, hw::potRaw());
    TEST_ASSERT_EQUAL_FLOAT(1.0f, volume::outputGain(1));
}
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_model_metadata_in_scratch_nvs);
    RUN_TEST(test_phonebook_in_scratch_nvs);
    RUN_TEST(test_full_phonebook_fits_beside_the_other_settings);
    RUN_TEST(test_littlefs_handshake_headers_and_streaming);
    RUN_TEST(test_nonblocking_i2s_and_pot_at_full_volume);
    UNITY_END();
}
void loop() {}
