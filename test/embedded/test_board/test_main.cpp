// On-board checks for the ESP32-S3 N16R8. Run with the bench prototype connected:
//   pio test -e s3
#include <Arduino.h>
#include <esp_partition.h>
#include <unity.h>

#include <cstring>

#include "dialstring.h"
#include "modemcfg.h"
#include "nvs_store.h"

void setUp() {}
void tearDown() {}

void test_flash_is_16mb() {
    TEST_ASSERT_EQUAL_UINT32(16u * 1024u * 1024u, ESP.getFlashChipSize());
}

void test_psram_is_8mb() {
    TEST_ASSERT_TRUE(psramFound());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT32(7u * 1024u * 1024u, ESP.getPsramSize());
}

void test_two_ota_slots_exist() {
    TEST_ASSERT_NOT_NULL(
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr));
    TEST_ASSERT_NOT_NULL(
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr));
}

void test_dialstring_runs_on_target() {
    dialstring::Result r = dialstring::classify("170064142064");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(dialstring::Kind::IpAddress), static_cast<int>(r.kind));
}

// NVS keeps the modem settings across a restart (v0.4). This uses a scratch namespace, so
// the modem's own saved settings are never touched, even if the test fails half way.
void test_settings_survive_in_nvs() {
    hw::NvsStore store("test_board", "cfg");
    TEST_ASSERT_TRUE(store.erase());

    modemcfg::Nvram n = modemcfg::factoryNvram();
    n.tmpl[0].s[7] = 45;
    n.tmpl[0].v[modemcfg::kAmpN] = 38;  // 56000, above the old 32-bit setting mask
    n.tmpl[0].v[modemcfg::kAmpU] = 32;  // 48000
    strcpy(n.phone[0], "170064142064");
    TEST_ASSERT_TRUE(modemcfg::saveNvram(store, n));

    hw::NvsStore again("test_board", "cfg");  // a fresh handle, as after a restart
    modemcfg::Nvram out;
    TEST_ASSERT_EQUAL(static_cast<int>(modemcfg::LoadResult::kOk),
                      static_cast<int>(modemcfg::loadNvram(again, &out)));
    TEST_ASSERT_EQUAL(45, out.tmpl[0].s[7]);
    TEST_ASSERT_EQUAL(38, out.tmpl[0].get(modemcfg::kAmpN));
    TEST_ASSERT_EQUAL(32, out.tmpl[0].get(modemcfg::kAmpU));
    TEST_ASSERT_EQUAL_STRING("170064142064", out.phone[0]);

    TEST_ASSERT_TRUE(store.erase());
    TEST_ASSERT_EQUAL(static_cast<int>(modemcfg::LoadResult::kEmpty),
                      static_cast<int>(modemcfg::loadNvram(store, &out)));
}

void setup() {
    delay(2000);  // give the USB-serial monitor time to attach
    UNITY_BEGIN();
    RUN_TEST(test_flash_is_16mb);
    RUN_TEST(test_psram_is_8mb);
    RUN_TEST(test_two_ota_slots_exist);
    RUN_TEST(test_dialstring_runs_on_target);
    RUN_TEST(test_settings_survive_in_nvs);
    UNITY_END();
}

void loop() {}
