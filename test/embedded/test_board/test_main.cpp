// On-board checks for the ESP32-S3 N16R8. Run with the bench prototype connected:
//   pio test -e s3
#include <Arduino.h>
#include <esp_partition.h>
#include <unity.h>

#include "dialstring.h"

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

void setup() {
    delay(2000);  // give the USB-serial monitor time to attach
    UNITY_BEGIN();
    RUN_TEST(test_flash_is_16mb);
    RUN_TEST(test_psram_is_8mb);
    RUN_TEST(test_two_ota_slots_exist);
    RUN_TEST(test_dialstring_runs_on_target);
    UNITY_END();
}

void loop() {}
