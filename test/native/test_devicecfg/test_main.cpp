// Device password lifecycle, input privacy, policy timing and reset recovery. No board required.
#include <unity.h>

#include <cstring>
#include <string>
#include <vector>

#include "device_commands.h"

using namespace devicecfg;
void setUp() {}
void tearDown() {}

class Memory : public modemcfg::Store {
public:
    std::vector<uint8_t> bytes;
    bool failSave = false;
    bool failLoad = false;
    bool failErase = false;
    bool noFilesystem = false;  // FileStore::erase(): not mounted means nothing to erase
    unsigned saves = 0;
    size_t load(uint8_t *out, size_t cap) override {
        if (failLoad)
            return modemcfg::kLoadError;
        size_t n = bytes.size() < cap ? bytes.size() : cap;
        if (n != 0) {
            memcpy(out, bytes.data(), n);
        }
        return n;
    }
    bool save(const uint8_t *data, size_t n) override {
        if (failSave) {
            return false;
        }
        bytes.assign(data, data + n);
        ++saves;
        return true;
    }
    bool erase() override {
        if (failErase && !noFilesystem) {
            return false;
        }
        bytes.clear();
        return true;
    }
    bool contains(const char *text) const {
        std::string data(bytes.begin(), bytes.end());
        return data.find(text) != std::string::npos;
    }
    void fixCrc() {
        uint32_t crc = modemcfg::crc32(bytes.data(), kBlobSize - 4);
        for (size_t i = 0; i < 4; ++i) {
            bytes[kBlobSize - 4 + i] = static_cast<uint8_t>(crc >> (8 * i));
        }
    }
};

// A deliberately fake, deterministic hash; the real SHA-256 vector is checked on-board.
class FakeCrypto : public Crypto {
public:
    uint32_t next = 0;
    bool brokenRandom = false;
    bool failHash = false;
    uint32_t randomWord() override { return brokenRandom ? UINT32_MAX : next++; }
    bool hash(const char *password, char ota[kOtaHashLength + 1]) override {
        unsigned sum = 0;
        for (const char *p = password; *p; ++p) {
            sum = sum * 31 + static_cast<unsigned char>(*p);
        }
        const char *hex = "0123456789abcdef";
        for (size_t i = 0; i < kOtaHashLength; ++i) {
            ota[i] = hex[((sum + i / 8) >> (i % 8 * 4)) & 15];
        }
        ota[kOtaHashLength] = 0;
        return !failHash;
    }
};

class FakeControl : public Control {
public:
    uint32_t clock = 0;
    bool free = true;
    bool connected = true;
    bool reboot = false;
    uint32_t now() override { return clock; }
    bool idle() override { return free; }
    bool wifiUp() override { return connected; }
    void restart() override { reboot = true; }
};
class Output : public atcmd::Sink {
public:
    std::string text;
    void write(const char *data, size_t n) override { text.append(data, n); }
};

const atcmd::StarCommand table[] = {DEVICECFG_STAR_COMMANDS};
struct Rig {
    Memory auth, modemStore, netStore, bookStore;
    FakeCrypto crypto;
    Device device;
    FakeControl control;
    modemcfg::Store *stores[3];
    Commands commands;
    atcmd::Modem modem;
    Output output, otherOutput;
    atcmd::Port port, other;
    Rig()
        : device(auth, crypto),
          stores{&modemStore, &netStore, &bookStore},
          commands(device, control, stores, 3),
          modem(modemStore, table, sizeof table / sizeof table[0]),
          port(modem, output, 115200),
          other(modem, otherOutput, 38400) {
        device.begin();
        modem.begin();
        attachCommands(commands);
    }
    std::string type(const char *text) {
        output.text.clear();
        while (*text) {
            port.feed(*text++);
        }
        return output.text;
    }
};

bool has(const std::string &text, const char *part) {
    return text.find(part) != std::string::npos;
}

void test_first_boot_is_random_twelve_and_saved_for_display() {
    Memory store;
    FakeCrypto crypto;
    Device device(store, crypto);
    TEST_ASSERT_TRUE(device.begin());
    std::string password(device.bootPassword());
    TEST_ASSERT_EQUAL(12, password.size());
    for (char c : password) {
        TEST_ASSERT_NOT_NULL(strchr(kAlphabet, c));
    }
    TEST_ASSERT_TRUE(store.contains(password.c_str()));
    Device again(store, crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_EQUAL_STRING(password.c_str(), again.bootPassword());
    TEST_ASSERT_EQUAL_STRING(device.otaHash(), again.otaHash());
    TEST_ASSERT_EQUAL(1, store.saves);
}

void test_first_use_erases_plaintext_but_keeps_the_hash() {
    Rig r;
    std::string password(r.device.bootPassword()), ota(r.device.otaHash());
    TEST_ASSERT_TRUE(r.device.markUsed());
    TEST_ASSERT_EQUAL_STRING("", r.device.bootPassword());
    TEST_ASSERT_FALSE(r.auth.contains(password.c_str()));
    Device again(r.auth, r.crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_EQUAL_STRING("", again.bootPassword());
    TEST_ASSERT_EQUAL_STRING(ota.c_str(), again.otaHash());
    unsigned saves = r.auth.saves;
    TEST_ASSERT_TRUE(r.device.markUsed());
    TEST_ASSERT_EQUAL(saves, r.auth.saves);
}

void test_matches_checks_the_password_through_its_hash() {
    Rig r;
    std::string password(r.device.bootPassword());
    TEST_ASSERT_TRUE(r.device.matches(password.c_str()));
    TEST_ASSERT_TRUE(r.device.markUsed());  // the plaintext is gone; the hash still checks
    TEST_ASSERT_TRUE(r.device.matches(password.c_str()));
    TEST_ASSERT_FALSE(r.device.matches((password + "x").c_str()));
    TEST_ASSERT_FALSE(r.device.matches(""));
    TEST_ASSERT_FALSE(r.device.matches("short"));
    TEST_ASSERT_TRUE(r.device.change("NewPassword!"));
    TEST_ASSERT_FALSE(r.device.matches(password.c_str()));
    TEST_ASSERT_TRUE(r.device.matches("NewPassword!"));
    r.crypto.failHash = true;
    TEST_ASSERT_FALSE(r.device.matches("NewPassword!"));
}

void test_password_validation_preserves_spaces_and_punctuation() {
    TEST_ASSERT_TRUE(validPassword(" abc! = "));
    TEST_ASSERT_TRUE(validPassword("12345678901234567890123456789012"));
    TEST_ASSERT_FALSE(validPassword("1234567"));
    TEST_ASSERT_FALSE(validPassword("123456789012345678901234567890123"));
    TEST_ASSERT_FALSE(validPassword("abcd\tefgh"));
    TEST_ASSERT_FALSE(validPassword("abcd\177efgh"));
    TEST_ASSERT_FALSE(validPassword("abcd\200efgh"));
}

void test_change_stores_only_hashes_even_without_first_use() {
    Rig r;
    std::string original(r.device.bootPassword());
    TEST_ASSERT_TRUE(r.device.change("NewPassword!"));
    TEST_ASSERT_EQUAL_STRING("", r.device.bootPassword());
    TEST_ASSERT_FALSE(r.auth.contains("NewPassword!"));
    TEST_ASSERT_FALSE(r.auth.contains(original.c_str()));
    Device again(r.auth, r.crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_EQUAL_STRING("", again.bootPassword());
}

void test_failed_changes_keep_previous_credentials() {
    Rig r;
    std::string hash(r.device.otaHash()), boot(r.device.bootPassword());
    r.auth.failSave = true;
    TEST_ASSERT_FALSE(r.device.change("NewPassword!"));
    TEST_ASSERT_FALSE(r.device.markUsed());
    TEST_ASSERT_EQUAL_STRING(hash.c_str(), r.device.otaHash());
    TEST_ASSERT_EQUAL_STRING(boot.c_str(), r.device.bootPassword());
    r.auth.failSave = false;
    r.crypto.failHash = true;
    TEST_ASSERT_FALSE(r.device.change("NewPassword!"));
    TEST_ASSERT_EQUAL_STRING(hash.c_str(), r.device.otaHash());
}

void test_failed_initial_save_never_enables_unauthenticated_ota() {
    Memory store;
    store.failSave = true;
    FakeCrypto crypto;
    Device device(store, crypto);
    TEST_ASSERT_FALSE(device.begin());
    TEST_ASSERT_FALSE(device.ready());
    TEST_ASSERT_FALSE(device.wantsListener(0, true, true));
    TEST_ASSERT_EQUAL_STRING("", device.bootPassword());
    TEST_ASSERT_EQUAL_STRING("", device.otaHash());
}

void test_damaged_and_future_records_fail_closed_without_overwriting() {
    Rig r;
    const std::vector<uint8_t> original = r.auth.bytes;
    for (size_t index : {size_t(0), size_t(4), size_t(5), size_t(6), size_t(20), kBlobSize - 1}) {
        r.auth.bytes = original;
        r.auth.bytes[index] ^= 0xFF;
        auto damaged = r.auth.bytes;
        Device again(r.auth, r.crypto);
        TEST_ASSERT_FALSE(again.begin());
        TEST_ASSERT_FALSE(again.wantsListener(0, true, true));
        TEST_ASSERT_TRUE(damaged == r.auth.bytes);
    }
    r.auth.bytes = original;
    r.auth.bytes.push_back(0);
    Device oversized(r.auth, r.crypto);
    TEST_ASSERT_FALSE(oversized.begin());
    r.auth.bytes.resize(10);
    Device truncated(r.auth, r.crypto);
    TEST_ASSERT_FALSE(truncated.begin());
}

void test_valid_crc_cannot_hide_invalid_hash_or_missing_terminator() {
    Rig r;
    auto original = r.auth.bytes;
    // The password's terminator, the hash's first and last characters, the hash's terminator.
    for (size_t index : {size_t(39), size_t(40), size_t(103), size_t(104)}) {
        r.auth.bytes = original;
        r.auth.bytes[index] = 0xFF;
        r.auth.fixCrc();
        Device again(r.auth, r.crypto);
        TEST_ASSERT_FALSE(again.begin());
    }
}

// Layout 1: USRA, layout, policy, reset marker, password (33 bytes from 7), OTA hash
// (SHA-256 hex, 65 bytes from 40), CRC-32.
void test_record_holds_the_sha256_hash() {
    Rig r;
    TEST_ASSERT_EQUAL(109, kBlobSize);
    TEST_ASSERT_EQUAL(kBlobSize, r.auth.bytes.size());
    TEST_ASSERT_EQUAL(1, r.auth.bytes[4]);
    TEST_ASSERT_EQUAL(64, strlen(r.device.otaHash()));
    TEST_ASSERT_EQUAL_MEMORY(r.device.otaHash(), r.auth.bytes.data() + 40, 65);
    // A short OTA hash (32 characters) is refused, even with a valid CRC.
    r.auth.bytes[72] = 0;
    r.auth.fixCrc();
    Device shortOta(r.auth, r.crypto);
    TEST_ASSERT_FALSE(shortOta.begin());
}

void test_broken_entropy_is_bounded_and_rejected() {
    Rig r;
    r.crypto.brokenRandom = true;
    char password[13] = {};
    TEST_ASSERT_FALSE(r.device.generate(password));
    TEST_ASSERT_EQUAL_STRING("", password);
}

void test_ota_always_default_requires_credentials_wifi_and_idle() {
    Rig r;
    TEST_ASSERT_EQUAL(static_cast<int>(Policy::kAlways), static_cast<int>(r.device.policy()));
    TEST_ASSERT_TRUE(r.device.wantsListener(0, true, true));
    TEST_ASSERT_FALSE(r.device.wantsListener(0, false, true));
    TEST_ASSERT_FALSE(r.device.wantsListener(0, true, false));
    TEST_ASSERT_FALSE(r.device.openWindow(0));
}

void test_demand_window_expires_exactly_at_ten_minutes_and_can_reopen() {
    Rig r;
    TEST_ASSERT_TRUE(r.device.setPolicy(Policy::kDemand));
    TEST_ASSERT_FALSE(r.device.wantsListener(100, true, true));
    TEST_ASSERT_TRUE(r.device.openWindow(100));
    TEST_ASSERT_EQUAL(kWindowMs, r.device.remainingMs(100));
    TEST_ASSERT_TRUE(r.device.wantsListener(100 + kWindowMs - 1, true, true));
    TEST_ASSERT_FALSE(r.device.wantsListener(100 + kWindowMs, true, true));
    r.device.tick(100 + kWindowMs);
    TEST_ASSERT_EQUAL(0, r.device.remainingMs(100));  // no revival after a full millis cycle
    TEST_ASSERT_TRUE(r.device.openWindow(800000));
    TEST_ASSERT_TRUE(r.device.wantsListener(800001, true, true));
    r.device.closeWindow();
    TEST_ASSERT_FALSE(r.device.wantsListener(800002, true, true));
}

void test_window_crosses_millis_rollover_and_disconnect_does_not_extend_it() {
    Rig r;
    r.device.setPolicy(Policy::kDemand);
    uint32_t start = UINT32_MAX - 1000;
    r.device.openWindow(start);
    TEST_ASSERT_TRUE(r.device.wantsListener(start + 2000, true, true));
    TEST_ASSERT_EQUAL(kWindowMs - 2000, r.device.remainingMs(start + 2000));
    TEST_ASSERT_FALSE(r.device.wantsListener(start + 3000, false, true));
    TEST_ASSERT_FALSE(r.device.wantsListener(start + kWindowMs, true, true));
}

void test_policy_survives_restart_but_window_does_not() {
    Rig r;
    r.device.setPolicy(Policy::kDemand);
    r.device.openWindow(100);
    Device again(r.auth, r.crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_EQUAL(static_cast<int>(Policy::kDemand), static_cast<int>(again.policy()));
    TEST_ASSERT_FALSE(again.wantsListener(101, true, true));
    r.auth.failSave = true;
    TEST_ASSERT_FALSE(r.device.setPolicy(Policy::kAlways));
    TEST_ASSERT_EQUAL(static_cast<int>(Policy::kDemand), static_cast<int>(r.device.policy()));
    TEST_ASSERT_FALSE(r.device.setPolicy(static_cast<Policy>(99)));
}

void test_inline_password_is_masked_not_repeated_or_disclosed_by_show_commands() {
    Rig r;
    std::string text = r.type("ATE1*PASS= Secret! \r");
    TEST_ASSERT_FALSE(has(text, " Secret! "));
    TEST_ASSERT_TRUE(has(text, "OK"));
    std::string hash(r.device.otaHash());
    TEST_ASSERT_TRUE(has(r.type("A/"), "ERROR"));
    TEST_ASSERT_EQUAL_STRING(hash.c_str(), r.device.otaHash());
    for (const char *command : {"AT*OTA?\r", "AT&V\r", "ATI4\r", "ATI5\r"}) {
        text = r.type(command);
        TEST_ASSERT_FALSE(has(text, " Secret! "));
        TEST_ASSERT_FALSE(has(text, hash.c_str()));
    }
}

void test_lower_case_password_and_backspace_never_reveal_arguments() {
    Rig r;
    TEST_ASSERT_FALSE(has(r.type("at*pass=HiddenXY\bZ\r"), "Hidden"));
    char ota[kOtaHashLength + 1];
    r.crypto.hash("HiddenXZ", ota);
    TEST_ASSERT_EQUAL_STRING(ota, r.device.otaHash());
}

void test_invalid_passwords_and_control_bytes_leave_credentials_unchanged() {
    Rig r;
    std::string hash(r.device.otaHash());
    for (const char *command : {"AT*PASS=short\r", "AT*PASS?\r", "AT*PASS=abcd\tefgh\r",
                                "AT*PASS=123456789012345678901234567890123\r"}) {
        TEST_ASSERT_TRUE(has(r.type(command), "ERROR"));
        TEST_ASSERT_EQUAL_STRING(hash.c_str(), r.device.otaHash());
    }
    std::string overflow = "AT*PASS=" + std::string(200, 'x') + "\r";
    TEST_ASSERT_TRUE(has(r.type(overflow.c_str()), "ERROR"));
    TEST_ASSERT_EQUAL_STRING(hash.c_str(), r.device.otaHash());
}

void test_random_replacement_displays_once_on_requesting_port() {
    Rig r;
    std::string text = r.type("AT*PASS\r");
    size_t start = text.find("Device password: ");
    TEST_ASSERT_TRUE(start != std::string::npos);
    std::string password = text.substr(start + strlen("Device password: "), 12);
    TEST_ASSERT_EQUAL(12, password.size());
    TEST_ASSERT_FALSE(r.auth.contains(password.c_str()));
    TEST_ASSERT_EQUAL_STRING("", r.device.bootPassword());
    TEST_ASSERT_TRUE(r.otherOutput.text.empty());
    TEST_ASSERT_TRUE(has(r.type("A/"), "ERROR"));
}

void test_ota_commands_status_persistence_and_invalid_arguments() {
    Rig r;
    TEST_ASSERT_TRUE(has(r.type("AT*OTA?\r"), "ALWAYS, listening"));
    TEST_ASSERT_TRUE(has(r.type("AT*OTA=DEMAND\r"), "OK"));
    TEST_ASSERT_TRUE(has(r.type("AT*OTA=ON\r"), "OK"));
    TEST_ASSERT_TRUE(has(r.type("AT*OTA?\r"), "Window: 600 seconds"));
    r.control.clock += 300000;
    TEST_ASSERT_TRUE(has(r.type("AT*OTA?\r"), "Window: 300 seconds"));
    TEST_ASSERT_TRUE(has(r.type("AT*OTA=OFF\r"), "OK"));
    TEST_ASSERT_TRUE(has(r.type("AT*OTA?\r"), "DEMAND, closed"));
    TEST_ASSERT_TRUE(has(r.type("AT*OTA=ALWAYS\r"), "OK"));
    for (const char *c : {"AT*OTA=ON\r", "AT*OTA=OFF\r", "AT*OTA=BOGUS\r", "AT*OTA??\r"}) {
        TEST_ASSERT_TRUE(has(r.type(c), "ERROR"));
    }
    r.control.connected = false;
    TEST_ASSERT_TRUE(has(r.type("AT*OTA?\r"), "Wi-Fi disconnected"));
}

void test_factory_no_timeout_escape_and_repeat_never_erase() {
    Rig r;
    auto saved = r.auth.bytes;
    TEST_ASSERT_TRUE(has(r.type("AT*FACTORY\r"), "Y/N"));
    TEST_ASSERT_TRUE(has(r.type("N\r"), "ERROR"));
    TEST_ASSERT_TRUE(has(r.type("A/"), "ERROR"));
    r.type("AT*FACTORY\r");
    TEST_ASSERT_TRUE(has(r.type("\033"), "ERROR"));
    r.commands.poll();
    r.type("AT*FACTORY\r");
    r.control.clock += kConfirmationMs;
    r.commands.poll();
    TEST_ASSERT_FALSE(r.port.busy());
    TEST_ASSERT_TRUE(has(r.type("Y\r"), "Y"));
    TEST_ASSERT_FALSE(r.control.reboot);
    TEST_ASSERT_TRUE(saved == r.auth.bytes);
}

void test_factory_confirmation_belongs_to_issuing_port_and_rechecks_idle() {
    Rig r;
    r.type("AT*FACTORY\r");
    for (char c : std::string("AT*FACTORY\r")) {
        r.other.feed(c);
    }
    TEST_ASSERT_TRUE(has(r.otherOutput.text, "ERROR"));
    TEST_ASSERT_TRUE(r.port.busy());
    r.control.free = false;
    TEST_ASSERT_TRUE(has(r.type("Y\r"), "ERROR"));
    TEST_ASSERT_FALSE(r.auth.bytes.empty());
    TEST_ASSERT_FALSE(r.control.reboot);
}

void test_factory_erases_all_project_stores_and_blocks_commands_until_reboot() {
    Rig r;
    r.modemStore.bytes.assign(100, 42);
    r.netStore.bytes.assign(100, 43);
    r.bookStore.bytes.assign(100, 44);
    r.type("AT*FACTORY\r");
    TEST_ASSERT_TRUE(has(r.type("Y\rAT&W\r"), "Restarting"));
    TEST_ASSERT_TRUE(r.control.reboot);
    TEST_ASSERT_TRUE(r.auth.bytes.empty());
    TEST_ASSERT_TRUE(r.modemStore.bytes.empty());
    TEST_ASSERT_TRUE(r.netStore.bytes.empty());
    TEST_ASSERT_TRUE(r.bookStore.bytes.empty());
    TEST_ASSERT_TRUE(r.modem.commandsStopped());
    r.port.execute("&W");  // direct command users must also respect the pending reboot
    TEST_ASSERT_TRUE(r.modemStore.bytes.empty());
    TEST_ASSERT_FALSE(r.device.wantsListener(0, true, true));
    Device again(r.auth, r.crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_EQUAL(12, strlen(again.bootPassword()));
}

void test_factory_interruption_is_retried_after_restart() {
    Rig r;
    r.netStore.bytes.assign(20, 1);
    r.bookStore.bytes.assign(50, 2);
    r.bookStore.failErase = true;
    TEST_ASSERT_FALSE(r.device.factoryReset(r.stores, 3));
    TEST_ASSERT_TRUE(r.device.needsReset());
    TEST_ASSERT_FALSE(r.device.ready());
    Device again(r.auth, r.crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_TRUE(again.needsReset());
    TEST_ASSERT_FALSE(again.wantsListener(0, true, true));
    TEST_ASSERT_FALSE(again.change("NewPassword!"));
    r.bookStore.failErase = false;
    TEST_ASSERT_TRUE(again.factoryReset(r.stores, 3));
    TEST_ASSERT_TRUE(r.auth.bytes.empty());
    TEST_ASSERT_TRUE(r.netStore.bytes.empty());
    TEST_ASSERT_TRUE(r.bookStore.bytes.empty());
}

void test_factory_marker_failure_does_not_erase_anything() {
    Rig r;
    r.netStore.bytes.assign(20, 1);
    r.auth.failSave = true;
    TEST_ASSERT_FALSE(r.device.factoryReset(r.stores, 3));
    TEST_ASSERT_EQUAL(20, r.netStore.bytes.size());
    TEST_ASSERT_TRUE(r.device.ready());
}

void test_factory_last_erase_failure_keeps_recovery_marker() {
    Rig r;
    r.auth.failErase = true;
    TEST_ASSERT_FALSE(r.device.factoryReset(r.stores, 3));
    Device again(r.auth, r.crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_TRUE(again.needsReset());
    r.auth.failErase = false;
    TEST_ASSERT_TRUE(again.factoryReset(r.stores, 3));
}

void test_factory_completes_when_a_store_has_no_filesystem() {
    Rig r;
    r.bookStore.failErase = true;     // would stop the reset ...
    r.bookStore.noFilesystem = true;  // ... but "not mounted" counts as erased
    TEST_ASSERT_TRUE(r.device.factoryReset(r.stores, 3));
    Device again(r.auth, r.crypto);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_FALSE(again.needsReset());
}

void test_modem_factory_templates_and_reset_keep_device_credentials() {
    Rig r;
    auto saved = r.auth.bytes;
    r.type("AT&F1&W\r");
    r.type("ATZ\r");
    TEST_ASSERT_TRUE(saved == r.auth.bytes);
}

void test_help_includes_every_device_command_without_credentials() {
    Rig r;
    atcmd::showStarHelp(r.port);
    TEST_ASSERT_TRUE(has(r.output.text, "AT*PASS"));
    TEST_ASSERT_TRUE(has(r.output.text, "AT*OTA"));
    TEST_ASSERT_TRUE(has(r.output.text, "AT*FACTORY"));
    TEST_ASSERT_FALSE(has(r.output.text, r.device.bootPassword()));
    TEST_ASSERT_FALSE(has(r.output.text, r.device.otaHash()));
}

void test_password_separator_and_only_value_masked() {
    Rig r;
    std::string text = r.type("AT*PASS=abc=defg\r");
    TEST_ASSERT_TRUE(text.find("AT*PASS=********\r") == 0);
    TEST_ASSERT_FALSE(has(text, "abc=defg"));
    TEST_ASSERT_TRUE(has(r.type("A/"), "ERROR"));
    text = r.type("at*pass=abcdefgh\x7fZ\r");
    TEST_ASSERT_TRUE(text.find("at*pass=********\b \b*\r") == 0);
    TEST_ASSERT_TRUE(text.find("at*pass=") == 0);
    TEST_ASSERT_FALSE(has(text, "abcdefgh"));
    r.type("ATE0\r");
    text = r.type("AT*PASS=abcdefgh\r");
    TEST_ASSERT_EQUAL_STRING("\r\nDevice password changed.\r\n\r\nOK\r\n", text.c_str());
    r.type("ATE1\r");
    text = r.type("AT*PASS=\b=abcdefgh\r");
    TEST_ASSERT_TRUE(text.find("AT*PASS=\b \b=********\r") == 0);
    TEST_ASSERT_FALSE(has(text, "abcdefgh"));
}

void test_browser_password_generation_discloses_once_to_requesting_port() {
    Rig r;
    r.port.setBrowser(true);
    std::string hash = r.device.otaHash();
    auto text = r.type("AT*PASS\r");
    TEST_ASSERT_TRUE(has(text, "Device password:"));
    TEST_ASSERT_TRUE(has(text, "OK"));
    TEST_ASSERT_FALSE(hash == r.device.otaHash());
    text = r.type("AT*PASS=WriteOnlyExample\r");
    TEST_ASSERT_FALSE(has(text, "WriteOnlyExample"));
    TEST_ASSERT_TRUE(has(text, "OK"));
    TEST_ASSERT_EQUAL_STRING("", r.device.bootPassword());
}

void test_unreadable_device_record_never_generates_replacement_credentials() {
    Memory memory;
    FakeCrypto crypto;
    Device first(memory, crypto);
    TEST_ASSERT_TRUE(first.begin());
    const auto bytes = memory.bytes;
    const unsigned saves = memory.saves;
    memory.failLoad = true;
    Device reload(memory, crypto);
    TEST_ASSERT_FALSE(reload.begin());
    TEST_ASSERT_FALSE(reload.ready());
    TEST_ASSERT_TRUE(bytes == memory.bytes);
    TEST_ASSERT_EQUAL(saves, memory.saves);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_unreadable_device_record_never_generates_replacement_credentials);

    RUN_TEST(test_browser_password_generation_discloses_once_to_requesting_port);
    RUN_TEST(test_password_separator_and_only_value_masked);
    RUN_TEST(test_first_boot_is_random_twelve_and_saved_for_display);
    RUN_TEST(test_first_use_erases_plaintext_but_keeps_the_hash);
    RUN_TEST(test_password_validation_preserves_spaces_and_punctuation);
    RUN_TEST(test_matches_checks_the_password_through_its_hash);
    RUN_TEST(test_change_stores_only_hashes_even_without_first_use);
    RUN_TEST(test_failed_changes_keep_previous_credentials);
    RUN_TEST(test_failed_initial_save_never_enables_unauthenticated_ota);
    RUN_TEST(test_damaged_and_future_records_fail_closed_without_overwriting);
    RUN_TEST(test_valid_crc_cannot_hide_invalid_hash_or_missing_terminator);
    RUN_TEST(test_record_holds_the_sha256_hash);
    RUN_TEST(test_broken_entropy_is_bounded_and_rejected);
    RUN_TEST(test_ota_always_default_requires_credentials_wifi_and_idle);
    RUN_TEST(test_demand_window_expires_exactly_at_ten_minutes_and_can_reopen);
    RUN_TEST(test_window_crosses_millis_rollover_and_disconnect_does_not_extend_it);
    RUN_TEST(test_policy_survives_restart_but_window_does_not);
    RUN_TEST(test_inline_password_is_masked_not_repeated_or_disclosed_by_show_commands);
    RUN_TEST(test_lower_case_password_and_backspace_never_reveal_arguments);
    RUN_TEST(test_invalid_passwords_and_control_bytes_leave_credentials_unchanged);
    RUN_TEST(test_random_replacement_displays_once_on_requesting_port);
    RUN_TEST(test_ota_commands_status_persistence_and_invalid_arguments);
    RUN_TEST(test_factory_no_timeout_escape_and_repeat_never_erase);
    RUN_TEST(test_factory_confirmation_belongs_to_issuing_port_and_rechecks_idle);
    RUN_TEST(test_factory_erases_all_project_stores_and_blocks_commands_until_reboot);
    RUN_TEST(test_factory_interruption_is_retried_after_restart);
    RUN_TEST(test_factory_marker_failure_does_not_erase_anything);
    RUN_TEST(test_factory_last_erase_failure_keeps_recovery_marker);
    RUN_TEST(test_factory_completes_when_a_store_has_no_filesystem);
    RUN_TEST(test_modem_factory_templates_and_reset_keep_device_credentials);
    RUN_TEST(test_help_includes_every_device_command_without_credentials);
    return UNITY_END();
}
