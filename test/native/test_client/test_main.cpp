#include <unity.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#define FAKES_STORE
#include "../support/fakes.h"
#include "client_service.h"
using namespace clienttest;
void setUp() {}
void tearDown() {}
using Memory = fakes::MemoryStore;
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
    memory.failSave = true;
    TEST_ASSERT_FALSE(loaded.save(1, {}));
    TEST_ASSERT_EQUAL(127, loaded.get(1).backspace);
    memory.bytes[8] ^= 1;
    Settings damaged(memory);
    TEST_ASSERT_FALSE(damaged.begin());
    TEST_ASSERT_EQUAL(80, damaged.get(1).columns);
}
void test_usb_profile_is_separate_and_old_layout_loads_defaults() {
    Memory memory;
    Settings settings(memory);
    TEST_ASSERT_TRUE(settings.save(kUsbPort, {Charset::Cp437, 100, 40, 127}));
    TEST_ASSERT_TRUE(settings.save(kRs232Port, {Charset::Plain, 40, 24, 8}));
    TEST_ASSERT_FALSE(settings.save(kPortCount, {}));
    Settings loaded(memory);
    TEST_ASSERT_TRUE(loaded.begin());
    TEST_ASSERT_EQUAL(100, loaded.get(kUsbPort).columns);
    TEST_ASSERT_EQUAL(int(Charset::Cp437), int(loaded.get(kUsbPort).charset));
    TEST_ASSERT_EQUAL(40, loaded.get(kRs232Port).columns);
    TEST_ASSERT_EQUAL(80, loaded.get(kConsolePort).columns);
    TEST_ASSERT_EQUAL_STRING("USB", portName(kUsbPort));
    TEST_ASSERT_EQUAL_STRING("RS232", portName(kRs232Port));
    TEST_ASSERT_EQUAL_STRING("CONSOLE", portName(kConsolePort));
    // A layout 1 record (two ports, before v2.0) is not converted: defaults apply.
    uint8_t old[26] = {'U', 'S', 'R', 'C', 1, 0};
    for (unsigned i = 0; i < 2; ++i) {
        uint8_t *r = old + 6 + i * 8;
        r[0] = 2;
        r[1] = 132;
        r[3] = 50;
        r[5] = 127;
    }
    uint32_t crc = crc32(old, 22);
    for (unsigned i = 0; i < 4; ++i)
        old[22 + i] = crc >> (8 * i);
    memory.bytes.assign(old, old + sizeof old);
    Settings legacy(memory);
    TEST_ASSERT_FALSE(legacy.begin());
    for (unsigned port = 0; port < kPortCount; ++port) {
        TEST_ASSERT_EQUAL(80, legacy.get(port).columns);
        TEST_ASSERT_EQUAL(8, legacy.get(port).backspace);
    }
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
void pair(Protocol protocol, uint32_t length, unsigned files = 1, size_t chunk = 1) {
    FileSource source;
    source.length = length;
    source.files = files;
    Transfer sender, receiver;
    TEST_ASSERT_TRUE(sender.start(protocol, true, &source, 0));
    TEST_ASSERT_TRUE(receiver.start(protocol, false, nullptr, 0));
    for (unsigned tick = 0; tick < 200000 && (sender.running() || receiver.running()); ++tick) {
        sender.poll(tick);
        receiver.poll(tick);
        uint8_t data[1024];
        size_t n;
        if (receiver.space() >= chunk && (n = sender.read(data, chunk)))
            TEST_ASSERT_EQUAL(n, receiver.write(data, n));
        if ((sender.space() >= chunk || !sender.running()) && (n = receiver.read(data, chunk)))
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
// 33 blocks never reach block 255. These send more than 256, so the number wraps to 0.
void test_block_numbers_wrap_after_255() {
    pair(Protocol::X1k, 257 * 1024 + 1, 1, 64);
    pair(Protocol::Xsum, 257 * 128 + 1, 1, 64);
    pair(Protocol::Y, 257 * 1024 + 1, 1, 64);
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
    memory.failSave = true;
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
// --- TG-03: hostile X/Y/ZMODEM input, table driven ----------------------------------------------
using Bytes = std::vector<uint8_t>;
// A receiver under test. send() puts bytes in and collects what the receiver says back,
// letting it speak between writes (it holds input while its output is not yet read).
struct Rx {
    Transfer t;
    explicit Rx(Protocol protocol) {
        TEST_ASSERT_TRUE(t.start(protocol, false, nullptr, 0));
        take();  // the opening 'C' or NAK, or the first ZRINIT
    }
    Bytes take() {
        Bytes out;
        uint8_t buf[256];
        size_t n;
        while ((n = t.read(buf, sizeof buf)))
            out.insert(out.end(), buf, buf + n);
        return out;
    }
    Bytes send(const Bytes &in) {
        Bytes out;
        size_t i = 0;
        for (;;) {
            size_t room = t.space();
            size_t n = t.write(in.data() + i, std::min(in.size() - i, room));
            i += n;
            Bytes more = take();
            out.insert(out.end(), more.begin(), more.end());
            if (!n && more.empty())
                break;
        }
        return out;
    }
};
Bytes cat(Bytes a, const Bytes &b) {
    a.insert(a.end(), b.begin(), b.end());
    return a;
}
Bytes xblock(uint8_t seq, uint8_t comp, size_t size = 128, uint8_t fill = 'A',
             bool goodCrc = true) {
    Bytes block{uint8_t(size == 1024 ? 2 : 1), seq, comp};
    block.resize(3 + size, fill);
    uint16_t crc = crc16(block.data() + 3, size);
    if (!goodCrc)
        crc ^= 1;
    block.push_back(uint8_t(crc >> 8));
    block.push_back(uint8_t(crc));
    return block;
}
// A YMODEM block 0 holding `name`, NUL, `rest`, padding.
Bytes ymeta(const std::string &name, const std::string &rest, size_t size = 128) {
    Bytes block{uint8_t(size == 1024 ? 2 : 1), 0, 255};
    block.insert(block.end(), name.begin(), name.end());
    if (!name.empty()) {
        block.push_back(0);
        block.insert(block.end(), rest.begin(), rest.end());
    }
    block.resize(3 + size, 0);
    uint16_t crc = crc16(block.data() + 3, size);
    block.push_back(uint8_t(crc >> 8));
    block.push_back(uint8_t(crc));
    return block;
}
const Bytes kCancelled(8, 24);
struct BlockCase {
    const char *name;
    Protocol protocol;
    Bytes input;
    Outcome outcome;
    uint32_t bytes;
    uint32_t errors;
    Bytes reply;
};
void test_xmodem_ymodem_hostile_blocks_are_nakked_ignored_or_cancel() {
    const std::string kLimit = "16777216";
    std::vector<BlockCase> cases = {
        {"good block", Protocol::Xcrc, xblock(1, 254), Outcome::Running, 128, 0, {6}},
        {"CAN bytes inside a block are data",
         Protocol::Xcrc,
         xblock(1, 254, 128, 24),
         Outcome::Running,
         128,
         0,
         {6}},
        {"block 0 is a repeat of the one before the first",
         Protocol::Xcrc,
         xblock(0, 255),
         Outcome::Running,
         0,
         0,
         {6}},
        {"repeated block is acknowledged and not counted again",
         Protocol::Xcrc,
         cat(xblock(1, 254), xblock(1, 254)),
         Outcome::Running,
         128,
         0,
         {6, 6}},
        // With no data accepted yet a retry restarts the handshake with 'C', not NAK.
        {"skipped block number", Protocol::Xcrc, xblock(2, 253), Outcome::Running, 0, 1, {'C'}},
        {"skip after a good block",
         Protocol::Xcrc,
         cat(xblock(1, 254), xblock(3, 252)),
         Outcome::Running,
         128,
         1,
         {6, 21}},
        {"old block number two back",
         Protocol::Xcrc,
         cat(cat(xblock(1, 254), xblock(2, 253)), xblock(1, 254)),
         Outcome::Running,
         256,
         1,
         {6, 6, 21}},
        {"complement does not match",
         Protocol::Xcrc,
         xblock(1, 253),
         Outcome::Running,
         0,
         1,
         {'C'}},
        {"complement equals the number",
         Protocol::Xcrc,
         xblock(1, 1),
         Outcome::Running,
         0,
         1,
         {'C'}},
        {"bad CRC", Protocol::Xcrc, xblock(1, 254, 128, 'A', false), Outcome::Running, 0, 1, {'C'}},
        {"bad CRC after a good block",
         Protocol::Xcrc,
         cat(xblock(1, 254), xblock(2, 253, 128, 'A', false)),
         Outcome::Running,
         128,
         1,
         {6, 21}},
        {"1K block with the wrong number",
         Protocol::X1k,
         xblock(5, 250, 1024),
         Outcome::Running,
         0,
         1,
         {'C'}},
        {"two CANs outside a block cancel", Protocol::Xcrc, Bytes{24, 24}, Outcome::Cancelled, 0, 0,
         kCancelled},
        {"one CAN is ignored", Protocol::Xcrc, Bytes{24}, Outcome::Running, 0, 0, {}},
        {"CANs split by another byte do not cancel",
         Protocol::Xcrc,
         Bytes{24, 'x', 24},
         Outcome::Running,
         0,
         0,
         {}},
        {"five CANs cancel", Protocol::Xcrc, Bytes(5, 24), Outcome::Cancelled, 0, 0, kCancelled},
        {"noise outside a block is ignored",
         Protocol::Xcrc,
         Bytes{'x', 0, 255, 7, 'q'},
         Outcome::Running,
         0,
         0,
         {}},
        {"eleven bad blocks give up", Protocol::Xcrc,
         [] {
             Bytes all;
             for (int i = 0; i < 11; ++i)
                 all = cat(all, xblock(1, 254, 128, 'A', false));
             return all;
         }(),
         Outcome::Timeout, 0, 11,
         [] {
             Bytes reply(10, 'C');
             reply.insert(reply.end(), 8, 24);
             return reply;
         }()},
        // YMODEM block 0
        {"good block 0", Protocol::Y, ymeta("A.BIN", "100 0 0"), Outcome::Running, 0, 0, {6, 'C'}},
        {"block 0 with no end to the name", Protocol::Y,
         [] {
             Bytes b = ymeta("", "");
             std::fill(b.begin() + 3, b.begin() + 131, 'x');
             uint16_t crc = crc16(b.data() + 3, 128);
             b[131] = uint8_t(crc >> 8);
             b[132] = uint8_t(crc);
             return b;
         }(),
         Outcome::Error, 0, 0, kCancelled},
        {"name over 128 bytes in a 1K block 0", Protocol::Y,
         ymeta(std::string(200, 'n'), "5", 1024), Outcome::Error, 0, 0, kCancelled},
        {"no size after the name", Protocol::Y, ymeta("A.BIN", ""), Outcome::Error, 0, 0,
         kCancelled},
        {"size with a letter in it", Protocol::Y, ymeta("A.BIN", "12x"), Outcome::Error, 0, 0,
         kCancelled},
        {"size too long for 32 bits", Protocol::Y, ymeta("A.BIN", "99999999999"), Outcome::Error, 0,
         0, kCancelled},
        {"size one over the 16 MiB limit", Protocol::Y, ymeta("A.BIN", "16777217"), Outcome::Error,
         0, 0, kCancelled},
        {"size exactly the 16 MiB limit",
         Protocol::Y,
         ymeta("A.BIN", kLimit),
         Outcome::Running,
         0,
         0,
         {6, 'C'}},
        {"empty name ends the batch", Protocol::Y, ymeta("", ""), Outcome::Success, 0, 0, {6}},
        {"block 0 with a bad CRC",
         Protocol::Y,
         [] {
             Bytes b = ymeta("A.BIN", "100");
             b.back() ^= 1;
             return b;
         }(),
         Outcome::Running,
         0,
         1,
         {'C'}},
        {"data block with the wrong number after block 0",
         Protocol::Y,
         cat(ymeta("A.BIN", "3000"), xblock(2, 253, 1024)),
         Outcome::Running,
         0,
         1,
         {6, 'C', 'C'}},
        {"data longer than the declared size is not accepted past it",
         Protocol::Y,
         cat(ymeta("A.BIN", "10"), xblock(1, 254, 1024)),
         Outcome::Running,
         10,
         0,
         {6, 'C', 6}},
    };
    for (const auto &c : cases) {
        Rx rx(c.protocol);
        Bytes reply = rx.send(c.input);
        TEST_ASSERT_EQUAL_MESSAGE(int(c.outcome), int(rx.t.statistics().outcome), c.name);
        TEST_ASSERT_EQUAL_MESSAGE(c.bytes, rx.t.statistics().bytes, c.name);
        TEST_ASSERT_EQUAL_MESSAGE(c.errors, rx.t.statistics().errors, c.name);
        TEST_ASSERT_EQUAL_MESSAGE(c.reply.size(), reply.size(), c.name);
        if (!reply.empty())
            TEST_ASSERT_EQUAL_MEMORY_MESSAGE(c.reply.data(), reply.data(), reply.size(), c.name);
    }
}
// A batch of 16 files is the most a YMODEM receiver takes; the 17th block 0 is refused.
void test_ymodem_receiver_refuses_the_seventeenth_file() {
    Rx rx(Protocol::Y);
    for (unsigned i = 0; i < 16; ++i) {
        TEST_ASSERT_EQUAL(2, rx.send(ymeta("F.BIN", "0")).size());
        TEST_ASSERT_EQUAL(2, rx.send(Bytes{4}).size());  // EOT: ACK and the next 'C'
        TEST_ASSERT_EQUAL(int(Outcome::Running), int(rx.t.statistics().outcome));
    }
    TEST_ASSERT_EQUAL(16, rx.t.statistics().files);
    Bytes reply = rx.send(ymeta("F.BIN", "0"));
    TEST_ASSERT_EQUAL(int(Outcome::Error), int(rx.t.statistics().outcome));
    TEST_ASSERT_EQUAL(kCancelled.size(), reply.size());
}
// EOT before all the declared bytes arrived is an error.
void test_ymodem_eot_before_the_declared_size_is_an_error() {
    Rx rx(Protocol::Y);
    rx.send(cat(ymeta("A.BIN", "300"), xblock(1, 254, 128)));
    rx.send(Bytes{4});
    TEST_ASSERT_EQUAL(int(Outcome::Error), int(rx.t.statistics().outcome));
}

// --- ZMODEM headers ---
Bytes zesc(uint8_t b) {
    if ((b & 0x60) == 0 || b == 0x7f || b == 0xff)
        return {24, uint8_t(b == 0x7f ? 'l' : b == 0xff ? 'm' : b ^ 0x40)};
    return {b};
}
Bytes zheaderBytes(uint8_t type, uint32_t pos) {
    return {type, uint8_t(pos), uint8_t(pos >> 8), uint8_t(pos >> 16), uint8_t(pos >> 24)};
}
// Hex header "**ZDLE B" + 14 hex digits + CR LF XON, as our own sender writes it.
Bytes zhex(uint8_t type, uint32_t pos, bool goodCrc = true) {
    Bytes data = zheaderBytes(type, pos);
    uint16_t crc = crc16(data.data(), 5);
    if (!goodCrc)
        crc ^= 1;
    data.push_back(uint8_t(crc >> 8));
    data.push_back(uint8_t(crc));
    Bytes out{'*', '*', 24, 'B'};
    for (uint8_t b : data) {
        out.push_back("0123456789abcdef"[b >> 4]);
        out.push_back("0123456789abcdef"[b & 15]);
    }
    out.insert(out.end(), {13, 10, 17});
    return out;
}
// Binary header, CRC-16 ('A') or CRC-32 ('C'), ZDLE escaped.
Bytes zbin(char format, uint8_t type, uint32_t pos, bool goodCrc = true) {
    Bytes data = zheaderBytes(type, pos);
    if (format == 'A') {
        uint16_t crc = crc16(data.data(), 5);
        if (!goodCrc)
            crc ^= 1;
        data.push_back(uint8_t(crc >> 8));
        data.push_back(uint8_t(crc));
    } else {
        uint32_t crc = ~crc32(data.data(), 5);
        if (!goodCrc)
            crc ^= 1;
        for (unsigned i = 0; i < 4; ++i)
            data.push_back(uint8_t(crc >> (8 * i)));
    }
    Bytes out{'*', '*', 24, uint8_t(format)};
    for (uint8_t b : data)
        out = cat(out, zesc(b));
    return out;
}
// A data subpacket ending in ZDLE `end` and a CRC-16 over the data and `end`.
Bytes zsub(const Bytes &data, uint8_t end, bool goodCrc = true) {
    Bytes out;
    for (uint8_t b : data)
        out = cat(out, zesc(b));
    out = cat(out, Bytes{24, end});
    uint16_t crc = crc16(&end, 1, crc16(data.data(), data.size()));
    if (!goodCrc)
        crc ^= 1;
    out = cat(out, zesc(uint8_t(crc >> 8)));
    return cat(out, zesc(uint8_t(crc)));
}
Bytes zname(const std::string &name, const std::string &rest) {
    Bytes b(name.begin(), name.end());
    b.push_back(0);
    b.insert(b.end(), rest.begin(), rest.end());
    b.push_back(0);
    return b;
}
// Header types in what the receiver wrote (hex headers only, as it writes them).
std::vector<unsigned> ztypes(const Bytes &out) {
    std::vector<unsigned> types;
    for (size_t i = 0; i + 6 <= out.size(); ++i)
        if (out[i] == '*' && out[i + 1] == '*' && out[i + 2] == 24 && out[i + 3] == 'B')
            types.push_back(unsigned(strtoul(
                std::string(out.begin() + i + 4, out.begin() + i + 6).c_str(), nullptr, 16)));
    return types;
}
struct ZCase {
    const char *name;
    Bytes input;
    Outcome outcome;
    uint32_t errors;
    std::vector<unsigned> replies;  // header types, in order
};
void test_zmodem_hostile_headers_are_nakked_ignored_or_cancel() {
    const Bytes good = zhex(0, 0);  // ZRQINIT; answered with ZRINIT (1)
    std::vector<ZCase> cases = {
        {"hex ZRQINIT", good, Outcome::Running, 0, {1}},
        {"hex header with a bad CRC", zhex(0, 0, false), Outcome::Running, 1, {6}},
        {"hex header with a non-hex digit",
         [&] {
             Bytes b = zhex(0, 0);
             b[6] = 'g';
             return cat(b, good);
         }(),
         Outcome::Running,
         0,
         {1}},
        {"hex header with a ZDLE escape inside",
         [&] {
             Bytes b = zhex(0, 0);
             b.insert(b.begin() + 8, {24, 'A'});
             return cat(b, good);
         }(),
         Outcome::Running,
         0,
         {1}},
        {"unknown header format letter",
         cat(Bytes{'*', '*', 24, 'D', '0', '0'}, good),
         Outcome::Running,
         0,
         {1}},
        {"header start cut short by another byte",
         cat(Bytes{'*', 'x'}, good),
         Outcome::Running,
         0,
         {1}},
        {"binary CRC-16 ZRQINIT", zbin('A', 0, 0), Outcome::Running, 0, {1}},
        {"binary CRC-16 with a bad CRC", zbin('A', 0, 0, false), Outcome::Running, 1, {6}},
        {"binary CRC-32 ZRQINIT", zbin('C', 0, 0), Outcome::Running, 0, {1}},
        {"binary CRC-32 with a bad CRC", zbin('C', 0, 0, false), Outcome::Running, 1, {6}},
        {"unknown header type", zhex(30, 0), Outcome::Running, 0, {6}},
        {"ZABORT ends the transfer", zhex(7, 0), Outcome::Error, 0, {}},
        {"ZFERR ends the transfer", zhex(12, 0), Outcome::Error, 0, {}},
        {"ZCAN ends the transfer", zhex(16, 0), Outcome::Error, 0, {}},
        {"four CANs do not cancel", Bytes(4, 24), Outcome::Running, 0, {}},
        {"five CANs cancel", Bytes(5, 24), Outcome::Cancelled, 0, {}},
        {"five CANs split by another byte do not cancel",
         cat(Bytes(4, 24), cat(Bytes{'x'}, Bytes(4, 24))),
         Outcome::Running,
         0,
         {}},
        {"ZFILE with no end to the name",
         cat(zhex(4, 0), zsub(Bytes{'a', 'b', 'c'}, 'k')),
         Outcome::Error,
         0,
         {}},
        {"ZFILE with a letter in the size",
         cat(zhex(4, 0), zsub(zname("a.bin", "12x 0 0 0"), 'k')),
         Outcome::Error,
         0,
         {}},
        {"ZFILE with a size over 16 MiB",
         cat(zhex(4, 0), zsub(zname("a.bin", "16777217"), 'k')),
         Outcome::Error,
         0,
         {}},
        {"ZFILE with an empty name", cat(zhex(4, 0), zsub(Bytes{0}, 'k')), Outcome::Error, 0, {}},
        {"ZFILE subpacket with a bad CRC",
         cat(zhex(4, 0), zsub(zname("a.bin", "100"), 'k', false)),
         Outcome::Running,
         1,
         {1}},
        {"good ZFILE is answered with ZRPOS",
         cat(zhex(4, 0), zsub(zname("a.bin", "100"), 'k')),
         Outcome::Running,
         0,
         {9}},
        {"ZDATA before any ZFILE", zhex(10, 0), Outcome::Running, 0, {9}},
        {"ZEOF before any ZFILE", zhex(11, 5), Outcome::Running, 0, {9}},
        {"ZDATA at the wrong position",
         cat(cat(zhex(4, 0), zsub(zname("a.bin", "100"), 'k')), zhex(10, 77)),
         Outcome::Running,
         0,
         {9, 9}},
        {"ZEOF at the wrong position",
         cat(cat(zhex(4, 0), zsub(zname("a.bin", "100"), 'k')), zhex(11, 99)),
         Outcome::Running,
         0,
         {9, 9}},
        {"data subpacket with a bad CRC",
         cat(cat(cat(zhex(4, 0), zsub(zname("a.bin", "100"), 'k')), zhex(10, 0)),
             zsub(Bytes(10, 'x'), 'k', false)),
         Outcome::Running,
         1,
         {9, 9}},
        {"data subpacket that never ends",
         cat(cat(cat(zhex(4, 0), zsub(zname("a.bin", "5000"), 'k')), zhex(10, 0)),
             Bytes(3000, 'x')),
         Outcome::Running,
         1,
         {9, 9}},
        {"data past the declared size",
         cat(cat(cat(zhex(4, 0), zsub(zname("a.bin", "4"), 'k')), zhex(10, 0)),
             zsub(Bytes(10, 'x'), 'k')),
         Outcome::Error,
         0,
         {9}},
    };
    for (const auto &c : cases) {
        Rx rx(Protocol::Z);
        Bytes out = rx.send(c.input);
        TEST_ASSERT_EQUAL_MESSAGE(int(c.outcome), int(rx.t.statistics().outcome), c.name);
        TEST_ASSERT_EQUAL_MESSAGE(c.errors, rx.t.statistics().errors, c.name);
        auto types = ztypes(out);
        TEST_ASSERT_EQUAL_MESSAGE(c.replies.size(), types.size(), c.name);
        for (size_t i = 0; i < types.size(); ++i)
            TEST_ASSERT_EQUAL_MESSAGE(c.replies[i], types[i], c.name);
        if (c.outcome == Outcome::Cancelled || c.outcome == Outcome::Error)
            TEST_ASSERT_FALSE_MESSAGE(rx.t.running(), c.name);
    }
}
// --- 5.5-10: only real progress keeps a transfer alive; ZFIN without "OO" is a success ---------
void test_noise_from_the_peer_does_not_postpone_the_retry_budget() {
    for (Protocol protocol : {Protocol::Xcrc, Protocol::Y, Protocol::Z}) {
        Rx rx(protocol);
        // One stray byte every 4 s, for two minutes. Silence gives up after about 55 s.
        for (uint32_t second = 1; second <= 120 && rx.t.running(); ++second) {
            rx.t.poll(second * 1000);
            if (second % 4 == 0)
                rx.send(Bytes{'x'});
            rx.take();
        }
        TEST_ASSERT_EQUAL(int(Outcome::Timeout), int(rx.t.statistics().outcome));
    }
}
void test_a_slow_peer_sending_a_block_is_not_retried() {
    Rx rx(Protocol::X1k);
    const Bytes block = xblock(1, 254, 1024);
    uint32_t ms = 0;
    for (uint8_t b : block) {  // 300 baud: one byte every 33 ms, 34 s for the block
        ms += 33;
        rx.t.poll(ms);
        rx.send(Bytes{b});
    }
    TEST_ASSERT_EQUAL(0, rx.t.statistics().retries);
    TEST_ASSERT_EQUAL(0, rx.t.statistics().errors);
    TEST_ASSERT_EQUAL(1024, rx.t.statistics().bytes);
    TEST_ASSERT_EQUAL(int(Outcome::Running), int(rx.t.statistics().outcome));
}
void test_zmodem_receiver_finishes_when_oo_after_zfin_is_missing() {
    Rx rx(Protocol::Z);
    Bytes reply = rx.send(zhex(8, 0));  // ZFIN
    TEST_ASSERT_EQUAL(1, ztypes(reply).size());
    TEST_ASSERT_EQUAL(8, ztypes(reply)[0]);
    rx.t.poll(1999);
    TEST_ASSERT_EQUAL(int(Outcome::Running), int(rx.t.statistics().outcome));
    rx.t.poll(2000);
    TEST_ASSERT_EQUAL(int(Outcome::Success), int(rx.t.statistics().outcome));
    // With the "OO" it is still immediate.
    Rx quick(Protocol::Z);
    quick.send(zhex(8, 0));
    quick.send(Bytes{'O', 'O'});
    TEST_ASSERT_EQUAL(int(Outcome::Success), int(quick.t.statistics().outcome));
}
// --- 5.5-11: the size probe finds its reply after an echoed query ------------------------------
void test_size_probe_finds_the_reply_after_an_echoed_query() {
    Memory memory;
    Settings settings(memory);
    Hardware hw;
    Service service(settings, 1, hw);
    TEST_ASSERT_TRUE(service.start("TEST", "SIZE", 0));
    drain(service);
    feed(service, "\x1b[6n");       // the terminal shows our query back
    feed(service, "\x1b[0;0R");     // a nonsense reply (zero rows)
    feed(service, "\x1b[50;132R");  // the real one
    TEST_ASSERT_EQUAL(132, service.findings().columns);
    TEST_ASSERT_EQUAL(50, service.findings().rows);
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_crc_independent_vectors);
    RUN_TEST(test_port_profiles_persist_and_rollback);
    RUN_TEST(test_usb_profile_is_separate_and_old_layout_loads_defaults);
    RUN_TEST(test_settings_validation);
    RUN_TEST(test_failed_display_does_not_replace_client_charset);
    RUN_TEST(test_x_checksum_roundtrip);
    RUN_TEST(test_x_crc_roundtrip);
    RUN_TEST(test_x_1k_binary_and_wrap);
    RUN_TEST(test_block_numbers_wrap_after_255);
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
    RUN_TEST(test_xmodem_ymodem_hostile_blocks_are_nakked_ignored_or_cancel);
    RUN_TEST(test_ymodem_receiver_refuses_the_seventeenth_file);
    RUN_TEST(test_ymodem_eot_before_the_declared_size_is_an_error);
    RUN_TEST(test_zmodem_hostile_headers_are_nakked_ignored_or_cancel);
    RUN_TEST(test_noise_from_the_peer_does_not_postpone_the_retry_budget);
    RUN_TEST(test_a_slow_peer_sending_a_block_is_not_retried);
    RUN_TEST(test_zmodem_receiver_finishes_when_oo_after_zfin_is_missing);
    RUN_TEST(test_size_probe_finds_the_reply_after_an_echoed_query);
    return UNITY_END();
}
