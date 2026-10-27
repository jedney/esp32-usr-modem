#include <Arduino.h>
#include <LittleFS.h>
#include <unity.h>
#include "catalog.h"
#include "client_platform.h"
#include "client_settings.h"
#include "nvs_store.h"
void setUp() {}
void tearDown() {}
void test_client_settings_scratch_nvs() {
    hw::NvsStore store("clienttmp", "cfg");
    TEST_ASSERT_TRUE(store.erase());
    clienttest::Settings settings(store);
    TEST_ASSERT_FALSE(settings.begin());
    TEST_ASSERT_TRUE(settings.save(1, {clienttest::Charset::Latin1, 80, 24, 127}));
    clienttest::Settings reload(store);
    TEST_ASSERT_TRUE(reload.begin());
    TEST_ASSERT_EQUAL(127, reload.get(1).backspace);
    TEST_ASSERT_EQUAL(8, reload.get(0).backspace);
    TEST_ASSERT_TRUE(store.erase());
}
void test_client_generated_source_and_immutable_log() {
    char storage[128];
    config::Log log;
    log.begin(storage, sizeof storage);
    log.event(0, config::Event::Boot);
    hw::ClientPlatform platform(log, false);
    auto *source = platform.source("LOG", 1);
    TEST_ASSERT_NOT_NULL(source);
    uint8_t before[128], after[128];
    size_t n = source->read(0, 0, before, sizeof before);
    log.clear();
    TEST_ASSERT_EQUAL(n, source->read(0, 0, after, sizeof after));
    TEST_ASSERT_EQUAL_MEMORY(before, after, n);
    source = platform.source("TEST", 1048576);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_EQUAL_STRING("TEST1M.BIN", source->name(0));
    TEST_ASSERT_EQUAL(1048576, source->size(0));
    TEST_ASSERT_EQUAL(4, source->read(0, 255, after, 4));
    const uint8_t expected[] = {255, 0, 1, 2};
    TEST_ASSERT_EQUAL_MEMORY(expected, after, 4);
    platform.release();
}
void test_client_crc_on_device() {
    const uint8_t data[] = {'1', '2', '3', '4', '5', '6', '7', '8', '9'};
    TEST_ASSERT_EQUAL_HEX16(0x31c3, clienttest::crc16(data, sizeof data));
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926, ~clienttest::crc32(data, sizeof data));
}

void test_every_packaged_sound_and_all_batch_source_is_readable() {
    char storage[128];
    config::Log log;
    log.begin(storage, sizeof storage);
    log.event(0, config::Event::Boot);
    hw::ClientPlatform platform(log, false);
    for (const auto &asset : sounds::kAssets) {
        auto *source = platform.source(asset.name, 1);
        TEST_ASSERT_NOT_NULL(source);
        TEST_ASSERT_EQUAL_STRING(asset.name, source->name(0));
        uint8_t header[4];
        TEST_ASSERT_EQUAL(4, source->read(0, 0, header, sizeof header));
        TEST_ASSERT_EQUAL_MEMORY("RIFF", header, 4);
    }
    auto *source = platform.source("ALL", 1);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_EQUAL(sounds::kAssetCount + 1, source->count());
    for (unsigned i = 1; i < source->count(); ++i) {
        TEST_ASSERT_EQUAL_STRING(sounds::kAssets[i - 1].name, source->name(i));
        uint8_t header[4];
        TEST_ASSERT_EQUAL(4, source->read(i, 0, header, sizeof header));
        TEST_ASSERT_EQUAL_MEMORY("RIFF", header, 4);
    }
    source = platform.source("HS9600.WAV", 1);
    TEST_ASSERT_NOT_NULL(source);
    TEST_ASSERT_EQUAL_STRING("HS2400.WAV", source->name(0));
    platform.release();
}
void setup() {
    delay(2000);
    LittleFS.begin(false);
    UNITY_BEGIN();
    RUN_TEST(test_every_packaged_sound_and_all_batch_source_is_readable);
    RUN_TEST(test_client_settings_scratch_nvs);
    RUN_TEST(test_client_generated_source_and_immutable_log);
    RUN_TEST(test_client_crc_on_device);
    UNITY_END();
}
void loop() {}
