// v0.7 hardware checks. Only scratch NVS namespaces are written; no extra wiring.
#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "device_crypto.h"
#include "nvs_store.h"
#include "pins.h"

hw::EspCrypto crypto;
bool cryptoReady = false;
void setUp() {}
void tearDown() {}

void test_real_hashes_match_ota_and_fixed_digest_realm() {
    char ota[33] = {}, digest[33] = {};
    TEST_ASSERT_TRUE(crypto.hashes("password", ota, digest));
    TEST_ASSERT_EQUAL_STRING("5f4dcc3b5aa765d61d8327deb882cf99", ota);
    TEST_ASSERT_EQUAL_STRING("3ba84c8c9a93845740a46d37bf97bd5d", digest);
}

void test_random_passwords_are_distinct_and_have_the_decided_alphabet() {
    TEST_ASSERT_TRUE(cryptoReady);
    hw::NvsStore store("authtest", "cfg");
    devicecfg::Device device(store, crypto);
    char a[13] = {}, b[13] = {};
    TEST_ASSERT_TRUE(device.generate(a));
    TEST_ASSERT_TRUE(device.generate(b));
    TEST_ASSERT_TRUE(strcmp(a, b) != 0);  // never print generated secrets on failure
    for (size_t i = 0; i < 12; ++i) {
        TEST_ASSERT_NOT_NULL(strchr(devicecfg::kAlphabet, a[i]));
    }
    devicecfg::wipe(a, sizeof a);
    devicecfg::wipe(b, sizeof b);
}

void test_offline_password_generation_preserves_pot_adc() {
    analogReadResolution(12);
    TEST_ASSERT_LESS_OR_EQUAL(4095, analogRead(pins::kPot));
    hw::NvsStore store("authtest", "cfg");
    devicecfg::Device device(store, crypto);
    char password[13] = {};
    TEST_ASSERT_TRUE(device.generate(password));
    TEST_ASSERT_LESS_OR_EQUAL(4095, analogRead(pins::kPot));
    devicecfg::wipe(password, sizeof password);
}

void test_credentials_and_first_use_survive_in_nvs() {
    hw::NvsStore store("authtest", "cfg");
    TEST_ASSERT_TRUE(store.erase());
    devicecfg::Device first(store, crypto);
    TEST_ASSERT_TRUE(first.begin());
    char password[33] = {}, hash[33] = {};
    memcpy(password, first.bootPassword(), sizeof password);
    memcpy(hash, first.otaHash(), sizeof hash);
    hw::NvsStore reopened("authtest", "cfg");
    devicecfg::Device second(reopened, crypto);
    TEST_ASSERT_TRUE(second.begin());
    TEST_ASSERT_TRUE(strcmp(password, second.bootPassword()) == 0);
    TEST_ASSERT_TRUE(second.markUsed());
    devicecfg::Device used(reopened, crypto);
    TEST_ASSERT_TRUE(used.begin());
    TEST_ASSERT_EQUAL_STRING("", used.bootPassword());
    TEST_ASSERT_TRUE(strcmp(hash, used.otaHash()) == 0);
    TEST_ASSERT_TRUE(used.change("test-only-password"));
    devicecfg::Device changed(reopened, crypto);
    TEST_ASSERT_TRUE(changed.begin());
    TEST_ASSERT_EQUAL_STRING("", changed.bootPassword());
    TEST_ASSERT_TRUE(strcmp(hash, changed.otaHash()) != 0);
    TEST_ASSERT_TRUE(changed.setPolicy(devicecfg::Policy::kDemand));
    changed.openWindow(millis());
    devicecfg::Device boot(reopened, crypto);
    TEST_ASSERT_TRUE(boot.begin());
    TEST_ASSERT_EQUAL(static_cast<int>(devicecfg::Policy::kDemand),
                      static_cast<int>(boot.policy()));
    TEST_ASSERT_FALSE(boot.wantsListener(millis(), true, true));
    devicecfg::wipe(password, sizeof password);
    devicecfg::wipe(hash, sizeof hash);
    TEST_ASSERT_TRUE(store.erase());
}

void test_factory_wipes_each_project_namespace_and_generates_a_new_password() {
    hw::NvsStore auth("authtest", "cfg"), modem("modemtest", "cfg"), net("nettest", "cfg");
    TEST_ASSERT_TRUE(auth.erase());
    devicecfg::Device device(auth, crypto);
    TEST_ASSERT_TRUE(device.begin());
    char oldHash[33] = {};
    memcpy(oldHash, device.otaHash(), sizeof oldHash);
    uint8_t sample[] = {1, 2, 3};
    TEST_ASSERT_TRUE(modem.save(sample, sizeof sample));
    TEST_ASSERT_TRUE(net.save(sample, sizeof sample));
    modemcfg::Store *stores[] = {&modem, &net};
    TEST_ASSERT_TRUE(device.factoryReset(stores, 2));
    TEST_ASSERT_EQUAL(0, auth.load(sample, sizeof sample));
    TEST_ASSERT_EQUAL(0, modem.load(sample, sizeof sample));
    TEST_ASSERT_EQUAL(0, net.load(sample, sizeof sample));
    devicecfg::Device fresh(auth, crypto);
    TEST_ASSERT_TRUE(fresh.begin());
    TEST_ASSERT_EQUAL(12, strlen(fresh.bootPassword()));
    TEST_ASSERT_TRUE(strcmp(oldHash, fresh.otaHash()) != 0);
    devicecfg::wipe(oldHash, sizeof oldHash);
    TEST_ASSERT_TRUE(auth.erase());
}

void setup() {
    // Must precede ADC initialization. RF is off throughout this test program.
    cryptoReady = crypto.begin();
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_real_hashes_match_ota_and_fixed_digest_realm);
    RUN_TEST(test_random_passwords_are_distinct_and_have_the_decided_alphabet);
    RUN_TEST(test_offline_password_generation_preserves_pot_adc);
    RUN_TEST(test_credentials_and_first_use_survive_in_nvs);
    RUN_TEST(test_factory_wipes_each_project_namespace_and_generates_a_new_password);
    UNITY_END();
}

void loop() {}
