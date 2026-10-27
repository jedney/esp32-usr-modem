#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#include "call_journal.h"
#include "config.h"
#include "config_dialer.h"
#include "registry.h"
using namespace config;
void setUp() {}
void tearDown() {}
class Store : public modemcfg::Store {
public:
    std::vector<uint8_t> bytes;
    size_t load(uint8_t *p, size_t n) override {
        n = n < bytes.size() ? n : bytes.size();
        if (n) {
            memcpy(p, bytes.data(), n);
        }
        return n;
    }
    bool save(const uint8_t *p, size_t n) override {
        bytes.assign(p, p + n);
        return true;
    }
    bool erase() override {
        bytes.clear();
        return true;
    }
};
class Sink : public atcmd::Sink {
public:
    std::string text;
    void write(const char *p, size_t n) override { text.append(p, n); }
};
class Radio : public netcfg::Radio {
public:
    void connect(const netcfg::Settings &) override {}
    void leave() override {}
    void off() override {}
    netcfg::LinkInfo link() override { return {}; }
    bool scanStart() override { return false; }
    int scanResult(netcfg::ScanEntry *, int) override { return -2; }
    void startServices(const netcfg::Settings &) override {}
    int64_t utcNow() override { return 123456789; }
    int64_t lastSync() override { return 0; }
    void mac(uint8_t out[6]) override { memset(out, 0, 6); }
};
class Crypto : public devicecfg::Crypto {
public:
    uint32_t value = 0;
    uint32_t randomWord() override { return ++value; }
    bool hashes(const char *, char a[33], char b[33]) override {
        memset(a, '1', 32);
        memset(b, '2', 32);
        a[32] = b[32] = 0;
        return true;
    }
};
class FakeHardware : public config::Hardware {
public:
    atcmd::Reply command(atcmd::Port &, const char *, const char *) override {
        return atcmd::Reply::kError;
    }
};
class Control : public devicecfg::Control {
public:
    uint32_t now() override { return 0; }
    bool idle() override { return true; }
    bool wifiUp() override { return false; }
    void restart() override {}
};
uint32_t clockMs = 0;
uint32_t now() {
    return clockMs;
}
int64_t utc() {
    return 123456789;
}
bool network() {
    return true;
}
calls::Phonebook *activeBook;
atcmd::Reply stub(atcmd::Port &, const char *) {
    return atcmd::Reply::kOk;
}
atcmd::Reply answer(atcmd::Port &p, const char *a) {
    if (!strcmp(a, "=CONSOLE") || !strcmp(a, "=RS232")) {
        return p.modem().setAnswerConsole(!strcmp(a, "=CONSOLE")) ? atcmd::Reply::kOk
                                                                  : atcmd::Reply::kError;
    }
    return atcmd::Reply::kError;
}
atcmd::Reply book(atcmd::Port &p, const char *a) {
    return activeBook->command(p, a, false);
}
Registry registry(Hooks{answer, book, stub, stub, stub, stub, stub, stub, stub});
struct Rig {
    Store settings, bookStore, netStore, auth, historyStore;
    Radio radio;
    Crypto crypto;
    Control control;
    FakeHardware hardware;
    Sink sink;
    atcmd::Modem modem{settings, registry.data(), registry.size()};
    hostport::CallSlot slot;
    hostport::Session session{modem, sink, 115200, slot};
    calls::Phonebook phonebook{bookStore};
    netcfg::Net net{netStore, radio};
    devicecfg::Device device{auth, crypto};
    History history{historyStore};
    Log log;
    char logData[256];
    Commands commands{log, history, phonebook, net, device, hardware};
    devicecfg::Commands deviceCommands{device, control, nullptr, 0};
    Rig() {
        modem.begin();
        phonebook.begin();
        net.begin(0);
        device.begin();
        history.begin();
        log.begin(logData, sizeof logData);
        Commands::attach(commands);
        activeBook = &phonebook;
        netcfg::attachCommands(net, now);
        devicecfg::attachCommands(deviceCommands);
    }
    std::string run(const char *s) {
        sink.text.clear();
        session.port().execute(s);
        return sink.text;
    }
};
void test_export_replays_nondefault_profiles_and_phonebook_without_secrets() {
    Rig source;
    source.run("E0M0&N38S7=21");
    source.run("*PROFILE=0,19200");
    source.run("E1M2&N8S7=33");
    source.run("*PROFILE=1,57600");
    source.run("*PROFILECTL=1,13,10,1,0");
    source.run("*SETTING=&Z2=bbs.example.net");
    source.run("*PHONEBOOK=42,bbs.example.com,2323,RAW,28800");
    source.run("*MODEL=33600");
    source.run("*THROTTLE=ON");
    source.run("*ANSWERPORT=CONSOLE");
    source.modem.setHostBaud(19200);
    source.modem.active().s[3] = 59;
    source.modem.active().s[4] = 12;
    source.run("*SETTING=Y1");
    source.run("*EXPLICIT=0");
    source.net.setHost("test-modem");
    source.net.setNtp("time.example.com");
    source.net.setTz("UTC");
    source.device.change("DeviceCanary123");
    source.run("*BOOKCLEAR");
    source.run("*PHONEBOOK=42,bbs.example.com,2323,RAW,28800");
    auto active = source.modem.active();
    auto stored = source.modem.nvram();
    auto script = source.run("*EXPORT");
    TEST_ASSERT_EQUAL(std::string::npos, script.find("DeviceCanary"));
    TEST_ASSERT_EQUAL(std::string::npos, script.find("*PASS"));
    TEST_ASSERT_EQUAL(std::string::npos, script.find("WIFIJOIN"));
    Rig restored;
    size_t start = 0;
    while (start < script.size()) {
        size_t end = script.find("\r\n", start);
        if (end == std::string::npos) {
            break;
        }
        auto line = script.substr(start, end - start);
        start = end + 2;
        if (line.rfind("AT", 0) != 0) {
            continue;
        }
        TEST_ASSERT_LESS_OR_EQUAL(atcmd::kLineMax + 2, line.size());
        auto reply = restored.run(line.c_str() + 2);
        TEST_ASSERT_EQUAL(std::string::npos, reply.find("ERROR"));
    }
    TEST_ASSERT_TRUE(active == restored.modem.active());
    TEST_ASSERT_TRUE(stored.tmpl[0] == restored.modem.nvram().tmpl[0]);
    TEST_ASSERT_TRUE(stored.tmpl[1] == restored.modem.nvram().tmpl[1]);
    TEST_ASSERT_EQUAL(1, restored.modem.nvram().y);
    TEST_ASSERT_EQUAL(33600, restored.modem.model());
    TEST_ASSERT_TRUE(restored.modem.throttle());
    TEST_ASSERT_TRUE(restored.modem.answerConsole());
    TEST_ASSERT_NOT_NULL(restored.phonebook.find("42"));
    TEST_ASSERT_EQUAL(19200, restored.modem.hostBaud());
    TEST_ASSERT_NULL(restored.phonebook.find("1"));
    TEST_ASSERT_EQUAL_STRING("test-modem", restored.net.settings().host);
}
void test_setting_wrapper_applies_validation_and_tone_choice() {
    Rig r;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*SETTING=&N39").find("ERROR"));
    r.run("*SETTING=T0");
    TEST_ASSERT_EQUAL(0, r.modem.active().get(modemcfg::kTone));
    r.run("*SETTING=T1");
    TEST_ASSERT_EQUAL(1, r.modem.active().get(modemcfg::kTone));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*SETTING=*FACTORY").find("ERROR"));
}
void test_log_range_is_bounded_and_verbosity_does_not_capture_raw_text() {
    Rig r;
    r.log.event(3, Event::Boot);
    auto out = r.run("*LOG=0,10");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, out.find("3 boot"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*LOG=0,9000").find("ERROR"));
    r.run("*LOG=0");
    r.log.event(4, Event::WifiDown);
    out = r.run("*LOG");
    TEST_ASSERT_EQUAL(std::string::npos, out.find("wifi down"));
}
void test_journal_records_payload_and_cleanup_once_with_clock_wrap() {
    Rig r;
    CallJournal journal(r.history, r.log, now, utc);
    calls::Entry entry;
    strcpy(entry.host, "bbs.example.com");
    journal.started(false, entry);
    hostport::EchoConnection echo;
    echo.open(0, 0);
    r.session.startCall(echo);
    clockMs = UINT32_MAX - 499;
    journal.connected(r.session);
    uint8_t data[] = {1, 2, 255};
    r.session.fromHost(data, 3, clockMs);
    uint8_t out[3];
    r.session.toHost(out, 3);
    clockMs = 1500;
    journal.ended(atcmd::Result::kNoCarrier);
    journal.ended(atcmd::Result::kNoCarrier);
    TEST_ASSERT_EQUAL(1, r.history.size());
    auto record = r.history.at(0);
    TEST_ASSERT_EQUAL(2, record.duration);
    TEST_ASSERT_EQUAL(3, record.sent);
    TEST_ASSERT_EQUAL(3, record.received);
    TEST_ASSERT_EQUAL(123456789, record.utc);
}
class Socket : public calls::Socket {
public:
    calls::SocketState value = calls::SocketState::kClosed;
    void begin(const char *, uint16_t) override { value = calls::SocketState::kPending; }
    void poll() override {}
    calls::SocketState state() const override { return value; }
    size_t write(const uint8_t *, size_t) override { return 0; }
    size_t read(uint8_t *, size_t) override { return 0; }
    void close() override {}
};
class Sound : public calls::CallSound {
public:
    bool on = false;
    unsigned starts = 0;
    std::vector<calls::SoundStage> stages;
    bool start(calls::SoundStage stage, const char *, bool, uint32_t,
               const modemcfg::Profile &) override {
        ++starts;
        stages.push_back(stage);
        on = true;
        return true;
    }
    bool running() const override { return on; }
    void stop() override { on = false; }
    void poll(uint32_t, const modemcfg::Profile &) override {}
};
void test_reserved_dial_is_offline_local_and_can_escape_ato_hangup() {
    Rig r;
    Sink other;
    hostport::Session host(r.modem, other, 38400, r.slot);
    Terminal consoleMenu(r.session, false), hostMenu(host, true);
    Socket socket;
    Sound sound;
    calls::Outgoing outgoing(r.modem, r.phonebook, r.slot, socket, sound, now, network);
    Dialer dialer(r.session, consoleMenu, hostMenu, outgoing, r.slot, sound, now);
    r.session.setDialer(&dialer);
    r.run("M0");
    auto result = r.run("DT123");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, result.find("CONNECT"));
    TEST_ASSERT_TRUE(r.session.callUp());
    TEST_ASSERT_EQUAL(0, sound.starts);
    TEST_ASSERT_TRUE(consoleMenu.isOpen());
    clockMs = 2000;
    const uint8_t plus[] = {'+', '+', '+'};
    r.session.fromHost(plus, 3, clockMs);
    r.session.tick(3100, true);
    TEST_ASSERT_EQUAL(int(hostport::Mode::kOnlineCommand), int(r.session.mode()));
    r.run("O");
    TEST_ASSERT_EQUAL(int(hostport::Mode::kOnline), int(r.session.mode()));
    r.session.hangUp();
    TEST_ASSERT_FALSE(consoleMenu.isOpen());
    TEST_ASSERT_FALSE(r.slot.busy());
}
void test_reserved_dial_audio_stages_and_cancel_release_reservation() {
    Rig r;
    Sink other;
    hostport::Session host(r.modem, other, 38400, r.slot);
    Terminal menu(r.session, false), hostMenu(host, true);
    Socket socket;
    Sound sound;
    calls::Outgoing outgoing(r.modem, r.phonebook, r.slot, socket, sound, now, network);
    Dialer dialer(r.session, menu, hostMenu, outgoing, r.slot, sound, now);
    r.session.setDialer(&dialer);
    r.run("DT123");
    TEST_ASSERT_TRUE(r.slot.busy());
    TEST_ASSERT_FALSE(r.session.callUp());
    sound.on = false;
    dialer.poll();
    sound.on = false;
    dialer.poll();
    clockMs += 1501;
    dialer.poll();
    TEST_ASSERT_EQUAL(3, sound.starts);
    TEST_ASSERT_TRUE(menu.isOpen());
    r.session.hangUp();
    r.run("DT123");
    r.session.port().feed('x');
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_FALSE(menu.isOpen());
}
void test_local_service_f0_never_echoes_password_and_escape_wipes_prompt() {
    Rig r;
    Terminal menu(r.session, false);
    menu.open();
    r.modem.active().v[modemcfg::kF] = 0;
    TEST_ASSERT_TRUE(r.session.startCall(menu));
    auto drain = [&]() {
        std::string text;
        uint8_t data[256];
        size_t n;
        while ((n = r.session.toHost(data, sizeof data))) {
            text.append(reinterpret_cast<char *>(data), n);
        }
        return text;
    };
    drain();
    auto feed = [&](const char *text) {
        while (*text) {
            uint8_t c = *text++;
            r.session.fromHost(&c, 1, clockMs);
        }
    };
    feed("8\r");
    drain();
    feed("4\r");
    drain();
    feed("SensitiveCanary");
    auto out = drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("SensitiveCanary"));
    menu.escaped();
    out = drain();
    TEST_ASSERT_EQUAL(std::string::npos, out.find("SensitiveCanary"));
}
void test_menu_command_results_are_readable_with_nonstandard_at_format() {
    Rig r;
    r.modem.active().s[3] = 59;
    r.modem.active().s[4] = 12;
    r.modem.active().v[modemcfg::kQ] = 1;
    r.modem.active().v[modemcfg::kV] = 0;
    Sink display;
    r.session.port().redirect(&display, true);
    r.session.port().execute("*SETTING=&N39");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, display.text.find("ERROR\r\n"));
    TEST_ASSERT_EQUAL(1, r.modem.active().get(modemcfg::kQ));
    TEST_ASSERT_EQUAL(59, r.modem.active().s[3]);
    r.session.port().redirect(nullptr);
}
void test_outgoing_history_failure_and_same_burst_redial_are_recorded_once() {
    Rig r;
    Socket socket;
    Sound sound;
    CallJournal journal(r.history, r.log, now, utc);
    calls::Outgoing outgoing(r.modem, r.phonebook, r.slot, socket, sound, now, network);
    outgoing.setObserver(&journal);
    r.run("M0");
    TEST_ASSERT_TRUE(outgoing.start(r.session, "bbs.example.com", false));
    socket.value = calls::SocketState::kRefused;
    outgoing.poll();
    TEST_ASSERT_EQUAL(1, r.history.size());
    TEST_ASSERT_EQUAL(static_cast<unsigned>(atcmd::Result::kBusy), r.history.at(0).result);
    TEST_ASSERT_TRUE(outgoing.start(r.session, "bbs.example.com", false));
    socket.value = calls::SocketState::kOpen;
    outgoing.poll();
    TEST_ASSERT_TRUE(r.session.callUp());
    r.session.hangUp();
    TEST_ASSERT_TRUE(outgoing.start(r.session, "bbs.example.com", false));
    TEST_ASSERT_EQUAL(2, r.history.size());
    outgoing.cancel();
    TEST_ASSERT_EQUAL(3, r.history.size());
}
void test_menu_export_retains_complete_replayable_lines_across_pages() {
    Rig r;
    Terminal menu(r.session, false);
    menu.setStyle("PLAIN");
    menu.open();
    auto drain = [&]() {
        std::string text;
        uint8_t byte;
        while (menu.read(&byte, 1))
            text += char(byte);
        return text;
    };
    auto feed = [&](const char *keys) {
        while (*keys) {
            uint8_t c = *keys++;
            menu.write(&c, 1);
        }
    };
    drain();
    feed("9\r");
    std::string output;
    for (unsigned i = 0; i < 30; ++i) {
        auto page = drain();
        output += page;
        if (page.find("Enter returns to menu") != std::string::npos)
            break;
        TEST_ASSERT_NOT_EQUAL(std::string::npos, page.find("More:"));
        feed("\r");
    }
    TEST_ASSERT_NOT_EQUAL(std::string::npos, output.find("AT*RESTOREEND="));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, output.find("AT*PROFILE=0,"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          output.find("AT*PHONEBOOK=1,www.amigaretro.com,23,TELNET,0\r\n"));
    TEST_ASSERT_EQUAL(std::string::npos, output.find("AT*PASS"));
    TEST_ASSERT_EQUAL(std::string::npos, output.find("AT*WIFIJOIN"));
    TEST_ASSERT_EQUAL(std::string::npos, output.find('|'));
    feed("\r");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, drain().find("Configuration"));
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_menu_export_retains_complete_replayable_lines_across_pages);
    RUN_TEST(test_export_replays_nondefault_profiles_and_phonebook_without_secrets);
    RUN_TEST(test_setting_wrapper_applies_validation_and_tone_choice);
    RUN_TEST(test_log_range_is_bounded_and_verbosity_does_not_capture_raw_text);
    RUN_TEST(test_journal_records_payload_and_cleanup_once_with_clock_wrap);
    RUN_TEST(test_reserved_dial_is_offline_local_and_can_escape_ato_hangup);
    RUN_TEST(test_reserved_dial_audio_stages_and_cancel_release_reservation);
    RUN_TEST(test_local_service_f0_never_echoes_password_and_escape_wipes_prompt);
    RUN_TEST(test_menu_command_results_are_readable_with_nonstandard_at_format);
    RUN_TEST(test_outgoing_history_failure_and_same_burst_redial_are_recorded_once);
    return UNITY_END();
}
