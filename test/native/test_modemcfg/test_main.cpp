// Unit tests for the modem settings and their saved format (brief 6.2, 6.6, 6.7).
// Run with: pio test -e native
#include <unity.h>

#include <cstring>

#include "modemcfg.h"

using namespace modemcfg;

void setUp() {}
void tearDown() {}

// In-memory stand-in for NVS.
class MemStore : public Store {
public:
    size_t load(uint8_t *buf, size_t cap) override {
        size_t n = len < cap ? len : cap;
        memcpy(buf, data, n);
        return n;
    }
    bool save(const uint8_t *d, size_t n) override {
        if (n > sizeof data) {
            return false;
        }
        memcpy(data, d, n);
        len = n;
        ++saves;
        return true;
    }
    bool erase() override {
        len = 0;
        return true;
    }
    uint8_t data[1024] = {};
    size_t len = 0;
    int saves = 0;
};

// --- Factory settings ---------------------------------------------------------------------

void test_factory_f1_has_the_decided_defaults() {
    Profile p = factoryProfile(1);
    TEST_ASSERT_EQUAL(1, p.get(kE));
    TEST_ASSERT_EQUAL(0, p.get(kQ));
    TEST_ASSERT_EQUAL(1, p.get(kV));
    TEST_ASSERT_EQUAL(4, p.get(kX));  // all result codes (v0.4 decision)
    TEST_ASSERT_EQUAL(1, p.get(kM));
    TEST_ASSERT_EQUAL(2, p.get(kL));
    TEST_ASSERT_EQUAL(0, p.get(kAmpA));  // plain CONNECT result (Q34)
    TEST_ASSERT_EQUAL(1, p.get(kAmpB));  // speed stays as detected from "AT" (v0.5)
    TEST_ASSERT_EQUAL(1, p.get(kAmpC));  // Q33
    TEST_ASSERT_EQUAL(2, p.get(kAmpD));  // Q33
    TEST_ASSERT_EQUAL(1, p.get(kAmpH));  // Q33
    TEST_ASSERT_EQUAL(2, p.get(kAmpR));  // Q33
    TEST_ASSERT_EQUAL(0, p.get(kAmpS));  // DSR hard-wired
    TEST_ASSERT_EQUAL(8, p.get(kAmpN));  // 14400
    TEST_ASSERT_EQUAL(1, p.get(kTone));
    TEST_ASSERT_EQUAL_UINT32(38400, p.baud);
}

void test_factory_s_registers() {
    Profile p = factoryProfile(1);
    TEST_ASSERT_EQUAL(0, p.s[0]);
    TEST_ASSERT_EQUAL(43, p.s[2]);  // '+'
    TEST_ASSERT_EQUAL(13, p.s[3]);
    TEST_ASSERT_EQUAL(10, p.s[4]);
    TEST_ASSERT_EQUAL(8, p.s[5]);
    TEST_ASSERT_EQUAL(60, p.s[7]);
    TEST_ASSERT_EQUAL(50, p.s[12]);  // 1 s guard time (brief 6.2)
}

void test_factory_f0_and_f2_differ_only_in_flow_control_and_results() {
    Profile f0 = factoryProfile(0);
    Profile f1 = factoryProfile(1);
    Profile f2 = factoryProfile(2);
    TEST_ASSERT_EQUAL(0, f0.get(kAmpH));
    TEST_ASSERT_EQUAL(1, f0.get(kAmpR));
    TEST_ASSERT_EQUAL(1, f0.get(kX));
    TEST_ASSERT_EQUAL(2, f2.get(kAmpH));
    TEST_ASSERT_EQUAL(1, f2.get(kAmpR));
    TEST_ASSERT_EQUAL(2, f2.get(kAmpI));
    f2.v[kAmpH] = f1.v[kAmpH];
    f2.v[kAmpR] = f1.v[kAmpR];
    f2.v[kAmpI] = f1.v[kAmpI];
    TEST_ASSERT_TRUE(f1 == f2);
}

void test_factory_values_are_all_allowed() {
    for (unsigned f = 0; f <= 2; ++f) {
        Profile p = factoryProfile(f);
        for (int i = 0; i < kSettingCount; ++i) {
            TEST_ASSERT_TRUE_MESSAGE(settingAllows(static_cast<Setting>(i), p.v[i]),
                                     settingInfo(static_cast<Setting>(i)).name);
        }
    }
}

void test_factory_nvram_y0_is_f1_y1_is_f2() {
    Nvram n = factoryNvram();
    TEST_ASSERT_TRUE(n.tmpl[0] == factoryProfile(1));
    TEST_ASSERT_TRUE(n.tmpl[1] == factoryProfile(2));
    TEST_ASSERT_EQUAL(0, n.y);
    TEST_ASSERT_EQUAL_STRING("", n.phone[0]);
    TEST_ASSERT_TRUE(powerOnProfile(n) == factoryProfile(1));
}

void test_power_on_profile_follows_y() {
    Nvram n = factoryNvram();
    n.tmpl[1].v[kX] = 2;
    n.y = 1;
    TEST_ASSERT_EQUAL(2, powerOnProfile(n).get(kX));
    n.y = 2;  // factory &F0
    TEST_ASSERT_TRUE(powerOnProfile(n) == factoryProfile(0));
}

// --- Setting table ------------------------------------------------------------------------

void test_find_setting() {
    TEST_ASSERT_EQUAL(kE, findSetting(0, 'E'));
    TEST_ASSERT_EQUAL(kE, findSetting(0, 'e'));
    TEST_ASSERT_EQUAL(kAmpC, findSetting('&', 'c'));
    TEST_ASSERT_EQUAL(kAmpN, findSetting('&', 'N'));
    TEST_ASSERT_EQUAL(-1, findSetting(0, 'T'));  // P/T are commands
    TEST_ASSERT_EQUAL(-1, findSetting('&', 'Q'));
    TEST_ASSERT_EQUAL(-1, findSetting(0, 'Z'));
}

void test_ranges() {
    TEST_ASSERT_TRUE(settingAllows(kX, 4));
    TEST_ASSERT_FALSE(settingAllows(kX, 5));
    TEST_ASSERT_TRUE(settingAllows(kQ, 2));
    TEST_ASSERT_FALSE(settingAllows(kQ, 3));
    TEST_ASSERT_TRUE(settingAllows(kAmpN, 16));
    TEST_ASSERT_TRUE(settingAllows(kAmpN, 38));
    TEST_ASSERT_FALSE(settingAllows(kAmpN, 39));
    TEST_ASSERT_FALSE(settingAllows(kAmpN, 64));
    TEST_ASSERT_TRUE(settingAllows(kAmpU, 38));
    TEST_ASSERT_FALSE(settingAllows(kAmpU, 39));
    TEST_ASSERT_FALSE(settingAllows(kAmpS, 1));  // DSR is hard-wired on
    TEST_ASSERT_FALSE(settingAllows(kAmpD, 1));  // reserved
    TEST_ASSERT_FALSE(settingAllows(kAmpR, 0));  // reserved
    TEST_ASSERT_TRUE(settingAllows(kAmpI, 5));
    TEST_ASSERT_FALSE(settingAllows(kAmpI, 3));
    TEST_ASSERT_FALSE(settingAllows(kE, 200));
}

// --- Saved format -----------------------------------------------------------------------

Nvram sample() {
    Nvram n = factoryNvram();
    n.tmpl[0].v[kX] = 1;
    n.tmpl[0].s[7] = 45;
    n.tmpl[1].v[kE] = 0;
    n.tmpl[1].baud = 115200;
    n.y = 1;
    strcpy(n.phone[0], "170064142064");
    strcpy(n.phone[3], "1-800-555-0199");
    return n;
}

void test_round_trip() {
    uint8_t blob[kBlobSize];
    Nvram in = sample();
    serialize(in, blob);
    Nvram out;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kOk),
                      static_cast<int>(deserialize(blob, sizeof blob, &out)));
    TEST_ASSERT_TRUE(out.tmpl[0] == in.tmpl[0]);
    TEST_ASSERT_TRUE(out.tmpl[1] == in.tmpl[1]);
    TEST_ASSERT_EQUAL(1, out.y);
    TEST_ASSERT_EQUAL_STRING("170064142064", out.phone[0]);
    TEST_ASSERT_EQUAL_STRING("", out.phone[1]);
    TEST_ASSERT_EQUAL_STRING("1-800-555-0199", out.phone[3]);
}

void test_full_length_phone_number_survives() {
    Nvram in = factoryNvram();
    memset(in.phone[2], '9', kPhoneLen);
    in.phone[2][kPhoneLen] = '\0';
    uint8_t blob[kBlobSize];
    serialize(in, blob);
    Nvram out;
    deserialize(blob, sizeof blob, &out);
    TEST_ASSERT_EQUAL(kPhoneLen, strlen(out.phone[2]));
}

void assertLoadFails(const uint8_t *blob, size_t len, LoadResult want) {
    Nvram out = sample();
    TEST_ASSERT_EQUAL(static_cast<int>(want), static_cast<int>(deserialize(blob, len, &out)));
    TEST_ASSERT_TRUE(out.tmpl[0] == factoryProfile(1));  // falls back to factory
    TEST_ASSERT_EQUAL(0, out.y);
}

void test_damaged_blobs_fall_back_to_factory() {
    uint8_t blob[kBlobSize];
    serialize(sample(), blob);
    assertLoadFails(blob, 0, LoadResult::kEmpty);
    assertLoadFails(blob, kBlobSize - 1, LoadResult::kBadSize);

    uint8_t bad[kBlobSize];
    memcpy(bad, blob, sizeof bad);
    bad[0] = 'X';
    assertLoadFails(bad, sizeof bad, LoadResult::kBadMagic);

    memcpy(bad, blob, sizeof bad);
    bad[4] = kLayoutVersion + 1;
    assertLoadFails(bad, sizeof bad, LoadResult::kBadVersion);

    memcpy(bad, blob, sizeof bad);
    bad[20] ^= 0x01;
    assertLoadFails(bad, sizeof bad, LoadResult::kBadCrc);
}

void test_out_of_range_value_is_refused_even_with_good_crc() {
    Nvram n = factoryNvram();
    n.tmpl[0].v[kX] = 9;
    uint8_t blob[kBlobSize];
    serialize(n, blob);  // the CRC is valid, the value is not
    assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
}

void test_host_baud_round_trip_and_range() {
    Nvram in = factoryNvram();
    TEST_ASSERT_EQUAL_UINT32(0, in.hostBaud);  // detect from "AT"
    in.hostBaud = 9600;
    uint8_t blob[kBlobSize];
    serialize(in, blob);
    Nvram out;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kOk),
                      static_cast<int>(deserialize(blob, sizeof blob, &out)));
    TEST_ASSERT_EQUAL_UINT32(9600, out.hostBaud);

    in.hostBaud = 9601;  // not a host rate
    serialize(in, blob);
    assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
    TEST_ASSERT_TRUE(isHostRate(300) && isHostRate(115200));
    TEST_ASSERT_FALSE(isHostRate(230400) || isHostRate(0));
}

// A v0.4 (layout 1) blob: the layout 2 bytes without hostBaud, version 1, its own CRC.
size_t makeV1Blob(const Nvram &n, uint8_t *v1) {
    uint8_t v2[kBlobSize];
    serialize(n, v2);
    memcpy(v1, v2, kBlobSizeV1 - 4);
    v1[4] = 1;
    uint32_t crc = crc32(v1, kBlobSizeV1 - 4);
    for (int i = 0; i < 4; ++i) {
        v1[kBlobSizeV1 - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
    return kBlobSizeV1;
}

void test_layout_1_is_migrated() {
    Nvram old = sample();
    old.tmpl[0].v[kAmpB] = 0;  // v0.4's "detect from AT"
    old.tmpl[1].v[kAmpB] = 1;  // v0.4's "fixed"
    uint8_t v1[kBlobSizeV1];
    size_t len = makeV1Blob(old, v1);
    Nvram out;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kMigrated),
                      static_cast<int>(deserialize(v1, len, &out)));
    TEST_ASSERT_EQUAL(1, out.tmpl[0].get(kAmpB));
    TEST_ASSERT_EQUAL(1, out.tmpl[1].get(kAmpB));
    TEST_ASSERT_EQUAL(0, out.tmpl[1].get(kE));  // everything else kept
    TEST_ASSERT_EQUAL_UINT32(115200, out.tmpl[1].baud);
    TEST_ASSERT_EQUAL(1, out.y);
    TEST_ASSERT_EQUAL_STRING("170064142064", out.phone[0]);
    TEST_ASSERT_EQUAL_UINT32(0, out.hostBaud);
    TEST_ASSERT_NOT_NULL(strstr(loadResultText(LoadResult::kMigrated), "saved settings"));

    v1[30] ^= 0x01;  // a damaged v1 blob is still refused
    assertLoadFails(v1, len, LoadResult::kBadCrc);
    assertLoadFails(v1, kBlobSize, LoadResult::kBadSize);  // v1 header, v2 size
}

void test_crc32_known_value() {
    const char *s = "123456789";
    TEST_ASSERT_EQUAL_HEX32(0xCBF43926u, crc32(reinterpret_cast<const uint8_t *>(s), 9));
}

void test_store_round_trip_is_the_restart_case() {
    MemStore store;
    Nvram n;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kEmpty), static_cast<int>(loadNvram(store, &n)));
    n.tmpl[0].s[7] = 45;
    TEST_ASSERT_TRUE(saveNvram(store, n));
    Nvram after;  // "after a restart"
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kOk),
                      static_cast<int>(loadNvram(store, &after)));
    TEST_ASSERT_EQUAL(45, after.tmpl[0].s[7]);
}

void test_load_result_texts() {
    TEST_ASSERT_EQUAL_STRING("saved settings", loadResultText(LoadResult::kOk));
    TEST_ASSERT_NOT_NULL(strstr(loadResultText(LoadResult::kBadCrc), "damaged"));
}

void test_extended_speed_profiles_survive_restart_and_keep_old_auto() {
    MemStore store;
    Nvram n = factoryNvram();
    n.tmpl[0].v[kAmpN] = 38;
    n.tmpl[0].v[kAmpU] = 32;
    n.tmpl[1].v[kAmpN] = 0;  // old saved auto setting is kept, not rewritten to 8
    n.tmpl[1].v[kAmpU] = 16;
    TEST_ASSERT_TRUE(saveNvram(store, n));
    Nvram out;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kOk), static_cast<int>(loadNvram(store, &out)));
    TEST_ASSERT_TRUE(n.tmpl[0] == out.tmpl[0]);
    TEST_ASSERT_TRUE(n.tmpl[1] == out.tmpl[1]);
    TEST_ASSERT_EQUAL_UINT8(2, kLayoutVersion);  // the serialized fields have not changed
    n.tmpl[0].v[kAmpN] = 39;
    uint8_t blob[kBlobSize];
    serialize(n, blob);
    assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
    n.tmpl[0].v[kAmpN] = 38;
    n.tmpl[0].v[kAmpU] = 64;
    serialize(n, blob);
    assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_factory_f1_has_the_decided_defaults);
    RUN_TEST(test_factory_s_registers);
    RUN_TEST(test_factory_f0_and_f2_differ_only_in_flow_control_and_results);
    RUN_TEST(test_factory_values_are_all_allowed);
    RUN_TEST(test_factory_nvram_y0_is_f1_y1_is_f2);
    RUN_TEST(test_power_on_profile_follows_y);
    RUN_TEST(test_find_setting);
    RUN_TEST(test_ranges);
    RUN_TEST(test_round_trip);
    RUN_TEST(test_full_length_phone_number_survives);
    RUN_TEST(test_damaged_blobs_fall_back_to_factory);
    RUN_TEST(test_out_of_range_value_is_refused_even_with_good_crc);
    RUN_TEST(test_host_baud_round_trip_and_range);
    RUN_TEST(test_layout_1_is_migrated);
    RUN_TEST(test_crc32_known_value);
    RUN_TEST(test_store_round_trip_is_the_restart_case);
    RUN_TEST(test_load_result_texts);
    RUN_TEST(test_extended_speed_profiles_survive_restart_and_keep_old_auto);
    return UNITY_END();
}
