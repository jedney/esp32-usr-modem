#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>
#include "internet_network.h"
#include "nvs_store.h"
#include "wifi_radio.h"

hw::InternetNetwork network;
hw::NvsStore savedNetwork("net", "cfg");
hw::EspRadio radio;
netcfg::Net wifi(savedNetwork, radio);
void setUp() {}
void tearDown() {}
bool waitIdle() {
    const uint32_t start = millis();
    while (!network.idle() && millis() - start < 2000) {
        network.poll();
        delay(1);
    }
    return network.idle();
}
void test_source_built_sdk_preserves_board_memory() {
    TEST_ASSERT_EQUAL(240, getCpuFrequencyMhz());
    TEST_ASSERT_EQUAL(16 * 1024 * 1024, ESP.getFlashChipSize());
    // This Arduino API reports allocatable PSRAM after heap metadata, not raw density.
    TEST_ASSERT_GREATER_THAN(8 * 1024 * 1024 - 16384, ESP.getPsramSize());
    TEST_ASSERT_LESS_OR_EQUAL(8 * 1024 * 1024, ESP.getPsramSize());
    TEST_ASSERT_TRUE(IP_FORWARD && IP_NAPT && CONFIG_LWIP_L2_TO_L3_COPY);
}
void test_real_netif_nat_lifecycle_and_repeated_cleanup() {
    // Reuse netcfg's saved settings; the normal radio disables IDF credential copies.
    wifi.begin(millis());
    if (!wifi.settings().enabled || !wifi.settings().hasNetwork()) {
        TEST_IGNORE_MESSAGE("No Wi-Fi network saved: join one with AT*WIFIJOIN first");
    }
    const uint32_t start = millis();
    while (!wifi.up() && millis() - start < 15000) {
        wifi.tick(millis());
        delay(10);
    }
    TEST_ASSERT_TRUE(wifi.up());
    const uint32_t before = ESP.getFreeHeap();
    dialup::Options options;
    options.dns = 0x08080808;
    for (unsigned i = 0; i < 20; ++i) {
        TEST_ASSERT_TRUE(network.begin(options, 1500));
        delay(20);
        TEST_ASSERT_TRUE(network.up());
        network.end();
        TEST_ASSERT_TRUE(waitIdle());
    }
    // Two four-packet queues are allocated once, then reused by every call.
    TEST_ASSERT_GREATER_THAN(before - 20000, ESP.getFreeHeap());
}
void test_internet_nvs_roundtrip_uses_scratch_namespace() {
    hw::NvsStore store("test_internet", "cfg");
    dialup::Settings first(store);
    TEST_ASSERT_TRUE(first.save({0x0a404005, 0x0a404006, 0x08080808}));
    dialup::Settings second(store);
    TEST_ASSERT_TRUE(second.begin());
    TEST_ASSERT_EQUAL_HEX32(0x0a404006, second.get().peer);
    TEST_ASSERT_TRUE(store.erase());
}
void setup() {
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_source_built_sdk_preserves_board_memory);
    RUN_TEST(test_real_netif_nat_lifecycle_and_repeated_cleanup);
    RUN_TEST(test_internet_nvs_roundtrip_uses_scratch_namespace);
    UNITY_END();
}
void loop() {}
