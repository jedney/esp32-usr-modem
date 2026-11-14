// Unit tests for the modem settings and their saved format (spec 3.2, 3.6, 3.7).
// Run with: pio test -e native
#include <unity.h>

#include <cstring>
#include <initializer_list>
#include <vector>

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
    TEST_ASSERT_EQUAL(4, p.get(kX));  // all result codes
    TEST_ASSERT_EQUAL(1, p.get(kM));
    TEST_ASSERT_EQUAL(2, p.get(kL));
    TEST_ASSERT_EQUAL(0, p.get(kAmpA));  // plain CONNECT result
    TEST_ASSERT_EQUAL(1, p.get(kAmpB));  // speed stays as detected from "AT"
    TEST_ASSERT_EQUAL(1, p.get(kAmpC));  // DCD follows the connection
    TEST_ASSERT_EQUAL(2, p.get(kAmpD));  // DTR drop hangs up
    TEST_ASSERT_EQUAL(1, p.get(kAmpH));  // hardware transmit flow control
    TEST_ASSERT_EQUAL(2, p.get(kAmpR));  // RTS receive flow control
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
    TEST_ASSERT_EQUAL(50, p.s[12]);  // 1 s guard time (spec 3.2)
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

void test_stored_line_end_registers_out_of_range_fall_back_to_factory() {
    uint8_t blob[kBlobSize];
    const struct {
        int reg;
        uint8_t value;
    } bad[] = {{3, 49}, {3, 0x20}, {3, 0x7E}, {3, 200}, {4, 128}, {5, 255}};
    for (const auto &b : bad) {
        Nvram n = factoryNvram();
        n.tmpl[0].s[b.reg] = b.value;
        serialize(n, blob);
        assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
    }
    Nvram ok = factoryNvram();
    ok.tmpl[0].s[3] = 0x1F;
    ok.tmpl[0].s[4] = 127;
    ok.tmpl[0].s[5] = 0;
    serialize(ok, blob);
    Nvram out;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kOk),
                      static_cast<int>(deserialize(blob, sizeof blob, &out)));
}

class FailingStore : public MemStore {
public:
    size_t load(uint8_t *, size_t) override { return kLoadError; }
};

void test_unreadable_store_is_a_bad_size_and_stays_untouched() {
    FailingStore store;
    Nvram n;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kBadSize),
                      static_cast<int>(loadNvram(store, &n)));
    TEST_ASSERT_EQUAL(0, store.saves);
    TEST_ASSERT_EQUAL(0, static_cast<int>(store.len));
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

void test_extended_speed_profiles_survive_restart() {
    MemStore store;
    Nvram n = factoryNvram();
    n.tmpl[0].v[kAmpN] = 38;
    n.tmpl[0].v[kAmpU] = 32;
    n.tmpl[1].v[kAmpN] = 0;  // highest speed, kept as saved
    n.tmpl[1].v[kAmpU] = 16;
    TEST_ASSERT_TRUE(saveNvram(store, n));
    Nvram out;
    TEST_ASSERT_EQUAL(static_cast<int>(LoadResult::kOk), static_cast<int>(loadNvram(store, &out)));
    TEST_ASSERT_TRUE(n.tmpl[0] == out.tmpl[0]);
    TEST_ASSERT_TRUE(n.tmpl[1] == out.tmpl[1]);
    n.tmpl[0].v[kAmpN] = 39;
    uint8_t blob[kBlobSize];
    serialize(n, blob);
    assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
    n.tmpl[0].v[kAmpN] = 38;
    n.tmpl[0].v[kAmpU] = 64;
    serialize(n, blob);
    assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
}

// Seals `blob` again after a byte was changed, so only the value is wrong.
void reseal(uint8_t *blob) {
    const uint32_t crc = crc32(blob, kBlobSize - 4);
    for (unsigned i = 0; i < 4; ++i) {
        blob[kBlobSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
    }
}

void test_model_throttle_and_line_speed_flag_survive_restart() {
    Nvram n = factoryNvram();
    n.model = 56000;
    n.throttle = true;
    n.tmpl[0].lineExplicit = true;
    n.tmpl[0].s[0] = 3;
    uint8_t blob[kBlobSize];
    serialize(n, blob);
    Nvram out;
    TEST_ASSERT_EQUAL(int(LoadResult::kOk), int(deserialize(blob, sizeof blob, &out)));
    TEST_ASSERT_EQUAL_UINT32(56000, out.model);
    TEST_ASSERT_TRUE(out.throttle);
    TEST_ASSERT_TRUE(out.tmpl[0].lineExplicit);
    TEST_ASSERT_FALSE(out.tmpl[1].lineExplicit);
    TEST_ASSERT_EQUAL(3, out.tmpl[0].s[0]);
}

void test_flag_bytes_above_one_are_refused() {
    // The bytes after the phone numbers and hostBaud: two lineExplicit flags, the model (4
    // bytes), the throttle flag, the answer port and the two call delays, then the CRC.
    const size_t lineExplicit0 = kBlobSize - 4 - 8 - 1 - 1 - 4 - 1 - kTemplates;
    const size_t throttle = kBlobSize - 4 - 8 - 1 - 1;
    for (size_t index : {lineExplicit0, lineExplicit0 + 1, throttle}) {
        uint8_t blob[kBlobSize];
        serialize(factoryNvram(), blob);
        blob[index] = 2;
        reseal(blob);
        assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
    }
}

void test_answer_port_values_load_as_themselves_and_larger_are_refused() {
    Nvram n = factoryNvram();
    n.answerPort = AnswerPort::kConsole;
    uint8_t blob[kBlobSize];
    serialize(n, blob);
    Nvram out;
    TEST_ASSERT_EQUAL(int(LoadResult::kOk), int(deserialize(blob, sizeof blob, &out)));
    TEST_ASSERT_EQUAL(int(AnswerPort::kConsole), int(out.answerPort));
    const size_t answerPort = kBlobSize - 4 - 8 - 1;
    for (uint8_t value = 0; value < kAnswerPortCount; ++value) {
        blob[answerPort] = value;
        reseal(blob);
        TEST_ASSERT_EQUAL(int(LoadResult::kOk), int(deserialize(blob, sizeof blob, &out)));
        TEST_ASSERT_EQUAL(value, static_cast<uint8_t>(out.answerPort));
    }
    blob[answerPort] = kAnswerPortCount;
    reseal(blob);
    assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
}

void test_answer_port_names_parse_and_factory_default() {
    TEST_ASSERT_EQUAL(int(AnswerPort::kAuto), int(factoryNvram().answerPort));
    const char *names[] = {"RS232", "CONSOLE", "USB", "AUTO"};
    for (uint8_t i = 0; i < kAnswerPortCount; ++i) {
        TEST_ASSERT_EQUAL_STRING(names[i], answerPortName(static_cast<AnswerPort>(i)));
        AnswerPort out = AnswerPort::kRs232;
        TEST_ASSERT_TRUE(parseAnswerPort(names[i], &out));
        TEST_ASSERT_EQUAL(i, static_cast<uint8_t>(out));
    }
    AnswerPort out = AnswerPort::kRs232;
    TEST_ASSERT_TRUE(parseAnswerPort("usb  ", &out));
    TEST_ASSERT_EQUAL(int(AnswerPort::kUsb), int(out));
    TEST_ASSERT_TRUE(parseAnswerPort("Auto", &out));
    TEST_ASSERT_EQUAL(int(AnswerPort::kAuto), int(out));
    for (const char *bad : {"", "US", "USBX", "RS23", "CONSOLE2", " USB"}) {
        out = AnswerPort::kRs232;
        TEST_ASSERT_FALSE(parseAnswerPort(bad, &out));
        TEST_ASSERT_EQUAL(int(AnswerPort::kRs232), int(out));
    }
}

void test_call_delays_round_trip_and_are_limited() {
    Nvram n = factoryNvram();
    TEST_ASSERT_EQUAL_UINT32(1500, n.ringDelayMs);
    TEST_ASSERT_EQUAL_UINT32(250, n.connectDelayMs);
    n.ringDelayMs = 5000;
    n.connectDelayMs = 60000;
    uint8_t blob[kBlobSize];
    serialize(n, blob);
    Nvram out;
    TEST_ASSERT_EQUAL(int(LoadResult::kOk), int(deserialize(blob, sizeof blob, &out)));
    TEST_ASSERT_EQUAL_UINT32(5000, out.ringDelayMs);
    TEST_ASSERT_EQUAL_UINT32(60000, out.connectDelayMs);
    for (bool ring : {false, true}) {
        (ring ? n.ringDelayMs : n.connectDelayMs) = 60001;
        serialize(n, blob);
        assertLoadFails(blob, sizeof blob, LoadResult::kBadValue);
        n = factoryNvram();
    }
}

// --- Recovering a blob saved through file, temporary file and backup -----------------

// A blob ending in its CRC-32, `size` bytes long in all.
std::vector<uint8_t> sealed(uint8_t fill, size_t size = 20) {
    std::vector<uint8_t> blob(size, fill);
    const uint32_t crc = crc32(blob.data(), size - 4);
    for (unsigned i = 0; i < 4; ++i)
        blob[size - 4 + i] = uint8_t(crc >> (8 * i));
    return blob;
}
struct Files {
    std::vector<uint8_t> file, temporary, backup;
};
size_t readSlot(void *context, Slot slot, uint8_t *out, size_t cap) {
    const Files &f = *static_cast<Files *>(context);
    const auto &blob = slot == Slot::kFile        ? f.file
                       : slot == Slot::kTemporary ? f.temporary
                                                  : f.backup;
    // The reader does what hw::FileStore does: only a complete, CRC-sealed file counts.
    if (blob.size() > cap || !crcSealed(blob.data(), blob.size()))
        return 0;
    memcpy(out, blob.data(), blob.size());
    return blob.size();
}
void test_crc_sealed_checks_the_trailing_crc() {
    auto blob = sealed(7);
    TEST_ASSERT_TRUE(crcSealed(blob.data(), blob.size()));
    blob[3] ^= 1;
    TEST_ASSERT_FALSE(crcSealed(blob.data(), blob.size()));
    blob = sealed(7);
    blob[blob.size() - 1] ^= 1;
    TEST_ASSERT_FALSE(crcSealed(blob.data(), blob.size()));
    TEST_ASSERT_FALSE(crcSealed(blob.data(), 4));  // no room for data and a CRC
    TEST_ASSERT_FALSE(crcSealed(blob.data(), 0));
}
void test_recovery_prefers_file_then_temporary_then_backup() {
    uint8_t out[64];
    Slot used = Slot::kBackup;
    Files f{sealed(1), sealed(2), sealed(3)};
    TEST_ASSERT_EQUAL(20, recoverBlob(readSlot, &f, out, sizeof out, &used));
    TEST_ASSERT_EQUAL(int(Slot::kFile), int(used));
    TEST_ASSERT_EQUAL(1, out[0]);
    // Power lost between the two renames: no file, the newest blob is the temporary file.
    f.file.clear();
    TEST_ASSERT_EQUAL(20, recoverBlob(readSlot, &f, out, sizeof out, &used));
    TEST_ASSERT_EQUAL(int(Slot::kTemporary), int(used));
    TEST_ASSERT_EQUAL(2, out[0]);
    // The file is there but damaged inside: the temporary file, then the backup, are tried.
    f.file = sealed(1);
    f.file[5] ^= 0x40;
    TEST_ASSERT_EQUAL(20, recoverBlob(readSlot, &f, out, sizeof out, &used));
    TEST_ASSERT_EQUAL(int(Slot::kTemporary), int(used));
    // Power lost while the temporary file was written: part of it is no blob. The backup
    // is the old blob when there is no file, and nothing newer was ever complete.
    f.file.clear();
    f.temporary = sealed(2);
    f.temporary.resize(11);
    TEST_ASSERT_EQUAL(20, recoverBlob(readSlot, &f, out, sizeof out, &used));
    TEST_ASSERT_EQUAL(int(Slot::kBackup), int(used));
    TEST_ASSERT_EQUAL(3, out[0]);
    // A good file wins over a half-written temporary file.
    f.file = sealed(1);
    TEST_ASSERT_EQUAL(20, recoverBlob(readSlot, &f, out, sizeof out, &used));
    TEST_ASSERT_EQUAL(int(Slot::kFile), int(used));
}
void test_recovery_returns_nothing_when_no_slot_has_a_good_blob() {
    uint8_t out[64];
    Slot used = Slot::kFile;
    Files none;
    TEST_ASSERT_EQUAL(0, recoverBlob(readSlot, &none, out, sizeof out, &used));
    Files bad{sealed(1), sealed(2), sealed(3)};
    bad.file[0] ^= 1;
    bad.temporary[0] ^= 1;
    bad.backup[0] ^= 1;
    TEST_ASSERT_EQUAL(0, recoverBlob(readSlot, &bad, out, sizeof out, nullptr));
    uint8_t tiny[8];  // a blob too big for the buffer does not count
    Files big{sealed(1), {}, {}};
    TEST_ASSERT_EQUAL(0, recoverBlob(readSlot, &big, tiny, sizeof tiny));
}

void test_product_name_follows_the_model() {
    TEST_ASSERT_EQUAL_STRING("US Robotics Sportster 14.4k", productName(14400));
    TEST_ASSERT_EQUAL_STRING("US Robotics Sportster 28.8k", productName(28800));
    TEST_ASSERT_EQUAL_STRING("US Robotics Sportster 33.6k", productName(33600));
    TEST_ASSERT_EQUAL_STRING("US Robotics Sportster 56k", productName(56000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_call_delays_round_trip_and_are_limited);
    RUN_TEST(test_answer_port_names_parse_and_factory_default);
    RUN_TEST(test_answer_port_values_load_as_themselves_and_larger_are_refused);
    RUN_TEST(test_flag_bytes_above_one_are_refused);
    RUN_TEST(test_model_throttle_and_line_speed_flag_survive_restart);
    RUN_TEST(test_stored_line_end_registers_out_of_range_fall_back_to_factory);
    RUN_TEST(test_unreadable_store_is_a_bad_size_and_stays_untouched);
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
    RUN_TEST(test_crc32_known_value);
    RUN_TEST(test_store_round_trip_is_the_restart_case);
    RUN_TEST(test_load_result_texts);
    RUN_TEST(test_crc_sealed_checks_the_trailing_crc);
    RUN_TEST(test_recovery_prefers_file_then_temporary_then_backup);
    RUN_TEST(test_recovery_returns_nothing_when_no_slot_has_a_good_blob);
    RUN_TEST(test_extended_speed_profiles_survive_restart);
    RUN_TEST(test_product_name_follows_the_model);
    return UNITY_END();
}
