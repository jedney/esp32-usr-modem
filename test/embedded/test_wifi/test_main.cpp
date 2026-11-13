// On-board Wi-Fi and clock checks (v0.6, brief 6.6). They use the network saved on the
// modem with AT*WIFIJOIN (NVS namespace "net"); with none saved they are skipped. The
// saved settings are only read, never changed.
// Run with: pio test -e s3 -f embedded/test_wifi   (then upload the firmware again)
#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>

#include <cstring>

#include "netcfg.h"
#include "nvs_store.h"
#include "wifi_radio.h"

namespace {

hw::NvsStore netStore("net", "cfg");
hw::EspRadio radio;
netcfg::Net *net = nullptr;
bool haveNetwork = false;

// Runs the controller until `done` or `ms` have passed.
template <typename F>
bool runUntil(F done, uint32_t ms) {
    const uint32_t start = millis();
    while (millis() - start < ms) {
        const uint32_t now = millis();
        net->tick(now);
        if (done()) {
            return true;
        }
        delay(20);
    }
    return false;
}

void needNetwork() {
    if (!haveNetwork) {
        TEST_IGNORE_MESSAGE("No Wi-Fi network saved: join one with AT*WIFIJOIN first");
    }
}

}  // namespace

void setUp() {}
void tearDown() {}

// The settings blob survives in NVS (in its own namespace, so the real one is untouched).
void test_settings_survive_in_nvs() {
    hw::NvsStore scratch("nettest", "cfg");
    netcfg::Settings s = netcfg::defaults();
    strcpy(s.ssid, "TestNet");
    strcpy(s.pass, "not-a-real-password");
    strcpy(s.host, "test-modem");
    TEST_ASSERT_TRUE(netcfg::save(scratch, s));
    hw::NvsStore again("nettest", "cfg");
    netcfg::Settings back;
    TEST_ASSERT_EQUAL(static_cast<int>(netcfg::LoadResult::kSaved),
                      static_cast<int>(netcfg::load(again, &back)));
    TEST_ASSERT_EQUAL_STRING("TestNet", back.ssid);
    TEST_ASSERT_EQUAL_STRING("test-modem", back.host);
    TEST_ASSERT_TRUE(strcmp(s.pass, back.pass) == 0);
    TEST_ASSERT_TRUE(scratch.erase());
    TEST_ASSERT_EQUAL(static_cast<int>(netcfg::LoadResult::kEmpty),
                      static_cast<int>(netcfg::load(scratch, &back)));
}

void test_joins_the_saved_network_within_20_s() {
    needNetwork();
    TEST_ASSERT_TRUE_MESSAGE(runUntil([] { return net->up(); }, 20000), "not connected after 20 s");
    const netcfg::LinkInfo &l = net->link();
    TEST_ASSERT_NOT_EQUAL(0, l.ip);
    TEST_ASSERT_NOT_EQUAL(0, l.gateway);
    TEST_ASSERT_TRUE(l.rssi < 0);
}

void test_lights_show_connected() {
    needNetwork();
    TEST_ASSERT_TRUE(net->up());
    TEST_ASSERT_EQUAL(static_cast<int>(netcfg::Arq::kOn), static_cast<int>(net->arq()));
}

void test_dhcp_name_is_the_mdns_name() {
    needNetwork();
    TEST_ASSERT_EQUAL_STRING(net->settings().host, WiFi.getHostname());
}

void test_ntp_sets_the_clock_within_30_s() {
    needNetwork();
    TEST_ASSERT_TRUE_MESSAGE(runUntil([] { return radio.lastSync() != 0; }, 30000),
                             "no NTP answer after 30 s");
    TEST_ASSERT_TRUE(radio.utcNow() > 1767225600LL);  // after 1 Jan 2026
}

void test_scan_while_connected() {
    needNetwork();
    TEST_ASSERT_TRUE(net->scanStart(millis()));
    TEST_ASSERT_TRUE(
        runUntil([] { return net->scanState() != netcfg::ScanState::kRunning; }, 10000));
    TEST_ASSERT_EQUAL(static_cast<int>(netcfg::ScanState::kDone),
                      static_cast<int>(net->scanState()));
    bool seen = false;
    for (size_t i = 0; i < net->scanCount(); ++i) {
        if (strcmp(net->scanEntry(i).ssid, net->settings().ssid) == 0) {
            seen = true;
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(seen, "the saved network is not in the scan");
    TEST_ASSERT_TRUE(net->up());  // still connected
}

void setup() {
    delay(2000);  // give the USB-serial monitor time to attach
    net = new netcfg::Net(netStore, radio);
    net->begin(millis());
    haveNetwork = net->settings().enabled && net->settings().hasNetwork();
    UNITY_BEGIN();
    RUN_TEST(test_settings_survive_in_nvs);
    RUN_TEST(test_joins_the_saved_network_within_20_s);
    RUN_TEST(test_lights_show_connected);
    RUN_TEST(test_dhcp_name_is_the_mdns_name);
    RUN_TEST(test_ntp_sets_the_clock_within_30_s);
    RUN_TEST(test_scan_while_connected);
    UNITY_END();
}

void loop() {}
