// Hardware checks for the device password and OTA policy. Only scratch NVS namespaces are written;
// no extra wiring.
#include <Arduino.h>
#include <unity.h>

#include <cstring>

#include "device_crypto.h"
#include "nvs_store.h"
#include "pins.h"
#include "web_policy.h"

hw::EspCrypto crypto;
bool cryptoReady = false;
void setUp() {}
void tearDown() {}

void test_real_hash_is_sha256() {
    char ota[devicecfg::kOtaHashLength + 1] = {};
    TEST_ASSERT_TRUE(crypto.hash("password", ota));
    // SHA-256("password"): the OTA and login hash.
    TEST_ASSERT_EQUAL_STRING("5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8",
                             ota);
}

void test_random_words_are_distinct_and_never_the_rejection_value() {
    TEST_ASSERT_TRUE(cryptoReady);
    uint32_t words[64];
    for (size_t i = 0; i < 64; ++i) {
        words[i] = crypto.randomWord();
        TEST_ASSERT_TRUE(words[i] != UINT32_MAX);
        for (size_t j = 0; j < i; ++j) {
            TEST_ASSERT_TRUE(words[i] != words[j]);  // 32-bit collision odds: about 5 in 10 million
        }
    }
}

void test_random_words_are_refused_until_the_boot_seed_is_drawn() {
    hw::EspCrypto unseeded;
    TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, unseeded.randomWord());
    TEST_ASSERT_TRUE(crypto.begin());  // a second begin() keeps the seed it has
    TEST_ASSERT_TRUE(crypto.randomWord() != UINT32_MAX);
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
    char password[devicecfg::kPasswordMax + 1] = {}, hash[devicecfg::kOtaHashLength + 1] = {};
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
    char oldHash[devicecfg::kOtaHashLength + 1] = {};
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

void test_oversized_nvs_is_not_missing_and_preserves_credentials() {
    hw::NvsStore store("review_nvs", "cfg");
    TEST_ASSERT_TRUE(store.erase());
    uint8_t small[10] = {};
    TEST_ASSERT_EQUAL(0, store.load(small, sizeof small));
    uint8_t oversized[devicecfg::kBlobSize + 2] = {};
    TEST_ASSERT_TRUE(store.save(oversized, sizeof oversized));
    TEST_ASSERT_EQUAL(modemcfg::kLoadError, store.load(small, sizeof small));
    webinterface::Settings web(store);
    TEST_ASSERT_FALSE(web.begin());
    TEST_ASSERT_FALSE(web.enabled());
    devicecfg::Device device(store, crypto);
    TEST_ASSERT_FALSE(device.begin());
    TEST_ASSERT_FALSE(device.ready());
    uint8_t after[sizeof oversized] = {};
    TEST_ASSERT_EQUAL(sizeof oversized, store.load(after, sizeof after));
    TEST_ASSERT_EQUAL_MEMORY(oversized, after, sizeof after);
    TEST_ASSERT_TRUE(store.erase());
}

void setup() {
    // Must precede ADC initialization. RF is off throughout this test program.
    cryptoReady = crypto.begin();
    delay(2000);
    UNITY_BEGIN();
    RUN_TEST(test_oversized_nvs_is_not_missing_and_preserves_credentials);
    RUN_TEST(test_real_hash_is_sha256);
    RUN_TEST(test_random_words_are_distinct_and_never_the_rejection_value);
    RUN_TEST(test_random_words_are_refused_until_the_boot_seed_is_drawn);
    RUN_TEST(test_random_passwords_are_distinct_and_have_the_decided_alphabet);
    RUN_TEST(test_offline_password_generation_preserves_pot_adc);
    RUN_TEST(test_credentials_and_first_use_survive_in_nvs);
    RUN_TEST(test_factory_wipes_each_project_namespace_and_generates_a_new_password);
    UNITY_END();
}

void loop() {}
