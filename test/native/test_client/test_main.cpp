#include <unity.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "client_service.h"
using namespace clienttest;
void setUp() {}
void tearDown() {}
class Memory : public modemcfg::Store {
public:
    std::vector<uint8_t> bytes;
    bool fail = false;
    size_t load(uint8_t *out, size_t cap) override {
        size_t n = std::min(cap, bytes.size());
        if (n)
            memcpy(out, bytes.data(), n);
        return n;
    }
    bool save(const uint8_t *data, size_t len) override {
        if (fail)
            return false;
        bytes.assign(data, data + len);
        return true;
    }
    bool erase() override {
        bytes.clear();
        return true;
    }
};
class FileSource : public Source {
public:
    uint32_t length = 8193;
    unsigned files = 1;
    unsigned count() const override { return files; }
    const char *name(unsigned file) const override { return file ? "SECOND.BIN" : "TEST.BIN"; }
    uint32_t size(unsigned) const override { return length; }
    size_t read(unsigned, uint32_t offset, uint8_t *out, size_t cap) override {
        cap = std::min<uint32_t>(cap, length - offset);
        for (size_t i = 0; i < cap; ++i)
            out[i] = (offset + i) & 255;
        return cap;
    }
};
class Hardware : public Platform {
public:
    FileSource file;
    unsigned restores = 0;
    bool ready = true;
    Source *source(const char *, uint32_t) override { return &file; }
    void release() override {}
    uint32_t baud() const override { return 38400; }
    bool rts() const override { return true; }
    bool dtr() const override { return true; }
    void signal(unsigned, bool) override {}
    void restore() override { ++restores; }
    bool drained() const override { return ready; }
};
std::string drain(Service &service) {
    std::string text;
    uint8_t b[255];
    size_t n;
    while ((n = service.read(b, sizeof b)))
        text.append(reinterpret_cast<char *>(b), n);
    return text;
}
void feed(Service &service, const char *text) {
    while (*text) {
        uint8_t b = *text++;
        TEST_ASSERT_EQUAL(1, service.write(&b, 1));
    }
}
void test_crc_independent_vectors() {
    const auto *data = reinterpret_cast<const uint8_t *>("123456789");
    TEST_ASSERT_EQUAL_HEX16(0x31c3, crc16(data, 9));
    TEST_ASSERT_EQUAL_HEX32(0xcbf43926, ~crc32(data, 9));
}
void test_port_profiles_persist_and_rollback() {
    Memory memory;
    Settings settings(memory);
    TEST_ASSERT_FALSE(settings.begin());
    ClientSettings saved{Charset::Latin1, 132, 50, 127};
    TEST_ASSERT_TRUE(settings.save(1, saved));
    TEST_ASSERT_EQUAL(80, settings.get(0).columns);
    Settings loaded(memory);
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_EQUAL(132, loaded.get(1).columns);
    memory.fail = true;
    TEST_ASSERT_FALSE(loaded.save(1, {}));
    TEST_ASSERT_EQUAL(127, loaded.get(1).backspace);
    memory.bytes[8] ^= 1;
    Settings damaged(memory);
    TEST_ASSERT_FALSE(damaged.begin());
    TEST_ASSERT_EQUAL(80, damaged.get(1).columns);
}
void test_settings_validation() {
    ClientSettings s;
    TEST_ASSERT_TRUE(parseSettings("LATIN1,9999,9999,127", s));
    for (const char *invalid : {"ANSI,0,25,8", "CP437,80,0,8", "UTF8,80,25,8", "PLAIN,80,25,9",
                                "ANSI,80,25,8x", "ANSI,10000,25,8"})
        TEST_ASSERT_FALSE(parseSettings(invalid, s));
}
void test_failed_display_does_not_replace_client_charset() {
    Memory memory;
    Settings settings(memory);
    Hardware hw;
    Service service(settings, 0, hw);
    service.start("TEST", "SET=PLAIN,80,25,8", 0);
    drain(service);
    service.poll(1);
    TEST_ASSERT_TRUE(service.start("TEST", "ANSI", 2));
    drain(service);
    feed(service, "n");
    TEST_ASSERT_EQUAL(int(Charset::Plain), int(service.findings().charset));
    drain(service);
    service.poll(3);
    service.start("TEST", "SUMMARY", 4);
    auto result = drain(service);
    TEST_ASSERT_TRUE(result.find("ANSI: fail (manual)") != std::string::npos);
    TEST_ASSERT_TRUE(result.find("LATIN1: not run/unknown") != std::string::npos);
}
void pair(Protocol protocol, uint32_t length, unsigned files = 1) {
    FileSource source;
    source.length = length;
    source.files = files;
    Transfer sender, receiver;
    TEST_ASSERT_TRUE(sender.start(protocol, true, &source, 0));
    TEST_ASSERT_TRUE(receiver.start(protocol, false, nullptr, 0));
    for (unsigned tick = 0; tick < 200000 && (sender.running() || receiver.running()); ++tick) {
        sender.poll(tick);
        receiver.poll(tick);
        uint8_t data[1];
        size_t n;
        if (receiver.space() && (n = sender.read(data, sizeof data)))
            TEST_ASSERT_EQUAL(n, receiver.write(data, n));
        if ((sender.space() || !sender.running()) && (n = receiver.read(data, sizeof data)))
            if (sender.running())
                TEST_ASSERT_EQUAL(n, sender.write(data, n));
    }
    TEST_ASSERT_EQUAL(int(Outcome::Success), int(sender.statistics().outcome));
    TEST_ASSERT_EQUAL(int(Outcome::Success), int(receiver.statistics().outcome));
    TEST_ASSERT_EQUAL(length * files, sender.statistics().bytes);
    if (protocol == Protocol::Y || protocol == Protocol::Z) {
        TEST_ASSERT_EQUAL(length * files, receiver.statistics().bytes);
        TEST_ASSERT_EQUAL_HEX32(sender.statistics().crc, receiver.statistics().crc);
    }
}
void test_x_checksum_roundtrip() {
    pair(Protocol::Xsum, 257);
}
void test_x_crc_roundtrip() {
    pair(Protocol::Xcrc, 2048);
}
void test_x_1k_binary_and_wrap() {
    pair(Protocol::X1k, 32769);
}
void test_y_batch_and_length() {
    pair(Protocol::Y, 1025, 2);
}
void test_z_binary_roundtrip() {
    pair(Protocol::Z, 8193, 2);
}
void test_zero_length_files() {
    pair(Protocol::Y, 0);
    pair(Protocol::Z, 0);
}
void test_timeout_and_blocked_output_are_bounded() {
    Transfer transfer;
    TEST_ASSERT_TRUE(transfer.start(Protocol::Z, false, nullptr, 0xfffffff0u));
    for (unsigned i = 1; i <= 11; ++i)
        transfer.poll(0xfffffff0u + i * 5000);
    TEST_ASSERT_EQUAL(int(Outcome::Timeout), int(transfer.statistics().outcome));
}
void test_raw_payload_can_and_escape_are_not_cancelled() {
    pair(Protocol::X1k, 1024);
}
void test_size_fragmentation_and_fallback() {
    Memory memory;
    Settings settings(memory);
    Hardware hw;
    Service service(settings, 1, hw);
    TEST_ASSERT_TRUE(service.start("TEST", "SIZE", 0));
    TEST_ASSERT_TRUE(drain(service).find("\x1b[6n") != std::string::npos);
    feed(service, "\x1b[50;132R");
    TEST_ASSERT_EQUAL(132, service.findings().columns);
    drain(service);
    service.poll(1);
    TEST_ASSERT_FALSE(service.isOpen());
    TEST_ASSERT_TRUE(service.start("TEST", "SIZE", 2));
    drain(service);
    service.poll(1002);
    TEST_ASSERT_TRUE(drain(service).find("ruler") != std::string::npos);
    feed(service, "80,24\r");
    TEST_ASSERT_EQUAL(24, service.findings().rows);
}
void test_command_started_after_loop_timestamp_does_not_timeout() {
    Memory memory;
    Settings settings(memory);
    Hardware hw;
    Service service(settings, 0, hw);
    TEST_ASSERT_TRUE(service.start("TEST", "SIZE", 100));
    drain(service);
    service.poll(99);
    TEST_ASSERT_TRUE(service.isOpen());
    TEST_ASSERT_TRUE(drain(service).empty());
    service.poll(101);
    feed(service, "\x1b[25;80R");
    TEST_ASSERT_TRUE(drain(service).find("Measured 80 columns") != std::string::npos);
}
void test_save_requires_confirmation_and_failed_save_is_reported() {
    Memory memory;
    Settings settings(memory);
    Hardware hw;
    Service service(settings, 0, hw);
    TEST_ASSERT_TRUE(service.start("TEST", "SET=CP437,80,24,127", 0));
    drain(service);
    service.poll(1);
    TEST_ASSERT_TRUE(service.start("TEST", "APPLY", 2));
    drain(service);
    feed(service, "n");
    drain(service);
    service.poll(3);
    TEST_ASSERT_TRUE(memory.bytes.empty());
    TEST_ASSERT_TRUE(service.start("TEST", "APPLY", 4));
    drain(service);
    memory.fail = true;
    feed(service, "y");
    TEST_ASSERT_TRUE(drain(service).find("save failed") != std::string::npos);
    TEST_ASSERT_EQUAL(8, settings.get(0).backspace);
}
void test_keys_receive_raw_controls_and_end_key() {
    Memory memory;
    Settings settings(memory);
    Hardware hw;
    Service service(settings, 0, hw);
    TEST_ASSERT_TRUE(service.start("TEST", "KEYS", 0));
    drain(service);
    feed(service, "\x1b[A\x7f\r\n");
    auto out = drain(service);
    TEST_ASSERT_TRUE(out.find("1B 5B 41") != std::string::npos);
    TEST_ASSERT_TRUE(out.find("0D 0A") != std::string::npos);
    TEST_ASSERT_EQUAL(127, service.findings().backspace);
    feed(service, "\x1d");
    drain(service);
    service.poll(1);
    TEST_ASSERT_FALSE(service.isOpen());
}
void test_console_cable_unavailable_and_close_restores() {
    Memory memory;
    Settings settings(memory);
    Hardware hw;
    Service service(settings, 0, hw);
    TEST_ASSERT_TRUE(service.start("TEST", "CABLE", 0));
    TEST_ASSERT_TRUE(drain(service).find("Unavailable") != std::string::npos);
    service.poll(1);
    TEST_ASSERT_TRUE(service.start("TEST", "BELL", 2));
    service.close();
    TEST_ASSERT_GREATER_THAN(0, hw.restores);
}
void test_corrupted_block_and_lost_ack_recover_without_duplicate_payload() {
    FileSource source;
    source.length = 513;
    Transfer sender, receiver;
    sender.start(Protocol::Xcrc, true, &source, 0);
    receiver.start(Protocol::Xcrc, false, nullptr, 0);
    bool corrupted = false, lostAck = false;
    for (unsigned tick = 0; tick < 100000 && (sender.running() || receiver.running()); ++tick) {
        sender.poll(tick);
        receiver.poll(tick);
        uint8_t data[2048];
        size_t n = sender.read(data, sizeof data);
        if (n) {
            if (!corrupted && n > 10) {
                data[10] ^= 1;
                corrupted = true;
            }
            receiver.write(data, n);
        }
        n = receiver.read(data, sizeof data);
        if (n) {
            if (!lostAck && receiver.statistics().bytes) {
                lostAck = true;
                continue;
            }
            sender.write(data, n);
        }
    }
    TEST_ASSERT_TRUE(corrupted);
    TEST_ASSERT_TRUE(lostAck);
    TEST_ASSERT_EQUAL(int(Outcome::Success), int(sender.statistics().outcome));
    TEST_ASSERT_EQUAL(int(Outcome::Success), int(receiver.statistics().outcome));
    TEST_ASSERT_EQUAL(640, receiver.statistics().bytes);
    TEST_ASSERT_GREATER_THAN(0, receiver.statistics().errors);
    TEST_ASSERT_GREATER_THAN(0, sender.statistics().retries);
}
void test_batch_lost_ack_keeps_counts_and_crc_stable() {
    for (Protocol protocol : {Protocol::Y, Protocol::Z}) {
        FileSource source;
        source.length = 1025;
        Transfer sender, receiver;
        sender.start(protocol, true, &source, 0);
        receiver.start(protocol, false, nullptr, 0);
        bool dropped = false;
        for (unsigned tick = 0; tick < 100000 && (sender.running() || receiver.running()); ++tick) {
            sender.poll(tick);
            receiver.poll(tick);
            uint8_t data[4096];
            size_t n = sender.read(data, sizeof data);
            if (n && receiver.running())
                receiver.write(data, n);
            n = receiver.read(data, sizeof data);
            if (n && sender.running()) {
                bool ack = protocol == Protocol::Y ? receiver.statistics().files > 0
                                                   : n > 6 && data[4] == '0' && data[5] == '3';
                if (!dropped && ack) {
                    dropped = true;
                    continue;
                }
                sender.write(data, n);
            }
        }
        TEST_ASSERT_TRUE(dropped);
        TEST_ASSERT_EQUAL(int(Outcome::Success), int(sender.statistics().outcome));
        TEST_ASSERT_EQUAL(int(Outcome::Success), int(receiver.statistics().outcome));
        TEST_ASSERT_EQUAL(1, receiver.statistics().files);
        TEST_ASSERT_EQUAL(1025, sender.statistics().bytes);
        TEST_ASSERT_EQUAL(1025, receiver.statistics().bytes);
        TEST_ASSERT_EQUAL_HEX32(sender.statistics().crc, receiver.statistics().crc);
    }
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc_independent_vectors);
    RUN_TEST(test_port_profiles_persist_and_rollback);
    RUN_TEST(test_settings_validation);
    RUN_TEST(test_failed_display_does_not_replace_client_charset);
    RUN_TEST(test_x_checksum_roundtrip);
    RUN_TEST(test_x_crc_roundtrip);
    RUN_TEST(test_x_1k_binary_and_wrap);
    RUN_TEST(test_y_batch_and_length);
    RUN_TEST(test_z_binary_roundtrip);
    RUN_TEST(test_zero_length_files);
    RUN_TEST(test_timeout_and_blocked_output_are_bounded);
    RUN_TEST(test_raw_payload_can_and_escape_are_not_cancelled);
    RUN_TEST(test_size_fragmentation_and_fallback);
    RUN_TEST(test_command_started_after_loop_timestamp_does_not_timeout);
    RUN_TEST(test_save_requires_confirmation_and_failed_save_is_reported);
    RUN_TEST(test_keys_receive_raw_controls_and_end_key);
    RUN_TEST(test_console_cable_unavailable_and_close_restores);
    RUN_TEST(test_corrupted_block_and_lost_ack_recover_without_duplicate_payload);
    RUN_TEST(test_batch_lost_ack_keeps_counts_and_crc_stable);
    return UNITY_END();
}
