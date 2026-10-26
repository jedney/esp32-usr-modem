#include <Arduino.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <unity.h>
#include "call_audio.h"
#include "config.h"
#include "file_store.h"
#include "hw.h"
#include "net_probe.h"
void setUp() {}
void tearDown() {}
void test_psram_log_capacity_and_wrap() {
    char *memory = static_cast<char *>(ps_malloc(256 * 1024));
    TEST_ASSERT_NOT_NULL(memory);
    config::Log log;
    log.begin(memory, 256 * 1024);
    for (unsigned i = 0; i < 40000; ++i) {
        log.event(i, config::Event::Boot);
    }
    TEST_ASSERT_EQUAL(256 * 1024, log.size());
    TEST_ASSERT_NOT_EQUAL(0, log.at(log.size() - 1));
    free(memory);
}
void test_atomic_history_files_reload_and_capacity() {
    TEST_ASSERT_TRUE(LittleFS.begin(false));
    hw::FileStore store("/v010-test-history.bin", "/v010-test-history.tmp",
                        "/v010-test-history.bak");
    TEST_ASSERT_TRUE(store.erase());
    auto *history = new config::History(store);
    TEST_ASSERT_TRUE(history->begin());
    for (unsigned i = 0; i < 52; ++i) {
        config::CallRecord record;
        record.duration = i;
        strcpy(record.host, "loopback");
        TEST_ASSERT_TRUE(history->append(record));
    }
    delete history;
    history = new config::History(store);
    TEST_ASSERT_TRUE(history->begin());
    TEST_ASSERT_EQUAL(50, history->size());
    TEST_ASSERT_EQUAL(2, history->at(0).duration);
    TEST_ASSERT_EQUAL(51, history->at(49).duration);
    delete history;
    File damaged = LittleFS.open("/v010-test-history.bin", "w");
    TEST_ASSERT_TRUE(bool(damaged));
    damaged.print("broken");
    damaged.close();
    history = new config::History(store);
    TEST_ASSERT_TRUE(history->begin());
    TEST_ASSERT_EQUAL(50, history->at(49).duration);
    delete history;
    TEST_ASSERT_TRUE(store.erase());
}
void test_numeric_dns_probe_and_cancel_without_network_peer() {
    hw::NetProbe probe;
    TEST_ASSERT_TRUE(probe.start("127.0.0.1", false, millis()));
    probe.poll(millis(), true);
    TEST_ASSERT_TRUE(probe.done());
    TEST_ASSERT_TRUE(probe.ok());
    TEST_ASSERT_TRUE(probe.start("127.0.0.1", false, millis()));
    probe.cancel();
    probe.poll(millis(), false);
}
void test_preview_streams_and_shuts_down_amp() {
    hw::CallAudio audio;
    auto p = modemcfg::factoryProfile(1);
    TEST_ASSERT_TRUE(audio.preview("SWEEP", p));
    uint32_t start = millis();
    while (millis() - start < 80) {
        audio.poll(millis(), p);
        delay(1);
    }
    TEST_ASSERT_TRUE(audio.running());
    TEST_ASSERT_FALSE(audio.failed());
    audio.stop();
    TEST_ASSERT_FALSE(hw::ampIsOn());
    TEST_ASSERT_FALSE(audio.preview("/sounds/../secret", p));
}
void setup() {
    delay(2000);
    WiFi.mode(WIFI_STA);
    UNITY_BEGIN();
    RUN_TEST(test_psram_log_capacity_and_wrap);
    RUN_TEST(test_atomic_history_files_reload_and_capacity);
    RUN_TEST(test_numeric_dns_probe_and_cancel_without_network_peer);
    RUN_TEST(test_preview_streams_and_shuts_down_amp);
    UNITY_END();
}
void loop() {}
