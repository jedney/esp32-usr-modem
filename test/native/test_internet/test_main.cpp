#include <unity.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#define FAKES_STORE
#define FAKES_SINK
#define FAKES_SOUND
#include "../support/fakes.h"
#include "input_buffer.h"
#include "internet.h"
#include "internet_dialer.h"
#include "session.h"

using Bytes = std::vector<uint8_t>;
void setUp() {}
void tearDown() {}
using Memory = fakes::MemoryStore;
class Network : public dialup::Network {
public:
    bool live = false, fail = false;
    unsigned begins = 0, ends = 0;
    std::vector<Bytes> sent, incoming;
    bool begin(const dialup::Options &, uint16_t) override {
        ++begins;
        live = !fail;
        return live;
    }
    void end() override {
        ++ends;
        live = false;
    }
    bool up() const override { return live; }
    bool send(const uint8_t *p, size_t n) override {
        sent.emplace_back(p, p + n);
        return true;
    }
    size_t receive(uint8_t *p, size_t cap) override {
        if (incoming.empty())
            return 0;
        auto bytes = incoming.front();
        incoming.erase(incoming.begin());
        if (bytes.size() > cap)
            return 0;
        std::copy(bytes.begin(), bytes.end(), p);
        return bytes.size();
    }
};
// Test peer wire encoder; always uses full address/control and escapes controls.
Bytes wireBody(Bytes body) {
    uint16_t crc = 0xffff;
    for (auto byte : body) {
        for (unsigned bit = 0; bit < 8; ++bit) {
            bool low = (crc ^ byte) & 1;
            crc >>= 1;
            if (low)
                crc ^= 0x8408;
            byte >>= 1;
        }
    }
    crc ^= 0xffff;
    body.push_back(uint8_t(crc));
    body.push_back(uint8_t(crc >> 8));
    Bytes result{0x7e};
    for (auto byte : body) {
        if (byte < 32 || byte == 0x7e || byte == 0x7d) {
            result.push_back(0x7d);
            result.push_back(byte ^ 0x20);
        } else
            result.push_back(byte);
    }
    result.push_back(0x7e);
    return result;
}
Bytes wire(uint16_t protocol, const Bytes &payload) {
    Bytes body{255, 3, uint8_t(protocol >> 8), uint8_t(protocol)};
    body.insert(body.end(), payload.begin(), payload.end());
    return wireBody(body);
}
std::vector<Bytes> drain(dialup::Engine &engine) {
    uint8_t out[16384];
    const size_t n = engine.read(out, sizeof out);
    std::vector<Bytes> frames;
    Bytes body;
    bool escaped = false;
    for (size_t i = 0; i < n; ++i) {
        uint8_t byte = out[i];
        if (byte == 0x7e) {
            if (!body.empty()) {
                TEST_ASSERT_TRUE(body.size() >= 6);
                TEST_ASSERT_EQUAL_HEX16(0xf0b8, dialup::fcs16(body.data(), body.size()));
                body.resize(body.size() - 2);
                frames.push_back(body);
                body.clear();
            }
        } else if (escaped) {
            body.push_back(byte ^ 0x20);
            escaped = false;
        } else if (byte == 0x7d)
            escaped = true;
        else
            body.push_back(byte);
    }
    return frames;
}
void feed(dialup::Engine &engine, uint16_t protocol, const Bytes &body) {
    auto bytes = wire(protocol, body);
    // Deliberately fragment every frame at every byte boundary.
    for (auto byte : bytes)
        TEST_ASSERT_EQUAL(1, engine.write(&byte, 1));
}
Bytes control(uint8_t code, uint8_t id, Bytes data) {
    Bytes body{code, id, uint8_t((data.size() + 4) >> 8), uint8_t(data.size() + 4)};
    body.insert(body.end(), data.begin(), data.end());
    return body;
}
struct Fixture {
    Network network;
    dialup::Engine engine{network};
    dialup::Options options;
    Fixture() { options.dns = 0x08080808; }
    void ppp(uint32_t now = 0) {
        TEST_ASSERT_TRUE(engine.begin(dialup::Protocol::Ppp, options, now));
    }
    void lcp() {
        auto output = drain(engine);
        TEST_ASSERT_EQUAL(1, output.size());
        auto request = output[0];
        request.erase(request.begin(), request.begin() + 4);
        request[0] = 2;
        feed(engine, 0xc021, request);
        feed(engine, 0xc021, control(1, 44, {}));
    }
    void ready() {
        ppp();
        lcp();
        auto output = drain(engine);
        TEST_ASSERT_EQUAL(2, output.size());
        auto request = output[1];
        request.erase(request.begin(), request.begin() + 4);
        TEST_ASSERT_EQUAL(1, request[0]);
        request[0] = 2;
        feed(engine, 0x8021, request);
        feed(engine, 0x8021, control(1, 45, {3, 6, 10, 64, 64, 2, 129, 6, 8, 8, 8, 8}));
        TEST_ASSERT_TRUE(engine.ready());
        drain(engine);
    }
};
Bytes ipv4() {
    Bytes packet(20 + 256);
    packet[0] = 0x45;
    packet[2] = uint8_t(packet.size() >> 8);
    packet[3] = uint8_t(packet.size());
    packet[8] = 64;
    packet[9] = 17;
    packet[12] = 10;
    packet[13] = 64;
    packet[14] = 64;
    packet[15] = 2;
    packet[16] = 8;
    packet[17] = 8;
    packet[18] = 8;
    packet[19] = 8;
    uint32_t sum = 0;
    for (unsigned i = 0; i < 20; i += 2)
        sum += unsigned(packet[i]) * 256 + packet[i + 1];
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    packet[10] = uint8_t(~sum >> 8);
    packet[11] = uint8_t(~sum);
    for (unsigned i = 0; i < 256; ++i)
        packet[20 + i] = i;
    return packet;
}
void test_settings_crc_defaults_and_rollback() {
    Memory memory;
    dialup::Settings settings(memory);
    TEST_ASSERT_FALSE(settings.begin());
    dialup::Options options{0xc0a87b01, 0xc0a87b02, 0x01010101};
    TEST_ASSERT_TRUE(settings.save(options));
    dialup::Settings restored(memory);
    TEST_ASSERT_TRUE(restored.begin());
    TEST_ASSERT_EQUAL_HEX32(options.peer, restored.get().peer);
    memory.failSave = true;
    options.dns = 0x08080808;
    TEST_ASSERT_FALSE(restored.save(options));
    TEST_ASSERT_EQUAL_HEX32(0x01010101, restored.get().dns);
    memory.bytes[8] ^= 1;
    TEST_ASSERT_FALSE(restored.begin());
    TEST_ASSERT_EQUAL_HEX32(0x0a404001, restored.get().local);
}
void test_settings_reject_bad_pairs_and_overlap() {
    TEST_ASSERT_FALSE(dialup::validOptions({0x0a404000, 0x0a404002, 0}));
    TEST_ASSERT_FALSE(dialup::validOptions({0x0a404001, 0x0a404003, 0}));
    TEST_ASSERT_FALSE(dialup::validOptions({0x0a404001, 0x0a404001, 0}));
    TEST_ASSERT_FALSE(dialup::validOptions({0x08080801, 0x08080802, 0}));
    TEST_ASSERT_FALSE(dialup::validOptions({0x0a404001, 0x0a404002, 0xe0000001}));
    TEST_ASSERT_TRUE(dialup::overlaps({}, 0x0a000001, 0xff000000));
    TEST_ASSERT_FALSE(dialup::overlaps({}, 0xc0a80101, 0xffffff00));
}
void test_rfc_fcs_and_binary_packet() {
    TEST_ASSERT_EQUAL_HEX16(0x6f91,
                            dialup::fcs16(reinterpret_cast<const uint8_t *>("123456789"), 9));
    Fixture f;
    f.ready();
    auto packet = ipv4();
    feed(f.engine, 0x0021, packet);
    TEST_ASSERT_EQUAL(1, f.network.sent.size());
    TEST_ASSERT_EQUAL_MEMORY(packet.data(), f.network.sent[0].data(), packet.size());
    f.network.incoming.push_back(packet);
    f.engine.poll(1);
    auto output = drain(f.engine);
    TEST_ASSERT_EQUAL(1, output.size());
    TEST_ASSERT_EQUAL_MEMORY(packet.data(), output[0].data() + 4, packet.size());
}
void test_ipv4_checks_source_length_checksum_and_rejects_fragments() {
    auto packet = ipv4();
    TEST_ASSERT_TRUE(dialup::validIpv4(packet.data(), packet.size(), 0x0a404002));
    TEST_ASSERT_FALSE(dialup::validIpv4(packet.data(), packet.size(), 0x0a404003));
    TEST_ASSERT_FALSE(dialup::validIpv4(packet.data(), packet.size() - 1, 0x0a404002));
    for (unsigned flags : {0x2000u, 0x0001u, 0x4000u}) {
        packet[6] = uint8_t(flags >> 8);
        packet[7] = uint8_t(flags);
        packet[10] = packet[11] = 0;
        uint32_t sum = 0;
        for (unsigned i = 0; i < 20; i += 2)
            sum += unsigned(packet[i]) * 256 + packet[i + 1];
        while (sum >> 16)
            sum = (sum & 65535) + (sum >> 16);
        packet[10] = uint8_t(~sum >> 8);
        packet[11] = uint8_t(~sum);
        TEST_ASSERT_EQUAL(flags == 0x4000,
                          dialup::validIpv4(packet.data(), packet.size(), 0x0a404002));
    }
    packet[10] ^= 1;
    TEST_ASSERT_FALSE(dialup::validIpv4(packet.data(), packet.size(), 0x0a404002));
}
void test_ppp_nak_reject_and_exact_ack() {
    Fixture f;
    f.ppp();
    TEST_ASSERT_EQUAL(1, drain(f.engine).size());
    feed(f.engine, 0xc021, control(2, 1, {}));  // Wrong ACK cannot open LCP.
    feed(f.engine, 0x8021, control(1, 1, {3, 6, 10, 64, 64, 2}));
    TEST_ASSERT_FALSE(f.engine.ready());
    feed(f.engine, 0xc021, control(1, 2, {99, 2}));
    auto rejected = drain(f.engine);
    TEST_ASSERT_EQUAL(4, rejected[0][4]);
    feed(f.engine, 0xc021, control(1, 2, {1, 4, 0, 10}));
    auto nak = drain(f.engine);
    TEST_ASSERT_EQUAL(3, nak[0][4]);
    TEST_ASSERT_FALSE(f.engine.ready());
}
void test_ipcp_assigns_peer_and_dns_by_nak() {
    Fixture f;
    f.ppp();
    f.lcp();
    drain(f.engine);
    feed(f.engine, 0x8021, control(1, 7, {3, 6, 0, 0, 0, 0, 129, 6, 0, 0, 0, 0}));
    auto output = drain(f.engine);
    TEST_ASSERT_EQUAL(1, output.size());
    TEST_ASSERT_EQUAL(3, output[0][4]);
    const uint8_t expected[] = {3, 6, 10, 64, 64, 2, 129, 6, 8, 8, 8, 8};
    TEST_ASSERT_EQUAL_MEMORY(expected, output[0].data() + 8, sizeof expected);
}
void test_any_pap_credentials_and_malformed_fields() {
    Fixture f;
    f.ready();
    feed(f.engine, 0xc023, control(1, 3, {3, 'a', 'b', 'c', 3, 'x', 'y', 'z'}));
    auto output = drain(f.engine);
    TEST_ASSERT_EQUAL(2, output[0][4]);
    feed(f.engine, 0xc023, control(1, 3, {3, 'a'}));
    TEST_ASSERT_TRUE(drain(f.engine).empty());
    TEST_ASSERT_EQUAL(1, f.engine.dropped());
}
void test_lcp_echo_termination_drain_and_redial() {
    Fixture f;
    f.ready();
    feed(f.engine, 0xc021, control(9, 81, {1, 2, 3, 4}));
    auto echo = drain(f.engine);
    TEST_ASSERT_EQUAL(10, echo[0][4]);
    feed(f.engine, 0xc021, control(5, 82, {}));
    TEST_ASSERT_FALSE(f.engine.ready());
    TEST_ASSERT_TRUE(f.engine.isOpen());
    auto term = drain(f.engine);
    TEST_ASSERT_EQUAL(6, term[0][4]);
    f.engine.poll(1);
    TEST_ASSERT_FALSE(f.engine.isOpen());
    TEST_ASSERT_EQUAL(1, f.network.ends);
    f.ready();
    TEST_ASSERT_EQUAL(2, f.network.begins);
}
void test_bad_crc_oversize_and_timeout_wrap() {
    Fixture f;
    f.ppp(0xfffffff0);
    auto bytes = wire(0xc021, control(1, 1, {}));
    bytes[bytes.size() - 2] ^= 1;
    f.engine.write(bytes.data(), bytes.size());
    Bytes oversized(1700, 0x40);
    oversized.push_back(0x7e);
    f.engine.write(oversized.data(), oversized.size());
    TEST_ASSERT_EQUAL(2, f.engine.dropped());
    f.engine.poll(uint32_t(0xfffffff0u + 30000u));
    TEST_ASSERT_FALSE(f.engine.isOpen());
    TEST_ASSERT_EQUAL(dialup::Reason::Timeout, f.engine.reason());
}
void test_call_start_after_loop_timestamp_does_not_timeout() {
    Fixture f;
    f.ppp(1000);
    f.engine.poll(999);
    TEST_ASSERT_TRUE(f.engine.isOpen());
    TEST_ASSERT_EQUAL(dialup::Reason::None, f.engine.reason());
    f.engine.poll(1001);
    TEST_ASSERT_TRUE(f.engine.isOpen());
    f.engine.poll(31000);
    TEST_ASSERT_FALSE(f.engine.isOpen());
}
void test_slip_binary_and_bad_escape_resynchronization() {
    Fixture f;
    TEST_ASSERT_TRUE(f.engine.begin(dialup::Protocol::Slip, f.options, 0));
    auto packet = ipv4();
    Bytes bytes{0xc0};
    for (auto b : packet) {
        if (b == 0xc0 || b == 0xdb) {
            bytes.push_back(0xdb);
            bytes.push_back(b == 0xc0 ? 0xdc : 0xdd);
        } else
            bytes.push_back(b);
    }
    bytes.push_back(0xc0);
    const uint8_t bad[] = {0xc0, 0xdb, 0x10, 0x45, 0xc0};
    f.engine.write(bad, sizeof bad);
    f.engine.write(bytes.data(), bytes.size());
    TEST_ASSERT_EQUAL(1, f.network.sent.size());
    TEST_ASSERT_EQUAL_MEMORY(packet.data(), f.network.sent[0].data(), packet.size());
    f.network.live = false;
    f.engine.poll(1);
    TEST_ASSERT_FALSE(f.engine.isOpen());
    TEST_ASSERT_EQUAL(dialup::Reason::Network, f.engine.reason());
}
using Sink = fakes::TextSink;
void test_binary_call_suppresses_echo_escape_and_software_flow() {
    Memory memory;
    atcmd::Modem modem(memory, nullptr, 0);
    Sink output;
    hostport::CallSlot slot;
    hostport::Session session(modem, output, 115200, slot);
    modem.begin();
    session.port().execute("F0&I2&H3S12=1");
    TEST_ASSERT_EQUAL(0, modem.active().get(modemcfg::kF));
    Fixture f;
    TEST_ASSERT_TRUE(f.engine.begin(dialup::Protocol::Slip, f.options, 0));
    TEST_ASSERT_TRUE(session.startCall(f.engine));
    const uint8_t bytes[] = {0xc0, 0x11, 0x13, '+', '+', '+', 0xc0};
    TEST_ASSERT_EQUAL(sizeof bytes, session.fromHost(bytes, sizeof bytes, 100));
    session.tick(1000, true);
    TEST_ASSERT_EQUAL(hostport::Mode::kOnline, session.mode());
    uint8_t buf[64];
    TEST_ASSERT_EQUAL(0, session.toHost(buf, sizeof buf));
    TEST_ASSERT_FALSE(session.heldByXoff());
    session.hangUp();
    TEST_ASSERT_FALSE(slot.busy());
}
class Radio : public netcfg::Radio {
public:
    netcfg::LinkInfo info{};
    void connect(const netcfg::Settings &) override {}
    void leave() override {}
    void off() override {}
    netcfg::LinkInfo link() override { return info; }
    bool scanStart() override { return false; }
    int scanResult(netcfg::ScanEntry *, int) override { return -2; }
    void startServices(const netcfg::Settings &) override {}
    int64_t utcNow() override { return 0; }
    int64_t lastSync() override { return 0; }
    void mac(uint8_t out[6]) override { memset(out, 0, 6); }
};
using Sound = fakes::RecordingSound;
class Fallback : public hostport::Session::Dialer {
public:
    unsigned starts = 0;
    bool start(hostport::Session &, const char *, bool) override {
        ++starts;
        return false;
    }
};
uint32_t now() {
    return 100;
}
struct DialFixture {
    Memory memory, savedNet, savedInternet;
    Radio radio;
    netcfg::Net net{savedNet, radio};
    dialup::Settings settings{savedInternet};
    Network network;
    Sound sound;
    Fallback fallback;
    atcmd::Modem modem{memory, nullptr, 0};
    Sink sink;
    hostport::CallSlot slot;
    hostport::Session session{modem, sink, 115200, slot};
    dialup::Dialer dialer{modem, settings, net, slot, network, sound, now, fallback};
    DialFixture() {
        modem.begin();
        session.port().execute("M0&D2&C1");
        session.setDialer(&dialer);
        net.begin(0);
        radio.info.state = netcfg::LinkState::kUp;
        radio.info.ip = 0xc0a80164;
        radio.info.mask = 0xffffff00;
        radio.info.dns = 0x08080808;
        net.tick(0);
    }
};
void test_reserved_dial_aliases_ownership_and_management_hangup() {
    for (const char *number : {"666", "667", "5517546", "5517545"}) {
        DialFixture f;
        TEST_ASSERT_TRUE(f.dialer.start(f.session, number, false));
        TEST_ASSERT_TRUE(f.session.callUp());
        TEST_ASSERT_TRUE(f.slot.busy());
        Sink other;
        hostport::Session second(f.modem, other, 115200, f.slot);
        TEST_ASSERT_TRUE(f.dialer.start(second, "667", false));
        TEST_ASSERT_NOT_EQUAL(std::string::npos, other.text.find("BUSY"));
        TEST_ASSERT_EQUAL(atcmd::Reply::kError, f.dialer.command(second.port(), "=DNS,1.1.1.1"));
        TEST_ASSERT_EQUAL(atcmd::Reply::kOk, f.dialer.command(second.port(), "=HANGUP"));
        f.session.tick(101, true);
        f.dialer.poll();
        TEST_ASSERT_FALSE(f.session.callUp());
        TEST_ASSERT_FALSE(f.slot.busy());
        TEST_ASSERT_FALSE(f.dialer.active());
    }
}
void test_internet_settings_commands_and_network_failure_cleanup() {
    DialFixture f;
    TEST_ASSERT_EQUAL(atcmd::Reply::kError,
                      f.dialer.command(f.session.port(), "=IP,10.64.64.0,10.64.64.2"));
    TEST_ASSERT_EQUAL(atcmd::Reply::kError,
                      f.dialer.command(f.session.port(), "=IP,192.168.1.1,192.168.1.2"));
    TEST_ASSERT_EQUAL(atcmd::Reply::kOk, f.dialer.command(f.session.port(), "=DNS,1.1.1.1"));
    TEST_ASSERT_EQUAL(atcmd::Reply::kError, f.dialer.command(f.session.port(), "=DNS,1.1.1.1,"));
    TEST_ASSERT_EQUAL(atcmd::Reply::kError,
                      f.dialer.command(f.session.port(), "=IP,10.64.64.1,10.64.64.2,"));
    TEST_ASSERT_TRUE(f.dialer.start(f.session, "667", false));
    f.radio.info.ip = 0xc0a80165;
    f.net.tick(2);
    f.dialer.poll();
    f.session.tick(103, true);
    f.dialer.poll();
    TEST_ASSERT_FALSE(f.session.callUp());
    TEST_ASSERT_FALSE(f.slot.busy());
    TEST_ASSERT_FALSE(f.dialer.start(f.session, "123", false));
    TEST_ASSERT_EQUAL(1, f.fallback.starts);
}

void test_ppp_renegotiation_gets_a_fresh_deadline_and_can_reconnect() {
    for (bool lcp : {false, true}) {
        Fixture f;
        f.ready();
        f.engine.poll(60000);
        feed(f.engine, lcp ? 0xc021 : 0x8021, control(lcp ? 1 : 5, 46, {}));
        auto output = drain(f.engine);
        TEST_ASSERT_FALSE(f.engine.ready());
        f.engine.poll(60001);
        TEST_ASSERT_TRUE(f.engine.isOpen());
        auto request = lcp ? output[0] : output[1];
        request.erase(request.begin(), request.begin() + 4);
        request[0] = 2;
        feed(f.engine, lcp ? 0xc021 : 0x8021, request);
        if (lcp) {
            auto ipc = drain(f.engine);
            TEST_ASSERT_EQUAL(1, ipc.size());
            request = ipc[0];
            request.erase(request.begin(), request.begin() + 4);
            request[0] = 2;
            feed(f.engine, 0x8021, request);
        }
        feed(f.engine, 0x8021, control(1, 47, {3, 6, 10, 64, 64, 2}));
        TEST_ASSERT_TRUE(f.engine.ready());
        TEST_ASSERT_EQUAL(2, f.network.begins);
        drain(f.engine);
    }
    Fixture f;
    f.ready();
    f.engine.poll(60000);
    feed(f.engine, 0xc021, control(1, 46, {}));
    drain(f.engine);
    f.engine.poll(89999);
    TEST_ASSERT_TRUE(f.engine.isOpen());
    drain(f.engine);
    f.engine.poll(90000);
    TEST_ASSERT_FALSE(f.engine.isOpen());
}
void test_uart_input_buffer_retains_ppp_tail_until_reply_queue_drains() {
    Fixture f;
    f.ready();
    while (f.engine.space())
        feed(f.engine, 0xc021, control(9, 55, {0, 0, 0, 0}));
    uint8_t byte = 0;
    bool body = false;
    do {
        TEST_ASSERT_EQUAL(1, f.engine.read(&byte, 1));
        body = body || byte != 0x7e;
    } while (!body || byte != 0x7e || !f.engine.space());
    Memory memory;
    atcmd::Modem modem(memory, nullptr, 0);
    modem.begin();
    Sink sink;
    hostport::CallSlot slot;
    hostport::Session session(modem, sink, 115200, slot);
    TEST_ASSERT_TRUE(session.startCall(f.engine));
    Bytes data;
    auto request = wire(0xc021, control(9, 56, {0, 0, 0, 0}));
    for (unsigned i = 0; i < 6; ++i)
        data.insert(data.end(), request.begin(), request.end());
    TEST_ASSERT_LESS_OR_EQUAL(session.hostSpace(), data.size());
    hostport::InputBuffer pending;
    TEST_ASSERT_TRUE(pending.append(data.data(), data.size()));
    pending.poll(session, 100);
    TEST_ASSERT_TRUE(pending.size() > 0);
    TEST_ASSERT_EQUAL(data.size(), pending.size() + session.payloadSent());
    auto frames = drain(f.engine);
    pending.poll(session, 101);
    TEST_ASSERT_EQUAL(0, pending.size());
    auto rest = drain(f.engine);
    frames.insert(frames.end(), rest.begin(), rest.end());
    unsigned replies = 0;
    for (const auto &frame : frames)
        if (frame[5] == 56)
            ++replies;
    TEST_ASSERT_EQUAL(6, replies);
    TEST_ASSERT_EQUAL(data.size(), session.payloadSent());
    uint8_t tooMuch[513] = {};
    TEST_ASSERT_FALSE(pending.append(tooMuch, sizeof tooMuch));
    session.hangUp();
}

// --- TG-03: malformed PPP control packets, table driven ---------------------------------------
// Each case is fed to an engine at one stage of the negotiation. Afterwards the engine must
// still be open, have answered as the table says (and nothing else), and still be able to
// finish negotiating and carry an IP packet.
enum class Stage { kStart, kLcpOpen, kReady };
struct PppCase {
    const char *name;
    Stage stage;
    Bytes frame;          // exactly the bytes put on the wire
    int replyCode;        // -1: no reply; else the code of the one reply expected
    uint16_t replyProto;  // protocol of that reply
    unsigned dropped;     // increase of dropped()
};
// Frame bytes whose FCS is wrong (the last data byte before the FCS is changed).
Bytes badFcs(Bytes frame) {
    frame[frame.size() - 3] ^= 0x01;
    return frame;
}
Bytes rawControl(uint8_t code, uint8_t id, uint16_t length, Bytes data) {
    Bytes body{code, id, uint8_t(length >> 8), uint8_t(length)};
    body.insert(body.end(), data.begin(), data.end());
    return body;
}
// Finds the first frame of `protocol` with `code`; empty if none.
Bytes findFrame(const std::vector<Bytes> &frames, uint16_t protocol, uint8_t code) {
    for (const auto &f : frames)
        if (f.size() >= 5 && f[2] == uint8_t(protocol >> 8) && f[3] == uint8_t(protocol) &&
            f[4] == code)
            return f;
    return {};
}
Bytes ackOf(const Bytes &request) {
    Bytes ack = request;
    ack.erase(ack.begin(), ack.begin() + 4);
    ack[0] = 2;
    return ack;
}
void runPppCase(const PppCase &c) {
    Fixture f;
    f.ppp();
    auto first = drain(f.engine);  // our LCP request
    TEST_ASSERT_EQUAL_MESSAGE(1, first.size(), c.name);
    std::vector<Bytes> opening;  // our IPCP request, once LCP is open
    if (c.stage != Stage::kStart) {
        feed(f.engine, 0xc021, ackOf(first[0]));
        feed(f.engine, 0xc021, control(1, 44, {}));
        opening = drain(f.engine);
        TEST_ASSERT_FALSE_MESSAGE(findFrame(opening, 0x8021, 1).empty(), c.name);
    }
    if (c.stage == Stage::kReady) {
        feed(f.engine, 0x8021, ackOf(findFrame(opening, 0x8021, 1)));
        feed(f.engine, 0x8021, control(1, 45, {3, 6, 10, 64, 64, 2, 129, 6, 8, 8, 8, 8}));
        TEST_ASSERT_TRUE_MESSAGE(f.engine.ready(), c.name);
        drain(f.engine);
    }
    const uint32_t droppedBefore = f.engine.dropped();
    for (auto byte : c.frame)
        TEST_ASSERT_EQUAL_MESSAGE(1, f.engine.write(&byte, 1), c.name);
    auto replies = drain(f.engine);
    TEST_ASSERT_EQUAL_MESSAGE(c.dropped, f.engine.dropped() - droppedBefore, c.name);
    TEST_ASSERT_EQUAL_MESSAGE(c.replyCode < 0 ? 0 : 1, replies.size(), c.name);
    if (c.replyCode >= 0) {
        TEST_ASSERT_EQUAL_MESSAGE(uint8_t(c.replyProto >> 8), replies[0][2], c.name);
        TEST_ASSERT_EQUAL_MESSAGE(uint8_t(c.replyProto), replies[0][3], c.name);
        TEST_ASSERT_EQUAL_MESSAGE(c.replyCode, replies[0][4], c.name);
    }
    TEST_ASSERT_TRUE_MESSAGE(f.engine.isOpen(), c.name);
    TEST_ASSERT_EQUAL_MESSAGE(c.stage == Stage::kReady, f.engine.ready(), c.name);
    // A good exchange still works afterwards.
    if (c.stage == Stage::kStart)
        feed(f.engine, 0xc021, ackOf(first[0]));
    if (c.stage != Stage::kReady) {
        feed(f.engine, 0xc021, control(1, 60, {}));  // a good LCP request from the peer
        for (auto &frame : drain(f.engine))
            opening.push_back(frame);
        auto request = findFrame(opening, 0x8021, 1);
        TEST_ASSERT_FALSE_MESSAGE(request.empty(), c.name);
        feed(f.engine, 0x8021, ackOf(request));
        feed(f.engine, 0x8021, control(1, 46, {3, 6, 10, 64, 64, 2, 129, 6, 8, 8, 8, 8}));
        drain(f.engine);
    }
    TEST_ASSERT_TRUE_MESSAGE(f.engine.ready(), c.name);
    const size_t sent = f.network.sent.size();
    feed(f.engine, 0x0021, ipv4());
    TEST_ASSERT_EQUAL_MESSAGE(sent + 1, f.network.sent.size(), c.name);
}
void test_ppp_malformed_control_packets_are_dropped_rejected_or_ignored() {
    constexpr uint16_t kLcp = 0xc021, kIpcp = 0x8021, kPap = 0xc023;
    const Bytes tooBig(1700, 0x40);
    std::vector<PppCase> cases = {
        // --- before LCP is open ---
        {"LCP length below the 4-byte header", Stage::kStart,
         wire(kLcp, rawControl(1, 7, 3, {})), -1, 0, 1},
        {"LCP length longer than the frame", Stage::kStart, wire(kLcp, rawControl(1, 7, 200, {})),
         -1, 0, 1},
        {"LCP length zero", Stage::kStart, wire(kLcp, rawControl(1, 7, 0, {})), -1, 0, 1},
        {"LCP option length 0", Stage::kStart, wire(kLcp, rawControl(1, 7, 6, {1, 0})), -1, 0, 0},
        {"LCP option length 1", Stage::kStart, wire(kLcp, rawControl(1, 7, 5, {1})), -1, 0, 0},
        {"LCP option runs past the end", Stage::kStart,
         wire(kLcp, rawControl(1, 7, 8, {1, 9, 0, 0})), -1, 0, 0},
        {"LCP unknown code", Stage::kStart, wire(kLcp, rawControl(42, 7, 4, {})), 7, kLcp, 0},
        {"LCP MRU option of the wrong size is rejected", Stage::kStart,
         wire(kLcp, rawControl(1, 7, 7, {1, 3, 5})), 4, kLcp, 0},
        {"LCP MRU below 128 is a Nak", Stage::kStart, wire(kLcp, rawControl(1, 7, 8, {1, 4, 0, 10})),
         3, kLcp, 0},
        {"LCP Nak with option length 0", Stage::kStart, wire(kLcp, rawControl(3, 1, 6, {1, 0})), -1,
         0, 0},
        {"LCP Ack for a request we never made", Stage::kStart,
         wire(kLcp, rawControl(2, 9, 8, {1, 4, 5, 220})), -1, 0, 0},
        {"bad FCS", Stage::kStart, badFcs(wire(kLcp, control(1, 7, {}))), -1, 0, 1},
        {"frame too short for a protocol field", Stage::kStart, wireBody({255, 3}), -1, 0, 0},
        {"frame without address and control, no ACFC agreed", Stage::kStart,
         wireBody({0xc0, 0x21, 1, 7, 0, 4}), -1, 0, 1},
        {"empty frame", Stage::kStart, Bytes{0x7e, 0x7e}, -1, 0, 0},
        {"escape cut off by the frame flag", Stage::kStart, Bytes{0x7d, 0x7e}, -1, 0, 1},
        {"frame longer than the MTU", Stage::kStart, [&] {
             Bytes b = tooBig;
             b.push_back(0x7e);
             return b;
         }(), -1, 0, 1},
        {"unknown protocol while LCP is closed", Stage::kStart, wire(0x1234, control(1, 7, {})), -1,
         0, 0},
        {"PAP request while LCP is closed", Stage::kStart,
         wire(kPap, control(1, 3, {3, 'a', 'b', 'c', 3, 'x', 'y', 'z'})), -1, 0, 0},
        {"IP packet before PPP is up", Stage::kStart, wire(0x0021, ipv4()), -1, 0, 1},
        // --- LCP open, IPCP not yet ---
        {"IPCP length below the header", Stage::kLcpOpen, wire(kIpcp, rawControl(1, 9, 3, {})), -1,
         0, 1},
        {"IPCP option length 0", Stage::kLcpOpen, wire(kIpcp, rawControl(1, 9, 6, {3, 0})), -1, 0,
         0},
        {"IPCP option length 1", Stage::kLcpOpen, wire(kIpcp, rawControl(1, 9, 5, {3})), -1, 0, 0},
        {"IPCP option runs past the end", Stage::kLcpOpen,
         wire(kIpcp, rawControl(1, 9, 8, {3, 9, 0, 0})), -1, 0, 0},
        {"IPCP address option of the wrong size is rejected", Stage::kLcpOpen,
         wire(kIpcp, rawControl(1, 9, 7, {3, 3, 1})), 4, kIpcp, 0},
        {"IPCP unknown option is rejected", Stage::kLcpOpen,
         wire(kIpcp, rawControl(1, 9, 10, {99, 6, 1, 2, 3, 4})), 4, kIpcp, 0},
        {"IPCP unknown code is ignored", Stage::kLcpOpen, wire(kIpcp, rawControl(42, 9, 4, {})),
         -1, 0, 0},
        {"IPCP Ack with the wrong id", Stage::kLcpOpen,
         wire(kIpcp, rawControl(2, 99, 10, {3, 6, 10, 64, 64, 1})), -1, 0, 0},
        {"unknown protocol after LCP is open gets a Protocol-Reject", Stage::kLcpOpen,
         wire(0x1234, control(1, 7, {})), 8, kLcp, 0},
        {"PAP request with a name longer than the packet", Stage::kLcpOpen,
         wire(kPap, rawControl(1, 3, 6, {200, 'a'})), -1, 0, 1},
        {"PAP request too short for two lengths", Stage::kLcpOpen,
         wire(kPap, rawControl(1, 3, 5, {1})), -1, 0, 1},
        {"PAP packet that is not a request is ignored", Stage::kLcpOpen,
         wire(kPap, rawControl(2, 3, 5, {0})), -1, 0, 0},
        {"LCP Echo-Request shorter than its magic number", Stage::kLcpOpen,
         wire(kLcp, rawControl(9, 5, 7, {1, 2, 3})), 7, kLcp, 0},
        {"LCP ACCM option of the wrong size is rejected", Stage::kLcpOpen,
         wire(kLcp, rawControl(1, 8, 7, {2, 3, 0})), 4, kLcp, 0},
        // --- PPP up ---
        {"IP packet shorter than its header", Stage::kReady, wire(0x0021, Bytes(10, 0x45)), -1, 0,
         1},
        {"IP packet from the wrong address", Stage::kReady, [] {
             auto packet = ipv4();
             packet[15] = 3;
             return wire(0x0021, packet);
         }(), -1, 0, 1},
        {"IP packet with the wrong total length", Stage::kReady, [] {
             auto packet = ipv4();
             packet[3] ^= 1;
             return wire(0x0021, packet);
         }(), -1, 0, 1},
        {"IP packet with a bad header checksum", Stage::kReady, [] {
             auto packet = ipv4();
             packet[10] ^= 1;
             return wire(0x0021, packet);
         }(), -1, 0, 1},
        {"IP packet longer than the MTU", Stage::kReady, wire(0x0021, Bytes(1501, 0x45)), -1, 0,
         1},
        {"IPCP length longer than the frame while up", Stage::kReady,
         wire(kIpcp, rawControl(1, 9, 200, {})), -1, 0, 1},
        {"LCP unknown code while up", Stage::kReady, wire(kLcp, rawControl(42, 7, 4, {})), 7, kLcp,
         0},
    };
    for (const auto &c : cases)
        runPppCase(c);
}
// 5.5-09: a peer cannot keep an unfinished negotiation alive with Terminate-Requests.
void test_ipcp_terminate_request_does_not_extend_the_negotiation_deadline() {
    Fixture f;
    f.ppp(0);
    f.lcp();
    drain(f.engine);
    f.engine.poll(29000);
    feed(f.engine, 0x8021, control(5, 50, {}));
    f.engine.poll(30000);
    TEST_ASSERT_FALSE(f.engine.isOpen());
    TEST_ASSERT_EQUAL(dialup::Reason::Timeout, f.engine.reason());
    // Once PPP has been up, a Terminate-Request still starts a fresh 30 s (renegotiation).
    Fixture g;
    g.ready();
    g.engine.poll(60000);
    feed(g.engine, 0x8021, control(5, 51, {}));
    g.engine.poll(89999);
    TEST_ASSERT_TRUE(g.engine.isOpen());
    g.engine.poll(90000);
    TEST_ASSERT_FALSE(g.engine.isOpen());
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_ppp_renegotiation_gets_a_fresh_deadline_and_can_reconnect);
    RUN_TEST(test_uart_input_buffer_retains_ppp_tail_until_reply_queue_drains);

    RUN_TEST(test_settings_crc_defaults_and_rollback);
    RUN_TEST(test_settings_reject_bad_pairs_and_overlap);
    RUN_TEST(test_rfc_fcs_and_binary_packet);
    RUN_TEST(test_ipv4_checks_source_length_checksum_and_rejects_fragments);
    RUN_TEST(test_ppp_nak_reject_and_exact_ack);
    RUN_TEST(test_ipcp_assigns_peer_and_dns_by_nak);
    RUN_TEST(test_any_pap_credentials_and_malformed_fields);
    RUN_TEST(test_lcp_echo_termination_drain_and_redial);
    RUN_TEST(test_bad_crc_oversize_and_timeout_wrap);
    RUN_TEST(test_ppp_malformed_control_packets_are_dropped_rejected_or_ignored);
    RUN_TEST(test_ipcp_terminate_request_does_not_extend_the_negotiation_deadline);
    RUN_TEST(test_call_start_after_loop_timestamp_does_not_timeout);
    RUN_TEST(test_slip_binary_and_bad_escape_resynchronization);
    RUN_TEST(test_binary_call_suppresses_echo_escape_and_software_flow);
    RUN_TEST(test_reserved_dial_aliases_ownership_and_management_hangup);
    RUN_TEST(test_internet_settings_commands_and_network_failure_cleanup);
    return UNITY_END();
}
