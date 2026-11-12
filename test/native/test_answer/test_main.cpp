#include <unity.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include "incoming.h"

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
        if (n)
            memcpy(out, bytes.data(), n);
        return n;
    }
    bool save(const uint8_t *in, size_t n) override {
        if (fail)
            return false;
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
    std::string unsolicited;  // what came through writeUnsolicited()
    bool full = false;        // a port nobody reads: unsolicited output is dropped
    void write(const char *s, size_t n) override { text.append(s, n); }
    void writeUnsolicited(const char *s, size_t n) override {
        if (!full) {
            unsolicited.append(s, n);
            text.append(s, n);
        }
    }
    std::string take() {
        std::string result = text;
        text.clear();
        return result;
    }
};
class FakeSocket : public Socket {
public:
    SocketState status = SocketState::kClosed;
    std::string input, output;
    size_t chunk = 256;
    void begin(const char *, uint16_t) override {}
    void resolve(const char *) override {}
    const char *address() const override { return ""; }
    void poll() override {}
    SocketState state() const override { return status; }
    size_t write(const uint8_t *data, size_t n) override {
        n = std::min(n, chunk);
        output.append(reinterpret_cast<const char *>(data), n);
        return n;
    }
    size_t read(uint8_t *data, size_t cap) override {
        size_t n = std::min(std::min(cap, chunk), input.size());
        memcpy(data, input.data(), n);
        input.erase(0, n);
        return n;
    }
    void close() override {
        status = SocketState::kClosed;
        input.clear();
    }
};
class FakeListener : public Listener {
public:
    FakeSocket &socket;
    unsigned pending = 0, rejected = 0, starts = 0;
    bool fail = false;
    explicit FakeListener(FakeSocket &s) : socket(s) {}
    bool begin(uint16_t port) override {
        TEST_ASSERT_EQUAL(23, port);
        ++starts;
        return !fail;
    }
    bool accept() override {
        if (!pending)
            return false;
        --pending;
        socket.status = SocketState::kOpen;
        return true;
    }
    void reject() override {
        if (pending) {
            --pending;
            ++rejected;
        }
    }
    void stop() override { pending = 0; }
};
class FakeSound : public CallSound {
public:
    std::vector<SoundStage> stages;
    bool active = false, fail = false, broken = false;
    bool start(SoundStage stage, const char *, bool, uint32_t, const modemcfg::Profile &) override {
        stages.push_back(stage);
        active = !fail;
        return !fail;
    }
    bool running() const override { return active; }
    bool failed() const override { return broken; }
    void stop() override { active = false; }
    void poll(uint32_t, const modemcfg::Profile &) override {}
};
unsigned dispatched;
atcmd::Reply spy(atcmd::Port &, const char *) {
    ++dispatched;
    return atcmd::Reply::kOk;
}
const atcmd::StarCommand commands[] = {{"PASS", "AT*PASS", "spy", spy, false},
                                       {"FACTORY", "AT*FACTORY", "spy", spy, false},
                                       {"CONFIG", "AT*CONFIG", "spy", spy, false},
                                       {"HWTEST", "AT*HWTEST", "spy", spy, false},
                                       {"HELP", "AT*HELP", "spy", spy, false}};
struct Rig {
    Memory store;
    atcmd::Modem modem{store, commands, 5};
    hostport::CallSlot slot;
    Output output, hostOutput, usbOutput;
    hostport::Session console{modem, output, 115200, slot}, host{modem, hostOutput, 38400, slot},
        usb{modem, usbOutput, 115200, slot};
    bool usbDtr = false;
    FakeSocket socket;
    FakeListener listener{socket};
    FakeSound sound;
    Incoming call{modem, slot, listener, socket, sound, console, host, clockNow, networkUp};
    Rig() {
        dispatched = 0;
        modem.begin();
        modem.active().v[modemcfg::kM] = 0;
        console.setAnswerer(&call);
        host.setAnswerer(&call);
        usb.setAnswerer(&call);
        call.setUsb(&usb);
        console.tick(clockMs, true);
        host.tick(clockMs, true);
        usb.tick(clockMs, usbDtr);
    }
    void send(const char *text, bool onHost = false) {
        (onHost ? host : console)
            .fromHost(reinterpret_cast<const uint8_t *>(text), strlen(text), clockMs);
    }
    void poll(uint32_t elapsed = 1, bool dtr = true) {
        clockMs += elapsed;
        console.tick(clockMs, true);
        host.tick(clockMs, dtr);
        usb.tick(clockMs, usbDtr);
        call.poll();
    }
    void ring() {
        ++listener.pending;
        call.poll();
    }
    void online() {
        ring();
        send("ATA\r");
        TEST_ASSERT_TRUE(console.callUp());
        output.take();
    }
    std::string receive() {
        uint8_t bytes[16384];
        size_t n = console.toHost(bytes, sizeof bytes);
        return std::string(reinterpret_cast<char *>(bytes), n);
    }
};
void test_manual_answer_reserves_transfers_and_releases_slot() {
    Rig r;
    r.ring();
    TEST_ASSERT_TRUE(r.call.ringing());
    TEST_ASSERT_TRUE(r.slot.busy());
    TEST_ASSERT_FALSE(r.console.dcd());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("RING"));
    r.send("ATA\r");
    TEST_ASSERT_TRUE(r.console.callUp());
    TEST_ASSERT_TRUE(r.console.dcd());
    TEST_ASSERT_FALSE(r.call.ri());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("CONNECT 14400"));
    r.send("+++");
    r.console.hangUp();
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_FALSE(r.sound.active);
    r.ring();
    TEST_ASSERT_TRUE(r.call.ringing());
}
void test_s0_zero_never_auto_answers_and_s1_stays_zero() {
    Rig r;
    r.ring();
    r.poll(3000);
    TEST_ASSERT_TRUE(r.call.ringing());
    TEST_ASSERT_EQUAL(0, r.modem.active().s[1]);
    TEST_ASSERT_FALSE(r.console.callUp() || r.host.callUp());
}
void test_auto_answer_uses_selected_port_and_exact_ring_threshold() {
    for (bool console : {false, true}) {
        Rig r;
        r.modem.setAnswerPort(console ? modemcfg::AnswerPort::kConsole
                                      : modemcfg::AnswerPort::kRs232);
        r.modem.active().s[0] = 3;
        r.ring();
        TEST_ASSERT_EQUAL(1, r.modem.active().s[1]);
        r.poll(2999);
        TEST_ASSERT_FALSE(r.console.callUp() || r.host.callUp());
        r.poll(1);
        TEST_ASSERT_EQUAL(2, r.modem.active().s[1]);
        r.poll(3000);
        TEST_ASSERT_EQUAL(console, r.console.callUp());
        TEST_ASSERT_EQUAL(!console, r.host.callUp());
        r.output.take();
        r.hostOutput.take();
        r.poll(3000);
        TEST_ASSERT_TRUE(r.output.text.empty() && r.hostOutput.text.empty());
    }
}
void test_s0_one_answers_first_ring_without_silent_delay() {
    Rig r;
    r.modem.setAnswerPort(modemcfg::AnswerPort::kConsole);
    r.modem.active().s[0] = 1;
    r.ring();
    TEST_ASSERT_TRUE(r.console.callUp());
    TEST_ASSERT_TRUE(r.sound.stages.empty());
}
void test_ri_uses_uk_double_ring_even_when_muted() {
    Rig r;
    r.ring();
    TEST_ASSERT_TRUE(r.call.ri());
    r.poll(400);
    TEST_ASSERT_FALSE(r.call.ri());
    r.poll(200);
    TEST_ASSERT_TRUE(r.call.ri());
    r.poll(400);
    TEST_ASSERT_FALSE(r.call.ri());
    r.poll(2000);
    TEST_ASSERT_TRUE(r.call.ri());
}
void test_ring_clock_rollover_saturation_and_delayed_poll() {
    clockMs = UINT32_MAX - 50;
    Rig r;
    r.modem.setAnswerPort(modemcfg::AnswerPort::kRs232);  // no DTR: rings without answering
    r.modem.active().s[0] = 255;
    r.host.tick(clockMs, false);
    r.ring();
    r.poll(9000, false);
    TEST_ASSERT_EQUAL(2, r.modem.active().s[1]);
    for (unsigned i = 0; i < 260; ++i)
        r.poll(3000, false);
    TEST_ASSERT_EQUAL(255, r.modem.active().s[1]);
    TEST_ASSERT_TRUE(r.call.ringing());
}
void test_extra_callers_rejected_during_ring_and_online() {
    Rig r;
    r.ring();
    ++r.listener.pending;
    r.poll();
    TEST_ASSERT_EQUAL(1, r.listener.rejected);
    r.send("ATA\r");
    ++r.listener.pending;
    r.poll();
    TEST_ASSERT_EQUAL(2, r.listener.rejected);
    TEST_ASSERT_TRUE(r.console.callUp());
    r.send("ATA\r", true);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.hostOutput.text.find("ERROR"));
}
void test_existing_call_prevents_acceptance() {
    Rig r;
    hostport::EchoConnection echo;
    echo.open(0, 0);
    TEST_ASSERT_TRUE(r.host.startCall(echo));
    r.ring();
    TEST_ASSERT_EQUAL(1, r.listener.rejected);
    TEST_ASSERT_FALSE(r.call.ringing());
    TEST_ASSERT_TRUE(r.host.callUp());
}
void test_pending_ath_and_atz_cancel_and_next_ata_without_caller_errors() {
    for (const char *command : {"ATH\r", "ATZ\r"}) {
        Rig r;
        r.ring();
        r.send(command);
        TEST_ASSERT_FALSE(r.slot.busy());
        TEST_ASSERT_FALSE(r.call.ringing());
        r.output.take();
        r.send("ATA\r");
        TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("ERROR"));
    }
}
void test_no_dtr_blocks_auto_answer_without_falling_back_to_console() {
    Rig r;
    r.modem.setAnswerPort(modemcfg::AnswerPort::kRs232);  // fixed: no fallback
    r.host.tick(0, false);
    r.poll(51, false);
    r.modem.active().s[0] = 1;
    r.ring();
    r.poll(3000, false);
    TEST_ASSERT_TRUE(r.call.ringing());
    r.modem.active().v[modemcfg::kAmpD] = 0;
    r.poll(3000, false);
    TEST_ASSERT_TRUE(r.host.callUp());
    TEST_ASSERT_FALSE(r.console.callUp());
}
void test_handshake_is_asynchronous_and_cancellable() {
    Rig r;
    r.modem.active().v[modemcfg::kM] = 1;
    r.ring();
    TEST_ASSERT_EQUAL(int(SoundStage::kRing), int(r.sound.stages.back()));
    r.send("ATA\r");
    TEST_ASSERT_EQUAL(int(SoundStage::kHandshake), int(r.sound.stages.back()));
    TEST_ASSERT_FALSE(r.console.callUp());
    r.output.take();
    r.send("x");
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("NO CARRIER"));
}
void test_handshake_completion_and_failures_cleanup() {
    for (bool fail : {false, true}) {
        Rig r;
        r.modem.active().v[modemcfg::kM] = 1;
        r.ring();
        r.send("ATA\r");
        r.sound.active = false;
        r.sound.broken = fail;
        r.poll();
        TEST_ASSERT_EQUAL(!fail, r.console.callUp());
        TEST_ASSERT_EQUAL(!fail, r.slot.busy());
    }
}
void test_remote_disconnect_and_wifi_loss_release_every_stage() {
    for (unsigned stage = 0; stage < 3; ++stage) {
        for (bool network : {false, true}) {
            Rig r;
            r.modem.active().v[modemcfg::kM] = stage == 1 ? 1 : 0;
            r.ring();
            if (stage)
                r.send("ATA\r");
            if (network)
                wifi = false;
            else
                r.socket.status = SocketState::kClosed;
            r.poll();
            TEST_ASSERT_FALSE(r.slot.busy());
            TEST_ASSERT_FALSE(r.call.ri());
            TEST_ASSERT_FALSE(r.sound.active);
            wifi = true;
            r.poll();
            r.ring();
            TEST_ASSERT_TRUE(r.call.ringing());
        }
    }
}
void test_remote_commands_and_guarded_escape_are_data_even_locally_escaped() {
    Rig r;
    r.socket.chunk = 1;
    r.online();
    std::string attack =
        "AT\rATA\rATH\rAT*HELP\rAT*PASS=canary\rAT*FACTORY\rAT*HWTEST\rAT*CONFIG\rATDT123\r+++";
    r.socket.input = attack;
    for (size_t i = 0; i < attack.size() + 10; ++i)
        r.poll(30);
    TEST_ASSERT_EQUAL_STRING(attack.c_str(), r.receive().c_str());
    TEST_ASSERT_EQUAL(0, dispatched);
    TEST_ASSERT_TRUE(r.console.callUp());
    r.poll(1200);
    r.send("+++");
    r.poll(1200);
    TEST_ASSERT_EQUAL(int(hostport::Mode::kOnlineCommand), int(r.console.mode()));
    r.output.take();
    r.socket.input = attack;
    for (size_t i = 0; i < attack.size() + 10; ++i)
        r.poll(30);
    TEST_ASSERT_TRUE(r.receive().empty());
    TEST_ASSERT_TRUE(r.output.text.empty());
    TEST_ASSERT_EQUAL(0, dispatched);
    r.send("ATO\r");
    TEST_ASSERT_EQUAL_STRING(attack.c_str(), r.receive().c_str());
    TEST_ASSERT_TRUE(r.socket.output.find("password") == std::string::npos);
}
void test_preanswer_payload_is_preserved_and_never_parsed() {
    Rig r;
    r.ring();
    r.socket.input = "AT*FACTORY\r";
    r.poll();
    TEST_ASSERT_EQUAL(0, dispatched);
    r.send("ATA\r");
    TEST_ASSERT_EQUAL_STRING("AT*FACTORY\r", r.receive().c_str());
    TEST_ASSERT_EQUAL(0, dispatched);
}
void test_answerport_persistence_reset_rollback_and_busy_rejection() {
    Rig r;
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kOk), int(r.call.command(r.console.port(), "=CONSOLE")));
    atcmd::Modem again(r.store, nullptr, 0);
    again.begin();
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kConsole), int(again.answerPort()));
    r.modem.reset(4);
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kConsole), int(r.modem.answerPort()));
    r.store.fail = true;
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kError), int(r.call.command(r.console.port(), "=RS232")));
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kConsole), int(r.modem.answerPort()));
    r.store.fail = false;
    r.ring();
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kError), int(r.call.command(r.console.port(), "=RS232")));
    r.output.take();
    r.call.command(r.console.port(), "?");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("CONSOLE"));
    r.output.take();
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kOk), int(r.call.command(r.console.port(), "=?")));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("RS232,USB,CONSOLE,AUTO"));
}
void test_answerport_accepts_usb_and_auto_and_rejects_unknown() {
    Rig r;
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kAuto), int(r.modem.answerPort()));
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kOk), int(r.call.command(r.console.port(), "=usb")));
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kUsb), int(r.modem.answerPort()));
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kOk), int(r.call.command(r.console.port(), "=AUTO")));
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kAuto), int(r.modem.answerPort()));
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kError), int(r.call.command(r.console.port(), "=USB2")));
    TEST_ASSERT_EQUAL(int(atcmd::Reply::kError), int(r.call.command(r.console.port(), "=")));
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kAuto), int(r.modem.answerPort()));
    r.output.take();
    r.call.command(r.console.port(), "?");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("AUTO"));
}
void test_pick_answer_port_fixed_and_auto_order() {
    using modemcfg::AnswerPort;
    for (AnswerPort fixed : {AnswerPort::kRs232, AnswerPort::kUsb, AnswerPort::kConsole}) {
        for (int dtr = 0; dtr < 4; ++dtr) {
            TEST_ASSERT_EQUAL(int(fixed), int(pickAnswerPort(fixed, dtr & 1, dtr & 2)));
        }
    }
    TEST_ASSERT_EQUAL(int(AnswerPort::kRs232), int(pickAnswerPort(AnswerPort::kAuto, true, true)));
    TEST_ASSERT_EQUAL(int(AnswerPort::kRs232), int(pickAnswerPort(AnswerPort::kAuto, true, false)));
    TEST_ASSERT_EQUAL(int(AnswerPort::kUsb), int(pickAnswerPort(AnswerPort::kAuto, false, true)));
    TEST_ASSERT_EQUAL(int(AnswerPort::kConsole),
                      int(pickAnswerPort(AnswerPort::kAuto, false, false)));
}
void test_auto_answer_follows_dtr_rs232_then_usb_then_console() {
    // DTR on each port; expected: 0 RS232, 1 USB, 2 console.
    const struct {
        bool rs232, usb;
        int expected;
    } cases[] = {{true, true, 0}, {true, false, 0}, {false, true, 1}, {false, false, 2}};
    for (const auto &c : cases) {
        Rig r;
        r.usbDtr = c.usb;
        r.host.tick(clockMs, c.rs232);
        r.usb.tick(clockMs, c.usb);
        r.poll(51, c.rs232);  // past the DTR filter
        r.modem.active().s[0] = 1;
        r.ring();
        TEST_ASSERT_EQUAL(c.expected == 0, r.host.callUp());
        TEST_ASSERT_EQUAL(c.expected == 1, r.usb.callUp());
        TEST_ASSERT_EQUAL(c.expected == 2, r.console.callUp());
    }
}
void test_auto_answer_on_usb_rings_every_port_and_drops_with_usb_dtr() {
    Rig r;
    r.usbDtr = true;
    r.poll(51, false);
    r.modem.active().s[0] = 2;
    r.ring();
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("RING"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.hostOutput.text.find("RING"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.usbOutput.text.find("RING"));
    r.poll(3000, false);
    TEST_ASSERT_TRUE(r.usb.callUp());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.usbOutput.text.find("CONNECT"));
    r.usbDtr = false;
    r.poll(1, false);  // the DTR filter times the drop from here
    r.poll(100, false);
    TEST_ASSERT_FALSE(r.usb.callUp());
    TEST_ASSERT_FALSE(r.slot.busy());
}
void test_numeric_and_quiet_results() {
    Rig r;
    r.modem.active().v[modemcfg::kE] = 0;
    r.modem.active().v[modemcfg::kV] = 0;
    r.ring();
    TEST_ASSERT_EQUAL_STRING("2\r", r.output.take().c_str());
    r.send("ATH\r");
    r.output.take();
    r.modem.active().v[modemcfg::kQ] = 2;
    r.online();
    TEST_ASSERT_TRUE(r.output.text.empty());
    r.socket.status = SocketState::kClosed;
    r.poll();
    TEST_ASSERT_TRUE(r.output.text.empty());
    r.modem.active().v[modemcfg::kM] = 1;
    r.ring();
    r.send("ATA\r");
    r.send("x");
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_TRUE(r.output.text.empty());
}
void test_ring_is_unsolicited_and_replies_are_not() {
    // STA-01: RING is output nobody asked for, so the port's sink may drop it when the
    // computer is not reading. Command replies (ATA's CONNECT) are not.
    Rig r;
    r.usbDtr = true;
    r.poll();
    r.ring();
    TEST_ASSERT_EQUAL_STRING("\r\nRING\r\n", r.output.unsolicited.c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nRING\r\n", r.hostOutput.unsolicited.c_str());
    TEST_ASSERT_EQUAL_STRING("\r\nRING\r\n", r.usbOutput.unsolicited.c_str());
    r.send("ATA\r");
    TEST_ASSERT_TRUE(r.console.callUp());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("CONNECT"));
    TEST_ASSERT_EQUAL_STRING("\r\nRING\r\n", r.output.unsolicited.c_str());
}
void test_rings_to_an_unread_port_are_dropped_and_the_call_still_answers() {
    Rig r;
    r.hostOutput.full = true;
    r.modem.active().s[0] = 3;
    r.ring();
    for (int i = 0; i < 20 && r.call.ringing(); ++i) {
        r.poll(3000);
    }
    TEST_ASSERT_EQUAL(std::string::npos, r.hostOutput.text.find("RING"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("RING"));
    TEST_ASSERT_TRUE(r.host.callUp());  // answered on the selected port all the same
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.hostOutput.text.find("CONNECT"));
}
void test_listen_failure_retries_and_resource_takeover_closes() {
    Rig r;
    r.listener.fail = true;
    r.call.poll();
    r.poll(4999);
    TEST_ASSERT_EQUAL(1, r.listener.starts);
    r.listener.fail = false;
    r.poll(1);
    TEST_ASSERT_TRUE(r.call.listening());
    r.ring();
    r.call.poll(false);
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_FALSE(r.call.listening());
}
void test_live_s1_not_saved_and_new_call_resets_count() {
    Rig r;
    r.modem.active().s[0] = 3;
    r.ring();
    TEST_ASSERT_EQUAL(1, r.modem.active().s[1]);
    r.send("AT&W\r");
    TEST_ASSERT_EQUAL(0, r.modem.nvram().tmpl[0].s[1]);
    atcmd::Modem again(r.store, nullptr, 0);
    again.begin();
    TEST_ASSERT_EQUAL(0, again.active().s[1]);
    TEST_ASSERT_EQUAL(3, again.active().s[0]);
    r.send("ATH\r");
    r.ring();
    TEST_ASSERT_EQUAL(1, r.modem.active().s[1]);
}
void test_auto_answer_does_not_interrupt_local_input_or_secret_prompt() {
    for (bool prompt : {false, true}) {
        Rig r;
        r.modem.setAnswerPort(modemcfg::AnswerPort::kConsole);
        r.modem.active().s[0] = 1;
        if (prompt) {
            r.console.port().askSecret("Password:", nullptr, nullptr);
        } else {
            r.send("AT");
        }
        r.output.take();
        r.ring();
        TEST_ASSERT_TRUE(r.output.text.empty());
        TEST_ASSERT_TRUE(r.call.ringing());
        if (prompt) {
            TEST_ASSERT_TRUE(r.console.port().cancelPrompt());
        } else {
            r.send("\r");
        }
        r.poll(3000);
        TEST_ASSERT_TRUE(r.console.callUp());
    }
}
void test_preanswer_flood_is_bounded_then_drains_after_answer() {
    Rig r;
    r.ring();
    r.socket.input.assign(12000, 'a');
    for (unsigned i = 0; i < 100; ++i)
        r.poll();
    TEST_ASSERT_TRUE(r.socket.input.size() >= 12000 - 8192);
    TEST_ASSERT_EQUAL(0, dispatched);
    r.send("ATA\r");
    size_t got = 0;
    for (unsigned i = 0; i < 100; ++i) {
        r.poll();
        std::string data = r.receive();
        TEST_ASSERT_TRUE(data.find_first_not_of('a') == std::string::npos);
        got += data.size();
    }
    TEST_ASSERT_EQUAL(12000, got);
}
void test_answer_snapshots_model_and_explicit_line_speed() {
    Rig r;
    r.modem.setModel(56000);
    r.online();
    TEST_ASSERT_EQUAL_UINT32(56000, r.console.reportedSpeed());
    TEST_ASSERT_EQUAL_UINT32(14400, r.console.lineSpeed());
    r.modem.setModel(28800);
    r.poll(1200);
    r.send("+++");
    r.poll(1200);
    r.output.take();
    r.send("ATO\r");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("CONNECT 56000"));
    r.console.hangUp();
    r.modem.active().lineExplicit = true;
    r.modem.active().v[modemcfg::kAmpN] = 38;
    r.ring();
    r.send("ATA\r");
    TEST_ASSERT_EQUAL_UINT32(56000, r.console.lineSpeed());
    TEST_ASSERT_EQUAL_UINT32(56000, r.console.reportedSpeed());
}
void test_minimum_speed_and_failed_handshake_release_reservation() {
    Rig r;
    r.modem.active().v[modemcfg::kAmpU] = 38;
    r.ring();
    r.send("ATA\r");
    TEST_ASSERT_FALSE(r.slot.busy());
    r.modem.active().v[modemcfg::kAmpU] = 0;
    r.modem.active().v[modemcfg::kM] = 1;
    r.ring();
    r.sound.fail = true;
    r.send("ATA\r");
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_FALSE(r.console.callUp());
}
void test_local_session_win_and_remote_disconnect_clear_queued_bytes() {
    Rig r;
    r.ring();
    r.send("ATA\r", true);
    TEST_ASSERT_TRUE(r.host.callUp());
    r.send("ATA\r");
    TEST_ASSERT_FALSE(r.console.callUp());
    r.host.hangUp();
    r.ring();
    r.socket.input = "AT*FACTORY\r";
    r.poll();
    r.socket.status = SocketState::kClosed;
    r.poll();
    r.online();
    TEST_ASSERT_TRUE(r.receive().empty());
    TEST_ASSERT_EQUAL(0, dispatched);
}
void test_failed_host_speed_transition_releases_call_and_recovers() {
    class FailBaud : public hostport::Session::BaudControl {
    public:
        bool fail = true;
        unsigned commands = 0;
        bool online(uint32_t) override { return !fail; }
        void command() override { ++commands; }
    } baud;
    Rig r;
    r.host.setBaudControl(&baud);
    r.ring();
    r.send("ATA\r", true);
    TEST_ASSERT_FALSE(r.host.callUp());
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_EQUAL(int(SocketState::kClosed), int(r.socket.state()));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.hostOutput.text.find("NO CARRIER"));
    r.ring();
    r.send("ATA\r");
    TEST_ASSERT_TRUE(r.console.callUp());
    r.console.hangUp();
    baud.fail = false;
    r.ring();
    r.send("ATA\r", true);
    TEST_ASSERT_TRUE(r.host.callUp());
    unsigned before = baud.commands;
    r.host.hangUp();
    TEST_ASSERT_EQUAL(before + 1, baud.commands);  // no recursive second teardown
}
void test_socket_and_network_loss_emit_only_one_carrier_result() {
    Rig r;
    r.online();
    r.socket.status = SocketState::kClosed;
    r.console.tick(clockMs, true);  // Session observes TCP loss first
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.output.text.find("NO CARRIER"));
    r.output.take();
    wifi = false;
    r.call.poll();  // then the listener observes Wi-Fi loss in the same loop
    TEST_ASSERT_TRUE(r.output.text.empty());
    TEST_ASSERT_FALSE(r.slot.busy());
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_manual_answer_reserves_transfers_and_releases_slot);
    RUN_TEST(test_s0_zero_never_auto_answers_and_s1_stays_zero);
    RUN_TEST(test_auto_answer_uses_selected_port_and_exact_ring_threshold);
    RUN_TEST(test_s0_one_answers_first_ring_without_silent_delay);
    RUN_TEST(test_ri_uses_uk_double_ring_even_when_muted);
    RUN_TEST(test_ring_clock_rollover_saturation_and_delayed_poll);
    RUN_TEST(test_extra_callers_rejected_during_ring_and_online);
    RUN_TEST(test_existing_call_prevents_acceptance);
    RUN_TEST(test_pending_ath_and_atz_cancel_and_next_ata_without_caller_errors);
    RUN_TEST(test_no_dtr_blocks_auto_answer_without_falling_back_to_console);
    RUN_TEST(test_handshake_is_asynchronous_and_cancellable);
    RUN_TEST(test_handshake_completion_and_failures_cleanup);
    RUN_TEST(test_remote_disconnect_and_wifi_loss_release_every_stage);
    RUN_TEST(test_remote_commands_and_guarded_escape_are_data_even_locally_escaped);
    RUN_TEST(test_preanswer_payload_is_preserved_and_never_parsed);
    RUN_TEST(test_answerport_persistence_reset_rollback_and_busy_rejection);
    RUN_TEST(test_answerport_accepts_usb_and_auto_and_rejects_unknown);
    RUN_TEST(test_pick_answer_port_fixed_and_auto_order);
    RUN_TEST(test_auto_answer_follows_dtr_rs232_then_usb_then_console);
    RUN_TEST(test_auto_answer_on_usb_rings_every_port_and_drops_with_usb_dtr);
    RUN_TEST(test_numeric_and_quiet_results);
    RUN_TEST(test_ring_is_unsolicited_and_replies_are_not);
    RUN_TEST(test_rings_to_an_unread_port_are_dropped_and_the_call_still_answers);
    RUN_TEST(test_listen_failure_retries_and_resource_takeover_closes);
    RUN_TEST(test_live_s1_not_saved_and_new_call_resets_count);
    RUN_TEST(test_auto_answer_does_not_interrupt_local_input_or_secret_prompt);
    RUN_TEST(test_preanswer_flood_is_bounded_then_drains_after_answer);
    RUN_TEST(test_answer_snapshots_model_and_explicit_line_speed);
    RUN_TEST(test_minimum_speed_and_failed_handshake_release_reservation);
    RUN_TEST(test_local_session_win_and_remote_disconnect_clear_queued_bytes);
    RUN_TEST(test_failed_host_speed_transition_releases_call_and_recovers);
    RUN_TEST(test_socket_and_network_loss_emit_only_one_carrier_result);
    return UNITY_END();
}
