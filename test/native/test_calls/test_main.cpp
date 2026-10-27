#include <unity.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <string>
#include <vector>
#include "outgoing.h"
#include "wav.h"

using namespace calls;
uint32_t clockMs;
bool wifi;
uint32_t clockNow() {
    return clockMs;
}
bool networkUp() {
    return wifi;
}
void setUp() {
    clockMs = 0;
    wifi = true;
}
void tearDown() {}

class Memory : public modemcfg::Store {
public:
    std::vector<uint8_t> bytes;
    bool fail = false;
    size_t load(uint8_t *out, size_t cap) override {
        size_t n = std::min(bytes.size(), cap);
        if (n) {
            memcpy(out, bytes.data(), n);
        }
        return n;
    }
    bool save(const uint8_t *in, size_t n) override {
        if (fail) {
            return false;
        }
        bytes.assign(in, in + n);
        return true;
    }
    bool erase() override {
        bytes.clear();
        return true;
    }
};
class Output : public atcmd::Sink {
public:
    std::string text;
    void write(const char *s, size_t n) override { text.append(s, n); }
    std::string take() {
        std::string result = text;
        text.clear();
        return result;
    }
};
class FakeSocket : public Socket {
public:
    SocketState status = SocketState::kClosed, initial = SocketState::kPending;
    std::string host;
    uint16_t port = 0;
    unsigned begins = 0;
    std::vector<uint8_t> input, output;
    size_t chunk = 256;
    void begin(const char *h, uint16_t p) override {
        host = h;
        port = p;
        ++begins;
        status = initial;
    }
    void poll() override {}
    SocketState state() const override { return status; }
    size_t write(const uint8_t *data, size_t n) override {
        n = std::min(n, chunk);
        output.insert(output.end(), data, data + n);
        return n;
    }
    size_t read(uint8_t *data, size_t cap) override {
        size_t n = std::min(std::min(cap, chunk), input.size());
        if (n) {
            memcpy(data, input.data(), n);
            input.erase(input.begin(), input.begin() + n);
        }
        return n;
    }
    void close() override { status = SocketState::kClosed; }
};
class FakeSound : public CallSound {
public:
    std::vector<SoundStage> stages;
    std::string digits;
    bool active = false, fail = false, pulse = false;
    bool start(SoundStage stage, const char *d, bool p, uint32_t,
               const modemcfg::Profile &) override {
        stages.push_back(stage);
        digits = d;
        pulse = p;
        active = !fail;
        return !fail;
    }
    bool running() const override { return active; }
    void stop() override { active = false; }
    void poll(uint32_t, const modemcfg::Profile &) override {}
};
const atcmd::StarCommand commands[] = {
    {"MODEL", "AT*MODEL", "model", atcmd::cmdModel, false},
    {"THROTTLE", "AT*THROTTLE", "throttle", atcmd::cmdThrottle, false}};
struct Rig {
    Memory store, bookStore;
    atcmd::Modem modem{store, commands, 2};
    Phonebook book{bookStore};
    hostport::CallSlot slot;
    Output output, otherOutput;
    hostport::Session session{modem, output, 115200, slot}, other{modem, otherOutput, 38400, slot};
    FakeSocket socket;
    FakeSound sound;
    Outgoing call{modem, book, slot, socket, sound, clockNow, networkUp};
    Rig() {
        modem.begin();
        book.begin();
        modem.active().v[modemcfg::kM] = 0;
        session.setDialer(&call);
        other.setDialer(&call);
        session.tick(0, true);
        other.tick(0, true);
    }
    void send(const char *text) {
        session.fromHost(reinterpret_cast<const uint8_t *>(text), strlen(text), clockMs);
    }
    void poll(uint32_t elapsed = 1, bool dtr = true) {
        clockMs += elapsed;
        session.tick(clockMs, dtr);
        call.poll();
    }
    void online() {
        send("ATDT1\r");
        socket.status = SocketState::kOpen;
        poll();
        TEST_ASSERT_TRUE(session.callUp());
        output.take();
    }
};

void test_phonebook_defaults_exact_keys_validation_and_atomic_save() {
    Memory store;
    Phonebook book(store);
    TEST_ASSERT_TRUE(book.begin());
    TEST_ASSERT_EQUAL_STRING("www.amigaretro.com", book.find("1")->host);
    TEST_ASSERT_EQUAL(9 + Phonebook::kRecordSize, store.bytes.size());
    Entry e;
    strcpy(e.number, "01");
    strcpy(e.host, "127.0.0.1");
    e.port = 2323;
    e.raw = true;
    e.speed = 56000;
    TEST_ASSERT_TRUE(book.put(e));
    TEST_ASSERT_TRUE(book.find("1") != book.find("01"));
    Phonebook again(store);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_EQUAL(2323, again.find("01")->port);
    TEST_ASSERT_TRUE(again.find("01")->raw);
    for (const char *invalid :
         {"123", "666", "667", "5517546", "5517545", "123456789012", "1-2", "a"}) {
        memset(e.number, 0, sizeof e.number);
        memcpy(e.number, invalid, std::min(strlen(invalid), sizeof e.number));
        TEST_ASSERT_FALSE(book.put(e));
    }
    strcpy(e.number, "01");
    e.speed = 12345;
    TEST_ASSERT_FALSE(book.put(e));
    e.speed = 2400;
    store.fail = true;
    TEST_ASSERT_FALSE(book.put(e));
    TEST_ASSERT_EQUAL_UINT32(56000, book.find("01")->speed);
    TEST_ASSERT_FALSE(book.remove("01"));
    TEST_ASSERT_NOT_NULL(book.find("01"));
    store.fail = false;
    TEST_ASSERT_TRUE(book.remove("01"));
    Phonebook removed(store);
    TEST_ASSERT_TRUE(removed.begin());
    TEST_ASSERT_NULL(removed.find("01"));
}
void test_phonebook_capacity_and_corruption() {
    Memory store;
    Phonebook book(store);
    book.begin();
    for (unsigned i = 2; i <= 64; ++i) {
        Entry e;
        snprintf(e.number, sizeof e.number, "%u", i);
        strcpy(e.host, "bbs.example.com");
        TEST_ASSERT_TRUE(book.put(e));
    }
    Entry e;
    strcpy(e.number, "65");
    strcpy(e.host, "bbs.example.com");
    TEST_ASSERT_FALSE(book.put(e));
    Phonebook full(store);
    TEST_ASSERT_TRUE(full.begin());
    TEST_ASSERT_NOT_NULL(full.find("64"));
    TEST_ASSERT_TRUE(full.remove("64"));
    TEST_ASSERT_TRUE(full.remove("1"));
    Phonebook compact(store);
    TEST_ASSERT_TRUE(compact.begin());
    TEST_ASSERT_NULL(compact.find("1"));
    TEST_ASSERT_NOT_NULL(compact.find("63"));
    TEST_ASSERT_EQUAL(9 + 62 * Phonebook::kRecordSize, store.bytes.size());
    store.bytes[50] ^= 1;
    auto before = store.bytes;
    Phonebook damaged(store);
    TEST_ASSERT_FALSE(damaged.begin());
    TEST_ASSERT_EQUAL_STRING("www.amigaretro.com", damaged.find("1")->host);
    TEST_ASSERT_TRUE(before == store.bytes);
}
void test_phonebook_command_roundtrip_and_strict_arguments() {
    Rig r;
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kOk),
                      int(r.book.command(r.session.port(), "=02,localhost,2323,RAW,33600", false)));
    TEST_ASSERT_EQUAL_UINT32(33600, r.book.find("02")->speed);
    r.book.command(r.session.port(), "?", false);
    TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "02,localhost,2323,RAW,33600"));
    for (const char *bad : {"=123,host,23,RAW", "=2,host,0,RAW", "=2,host,23,OTHER",
                            "=2,host,23,RAW,1", "=2,host,23,RAW,0,extra"}) {
        TEST_ASSERT_EQUAL(int(atcmd::Reply::kError),
                          int(r.book.command(r.session.port(), bad, false)));
    }
}
void test_telnet_fragmented_negotiation_ansi_naws_and_escaped_ff() {
    Telnet t;
    t.begin();
    t.tx.clear();
    const uint8_t bytes[] = {255, 253, 24,  255, 250, 24,  1, 255, 240, 255, 253,
                             31,  255, 251, 0,   255, 253, 0, 65,  255, 255, 66};
    for (uint8_t b : bytes) {
        t.feed(b);
    }
    uint8_t data[128];
    size_t n = t.rx.read(data, sizeof data);
    const uint8_t expected[] = {65, 255, 66};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, 3);
    TEST_ASSERT_EQUAL(3, n);
    n = t.tx.read(data, sizeof data);
    const uint8_t replies[] = {255, 250, 24, 0, 'A', 'N', 'S', 'I', 255, 240,
                               255, 250, 31, 0, 80,  0,   24,  255, 240};
    TEST_ASSERT_EQUAL(sizeof replies, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(replies, data, n);
    const uint8_t outgoing[] = {0, 255, 13};
    t.write(outgoing, sizeof outgoing);
    n = t.tx.read(data, sizeof data);
    const uint8_t escaped[] = {0, 255, 255, 13};
    TEST_ASSERT_EQUAL(sizeof escaped, n);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(escaped, data, n);
}
void test_telnet_refusals_no_loops_and_oversized_subnegotiation() {
    Telnet t;
    t.begin();
    t.tx.clear();
    for (unsigned i = 0; i < 100; ++i) {
        t.feed(255);
        t.feed(251);
        t.feed(99);
    }
    TEST_ASSERT_EQUAL(3, t.tx.size());
    t.feed(255);
    t.feed(250);
    for (unsigned i = 0; i < 10000; ++i) {
        t.feed(42);
    }
    t.feed(255);
    t.feed(240);
    t.feed('x');
    uint8_t byte;
    TEST_ASSERT_EQUAL(1, t.rx.read(&byte, 1));
    TEST_ASSERT_EQUAL('x', byte);
}
void test_tcp_raw_all_bytes_and_partial_socket_writes() {
    FakeSocket socket;
    socket.status = SocketState::kOpen;
    socket.chunk = 1;
    TcpConnection conn(socket);
    conn.begin(true);
    std::vector<uint8_t> bytes;
    for (unsigned i = 0; i < 4096; ++i) {
        bytes.push_back(i & 255);
    }
    TEST_ASSERT_EQUAL(bytes.size(), conn.write(bytes.data(), bytes.size()));
    socket.input = bytes;
    for (unsigned i = 0; i < 5000; ++i) {
        conn.poll(i);
    }
    TEST_ASSERT_TRUE(bytes == socket.output);
    std::vector<uint8_t> received(4096);
    TEST_ASSERT_EQUAL(4096, conn.read(received.data(), received.size()));
    TEST_ASSERT_TRUE(bytes == received);
    socket.status = SocketState::kClosed;
    TEST_ASSERT_FALSE(conn.isOpen());
}
void test_pending_connect_reset_and_refusal_are_busy_but_other_errors_are_not() {
    for (int error : {ECONNREFUSED, ECONNRESET}) {
        TEST_ASSERT_EQUAL(int(SocketState::kRefused), int(connectErrorState(error)));
    }
    for (int error : {ETIMEDOUT, ENETUNREACH, ECONNABORTED, EBADF}) {
        TEST_ASSERT_EQUAL(int(SocketState::kFailed), int(connectErrorState(error)));
    }
}
void test_invalid_reserved_and_missing_entries_never_start_socket_or_sound() {
    Rig r;
    for (const char *cmd :
         {"ATDT\r", "ATDT999\r", "ATDT666\r", "ATDT123\r", "ATDT999999999999\r"}) {
        r.send(cmd);
        TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "ERROR"));
    }
    TEST_ASSERT_EQUAL(0, r.socket.begins);
    TEST_ASSERT_TRUE(r.sound.stages.empty());
    TEST_ASSERT_FALSE(r.slot.busy());
}
void test_call_resolves_stored_numbers_and_preserves_actual_dialled_digits() {
    Rig r;
    r.send("AT&Z0=1\r");
    r.output.take();
    r.send("ATDS0\r");
    TEST_ASSERT_EQUAL_STRING("www.amigaretro.com", r.socket.host.c_str());
    TEST_ASSERT_EQUAL_STRING("1", r.modem.lastDialed());
    TEST_ASSERT_EQUAL(23, r.socket.port);
}
void test_call_sequence_pulse_and_buffered_greeting() {
    Rig r;
    r.modem.active().v[modemcfg::kM] = 1;
    r.send("ATDP1\r");
    TEST_ASSERT_EQUAL(int(CallStage::kDial), int(r.call.stage()));
    r.sound.active = false;
    r.poll();
    TEST_ASSERT_EQUAL(int(SoundStage::kDigits), int(r.sound.stages.back()));
    TEST_ASSERT_TRUE(r.sound.pulse);
    TEST_ASSERT_EQUAL_STRING("1", r.sound.digits.c_str());
    r.sound.active = false;
    r.poll();
    TEST_ASSERT_EQUAL(int(CallStage::kConnecting), int(r.call.stage()));
    r.socket.status = SocketState::kOpen;
    r.socket.input = {'h', 'i'};
    r.poll();
    TEST_ASSERT_EQUAL(int(CallStage::kHandshake), int(r.call.stage()));
    TEST_ASSERT_FALSE(r.session.callUp());
    r.sound.active = false;
    r.poll();
    TEST_ASSERT_TRUE(r.session.callUp());
    uint8_t greeting[8];
    TEST_ASSERT_EQUAL(2, r.session.toHost(greeting, sizeof greeting));
    TEST_ASSERT_EQUAL_UINT8_ARRAY("hi", greeting, 2);
}
void test_redial_uses_the_complete_last_destination_and_rejects_empty() {
    Rig r;
    r.send("ATDL\r");
    TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "ERROR"));
    const char *host = "this-host-name-is-deliberately-longer-than-forty.example";
    std::string command = std::string("ATDT") + host + ":2323\r";
    r.send(command.c_str());
    r.send("x");
    r.output.take();
    r.send("ATDL\r");
    TEST_ASSERT_EQUAL_STRING(host, r.socket.host.c_str());
    TEST_ASSERT_EQUAL(2323, r.socket.port);
    TEST_ASSERT_EQUAL(2, r.socket.begins);
}
void test_speed_floor_rejects_entry_before_sound_and_dns_deadline_reports_no_tone() {
    Rig r;
    Entry entry = *r.book.find("1");
    entry.speed = 2400;
    TEST_ASSERT_TRUE(r.book.put(entry));
    r.send("AT&U8\r");
    r.output.take();
    r.send("ATDT1\r");
    TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "NO CARRIER"));
    TEST_ASSERT_TRUE(r.sound.stages.empty());
    TEST_ASSERT_EQUAL(0, r.socket.begins);
    TEST_ASSERT_FALSE(r.slot.busy());
    r.send("AT&U0S7=1\r");
    r.socket.initial = SocketState::kResolving;
    r.send("ATDT1\r");
    r.output.take();
    r.poll(1000);
    TEST_ASSERT_EQUAL_STRING("\r\nNO DIAL TONE\r\n", r.output.take().c_str());
    TEST_ASSERT_FALSE(r.slot.busy());
}
void test_call_refusal_timeout_dns_offline_and_cancellation_results() {
    for (SocketState error :
         {SocketState::kRefused, SocketState::kDnsError, SocketState::kFailed}) {
        Rig r;
        r.send("ATDT1\r");
        r.output.take();
        r.socket.status = error;
        r.poll();
        const char *expected = error == SocketState::kRefused    ? "BUSY"
                               : error == SocketState::kDnsError ? "NO DIAL TONE"
                                                                 : "NO ANSWER";
        TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), expected));
        TEST_ASSERT_FALSE(r.slot.busy());
    }
    {
        Rig r;
        r.modem.active().s[7] = 1;
        r.send("ATDT1\r");
        r.output.take();
        r.poll(1000);
        TEST_ASSERT_EQUAL_STRING("\r\nNO ANSWER\r\n", r.output.take().c_str());
        TEST_ASSERT_FALSE(r.slot.busy());
    }
    {
        Rig r;
        wifi = false;
        r.send("ATDT1\r");
        TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "NO DIAL TONE"));
        TEST_ASSERT_EQUAL(0, r.socket.begins);
    }
    {
        wifi = true;
        Rig r;
        r.send("ATDT1\r");
        r.output.take();
        r.send("x");
        TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n", r.output.take().c_str());
        TEST_ASSERT_FALSE(r.slot.busy());
    }
}
void test_other_port_cannot_replace_dial_target() {
    Rig r;
    r.modem.active().v[modemcfg::kM] = 1;
    r.send("ATDT1\r");
    const char *command = "ATDTother.example:2323\r";
    r.other.fromHost(reinterpret_cast<const uint8_t *>(command), strlen(command), clockMs);
    TEST_ASSERT_NOT_NULL(strstr(r.otherOutput.text.c_str(), "BUSY"));
    r.sound.active = false;
    r.poll();
    r.sound.active = false;
    r.poll();
    TEST_ASSERT_EQUAL_STRING("www.amigaretro.com", r.socket.host.c_str());
}
void test_model_default_explicit_override_and_current_call_snapshot() {
    for (uint32_t model : {14400u, 28800u, 33600u, 56000u}) {
        Rig r;
        TEST_ASSERT_TRUE(r.modem.setModel(model));
        r.online();
        TEST_ASSERT_EQUAL_UINT32(model, r.session.reportedSpeed());
        TEST_ASSERT_EQUAL_UINT32(14400, r.session.lineSpeed());
        r.modem.setModel(model == 14400 ? 56000 : 14400);
        r.poll(1100);
        r.send("+++");
        r.poll(1100);
        r.output.take();
        r.send("ATO\r");
        char expected[40];
        snprintf(expected, sizeof expected, "CONNECT %u", unsigned(model));
        TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), expected));
    }
    {
        Rig r;
        r.modem.setModel(56000);
        r.send("AT&N8\r");
        r.online();
        TEST_ASSERT_EQUAL_UINT32(14400, r.session.reportedSpeed());
    }
    {
        Rig r;
        Entry e = *r.book.find("1");
        e.speed = 2400;
        r.book.put(e);
        r.send("AT&N38\r");
        r.online();
        TEST_ASSERT_EQUAL_UINT32(2400, r.session.reportedSpeed());
        TEST_ASSERT_EQUAL_UINT32(2400, r.session.lineSpeed());
    }
}

void test_model_and_throttle_failed_save_roll_back() {
    Rig r;
    r.store.fail = true;
    TEST_ASSERT_FALSE(r.modem.setModel(56000));
    TEST_ASSERT_EQUAL_UINT32(14400, r.modem.model());
    TEST_ASSERT_FALSE(r.modem.setThrottle(true));
    TEST_ASSERT_FALSE(r.modem.throttle());
}
void test_baud_control_uses_operational_rate_and_failed_switch_closes_call() {
    struct Baud : hostport::Session::BaudControl {
        uint32_t rate = 0;
        unsigned returns = 0;
        bool ready = true;
        bool online(uint32_t speed) override {
            rate = speed;
            return ready;
        }
        void command() override { ++returns; }
    } baud;
    Rig r;
    r.session.setBaudControl(&baud);
    r.modem.setModel(56000);
    r.online();
    TEST_ASSERT_EQUAL_UINT32(14400, baud.rate);
    r.poll(1100);
    r.send("+++");
    r.poll(1100);
    TEST_ASSERT_EQUAL(1, baud.returns);
    baud.ready = false;
    r.output.take();
    r.send("ATO\r");
    TEST_ASSERT_FALSE(r.session.callUp());
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "NO CARRIER"));
    r.send("ATDT1\r");
    r.output.take();
    r.socket.status = SocketState::kOpen;
    r.poll();
    TEST_ASSERT_EQUAL_STRING("\r\nCONNECT 56000\r\n\r\nNO CARRIER\r\n", r.output.take().c_str());
    TEST_ASSERT_FALSE(r.slot.busy());
}
void test_network_bytes_never_execute_commands_remote_drop_and_wifi_loss() {
    Rig r;
    r.online();
    r.socket.input = {'A', 'T', '*', 'P', 'A', 'S', 'S', '=', 'x', '\r'};
    r.poll();
    uint8_t data[64];
    size_t n = r.session.toHost(data, sizeof data);
    TEST_ASSERT_EQUAL(10, n);
    TEST_ASSERT_EQUAL_UINT32(14400, r.modem.model());
    TEST_ASSERT_TRUE(r.output.text.empty());
    wifi = false;
    r.poll();
    r.poll();
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n", r.output.take().c_str());
    TEST_ASSERT_FALSE(r.slot.busy());
    wifi = true;
    r.online();
    r.socket.status = SocketState::kClosed;
    r.poll();
    r.poll();
    TEST_ASSERT_EQUAL_STRING("\r\nNO CARRIER\r\n", r.output.take().c_str());
}
void test_dtr_cancels_pending_call_and_timer_rollover() {
    Rig r;
    r.send("ATDT1\r");
    r.output.take();
    r.poll(1, false);
    r.poll(60, false);
    TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "NO CARRIER"));
    TEST_ASSERT_FALSE(r.slot.busy());
    r.modem.active().s[7] = 1;
    clockMs = UINT32_MAX - 500;
    r.send("ATDT1\r");
    r.output.take();
    r.poll(1100);
    TEST_ASSERT_NOT_NULL(strstr(r.output.take().c_str(), "NO ANSWER"));
}

void test_throttle_limits_both_directions_and_keeps_model_cosmetic() {
    Rig r;
    TEST_ASSERT_TRUE(r.modem.setThrottle(true));
    r.modem.setModel(56000);
    r.send("AT&N1\r");
    r.online();
    TEST_ASSERT_EQUAL_UINT32(300, r.session.lineSpeed());
    const uint8_t bytes[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    TEST_ASSERT_EQUAL(0, r.session.fromHost(bytes, sizeof bytes, clockMs));
    r.socket.input.assign(bytes, bytes + sizeof bytes);
    r.poll(100);
    TEST_ASSERT_EQUAL(3, r.session.fromHost(bytes, sizeof bytes, clockMs));
    uint8_t out[32];
    TEST_ASSERT_EQUAL(3, r.session.toHost(out, sizeof out));
    TEST_ASSERT_EQUAL(0, r.session.toHost(out, sizeof out));
    r.poll(100);
    TEST_ASSERT_EQUAL(3, r.session.toHost(out, sizeof out));
    atcmd::Modem again(r.store, nullptr, 0);
    again.begin();
    TEST_ASSERT_TRUE(again.throttle());
}

class BytesReader : public sounds::Reader {
public:
    std::vector<uint8_t> bytes;
    size_t at = 0;
    size_t read(uint8_t *out, size_t n) override {
        n = std::min(n, bytes.size() - at);
        memcpy(out, bytes.data() + at, n);
        at += n;
        return n;
    }
    bool seek(uint32_t n) override {
        if (n > bytes.size()) {
            return false;
        }
        at = n;
        return true;
    }
    uint32_t size() const override { return bytes.size(); }
};
BytesReader wav() {
    BytesReader r;
    const uint8_t header[] = {'R',  'I',  'F', 'F', 40,   0,    0, 0, 'W', 'A', 'V',  'E',
                              'f',  'm',  't', ' ', 16,   0,    0, 0, 1,   0,   1,    0,
                              0x40, 0x1f, 0,   0,   0x80, 0x3e, 0, 0, 2,   0,   16,   0,
                              'd',  'a',  't', 'a', 4,    0,    0, 0, 1,   0,   0xff, 0x7f};
    r.bytes.assign(header, header + sizeof header);
    return r;
}
void test_wav_validation_upsampling_and_handshake_mapping() {
    BytesReader r = wav();
    sounds::WavSource source(r);
    TEST_ASSERT_TRUE(source.begin());
    int16_t out[8];
    TEST_ASSERT_EQUAL(3, source.fill(out, 3));
    TEST_ASSERT_EQUAL(1, out[0]);
    TEST_ASSERT_EQUAL(1, out[1]);
    TEST_ASSERT_EQUAL(32767, out[2]);
    TEST_ASSERT_EQUAL(1, source.fill(out, 8));
    TEST_ASSERT_EQUAL(32767, out[0]);
    TEST_ASSERT_FALSE(source.failed());
    sounds::WavInfo info;
    for (size_t index : {size_t(0), size_t(20), size_t(22), size_t(24), size_t(28), size_t(32),
                         size_t(34), size_t(40)}) {
        BytesReader bad = wav();
        bad.bytes[index] ^= 1;
        TEST_ASSERT_FALSE(sounds::inspectWav(bad, info));
    }
    r = wav();
    r.bytes.pop_back();
    TEST_ASSERT_FALSE(sounds::inspectWav(r, info));
    TEST_ASSERT_EQUAL_UINT32(1200, sounds::handshakeSpeed(300));
    TEST_ASSERT_EQUAL_UINT32(2400, sounds::handshakeSpeed(4800));
    TEST_ASSERT_EQUAL_UINT32(9600, sounds::handshakeSpeed(9600));
    TEST_ASSERT_EQUAL_UINT32(9600, sounds::handshakeSpeed(12000));
    TEST_ASSERT_EQUAL_UINT32(14400, sounds::handshakeSpeed(19200));
    TEST_ASSERT_EQUAL_UINT32(33600, sounds::handshakeSpeed(54666));
    TEST_ASSERT_EQUAL_UINT32(56000, sounds::handshakeSpeed(56000));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_phonebook_defaults_exact_keys_validation_and_atomic_save);
    RUN_TEST(test_phonebook_capacity_and_corruption);
    RUN_TEST(test_phonebook_command_roundtrip_and_strict_arguments);
    RUN_TEST(test_telnet_fragmented_negotiation_ansi_naws_and_escaped_ff);
    RUN_TEST(test_telnet_refusals_no_loops_and_oversized_subnegotiation);
    RUN_TEST(test_tcp_raw_all_bytes_and_partial_socket_writes);
    RUN_TEST(test_pending_connect_reset_and_refusal_are_busy_but_other_errors_are_not);
    RUN_TEST(test_invalid_reserved_and_missing_entries_never_start_socket_or_sound);
    RUN_TEST(test_call_resolves_stored_numbers_and_preserves_actual_dialled_digits);
    RUN_TEST(test_call_sequence_pulse_and_buffered_greeting);
    RUN_TEST(test_redial_uses_the_complete_last_destination_and_rejects_empty);
    RUN_TEST(test_speed_floor_rejects_entry_before_sound_and_dns_deadline_reports_no_tone);
    RUN_TEST(test_call_refusal_timeout_dns_offline_and_cancellation_results);
    RUN_TEST(test_other_port_cannot_replace_dial_target);
    RUN_TEST(test_model_default_explicit_override_and_current_call_snapshot);
    RUN_TEST(test_model_and_throttle_failed_save_roll_back);
    RUN_TEST(test_baud_control_uses_operational_rate_and_failed_switch_closes_call);
    RUN_TEST(test_network_bytes_never_execute_commands_remote_drop_and_wifi_loss);
    RUN_TEST(test_dtr_cancels_pending_call_and_timer_rollover);
    RUN_TEST(test_throttle_limits_both_directions_and_keeps_model_cosmetic);
    RUN_TEST(test_wav_validation_upsampling_and_handshake_mapping);
    return UNITY_END();
}
