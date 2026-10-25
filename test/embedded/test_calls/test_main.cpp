#include <Arduino.h>
#include <LittleFS.h>
#include <unity.h>
#include "call_audio.h"
#include "hw.h"
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
    modem.active().lineExplicit = true;
    modem.active().v[modemcfg::kAmpN] = 38;
    TEST_ASSERT_TRUE(modem.writeTemplate(0, 115200));
    atcmd::Modem again(store, nullptr, 0);
    again.begin();
    TEST_ASSERT_EQUAL_UINT32(56000, again.model());
    TEST_ASSERT_TRUE(again.throttle());
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
void test_nonblocking_i2s_and_pot_with_volume_cap() {
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
    TEST_ASSERT_FLOAT_WITHIN(0.00001f, volume::capGain(), volume::outputGain(1));
}
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_model_metadata_in_scratch_nvs);
    RUN_TEST(test_phonebook_in_scratch_nvs);
    RUN_TEST(test_littlefs_handshake_headers_and_streaming);
    RUN_TEST(test_nonblocking_i2s_and_pot_with_volume_cap);
    UNITY_END();
}
void loop() {}
