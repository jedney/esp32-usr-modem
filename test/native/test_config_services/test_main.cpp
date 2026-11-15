#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#define FAKES_STORE
#define FAKES_SINK
#define FAKES_SOUND
#include "../support/fakes.h"
#include "call_journal.h"
#include "config.h"
#include "config_dialer.h"
#include "registry.h"
using namespace config;
void setUp() {}
void tearDown() {}
using Store = fakes::MemoryStore;
using Sink = fakes::TextSink;
class Radio : public netcfg::Radio {
public:
    bool up = false;  // a join succeeds at once
    void connect(const netcfg::Settings &) override {}
    void leave() override {}
    void off() override {}
    netcfg::LinkInfo link() override {
        netcfg::LinkInfo info = {};
        if (up) {
            info.state = netcfg::LinkState::kUp;
        }
        return info;
    }
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
    // Different hashes for different passwords, so a leaked hash can be searched for.
    static void hex(uint32_t seed, const char *text, char *out, size_t length) {
        for (size_t word = 0; word < length / 8; ++word) {
            uint32_t h = 2166136261u ^ (seed + uint32_t(word) * 7919u);
            for (const char *c = text; *c; ++c) {
                h = (h ^ uint8_t(*c)) * 16777619u;
            }
            snprintf(out + word * 8, 9, "%08x", unsigned(h));
        }
    }
    bool hashes(const char *password, char a[devicecfg::kOtaHashLength + 1],
                char b[devicecfg::kDigestHashLength + 1]) override {
        hex(1, password, a, devicecfg::kOtaHashLength);
        hex(2, password, b, devicecfg::kDigestHashLength);
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
bool callIsUp = false;  // what the Wi-Fi commands are told about calls
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
    modemcfg::AnswerPort port;
    if (*a == '=' && modemcfg::parseAnswerPort(a + 1, &port)) {
        return p.modem().setAnswerPort(port) ? atcmd::Reply::kOk : atcmd::Reply::kError;
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
        netcfg::attachCommands(net, now, [] { return callIsUp; });
        devicecfg::attachCommands(deviceCommands);
    }
    std::string run(const char *s) {
        sink.text.clear();
        session.port().execute(s);
        return sink.text;
    }
};
void test_about_is_compact_names_and_urls_and_ends_at_project_repository() {
    Rig r;
    const auto text = r.run("*ABOUT");
    TEST_ASSERT_LESS_THAN(3500, text.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("Arduino-ESP32 / ArduinoOTA: https://"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("Zimodem: https://"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, text.find("xterm.js: https://"));
    TEST_ASSERT_EQUAL(std::string::npos, text.find("Obligations"));
    TEST_ASSERT_EQUAL(std::string::npos, text.find("Keep upstream"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          text.find("Full credits and licences: credits/LICENSES.md"));
    const auto repo = r.run("*GITHUB");
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          repo.find("https://github.com/jedney/esp32-usr-modem"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, repo.find("OK"));
}
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
    source.run("*ANSWERPORT=USB");
    source.run("*RINGDELAY=4321");
    source.run("*CONNECTDELAY=567");
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
    TEST_ASSERT_EQUAL(int(modemcfg::AnswerPort::kUsb), int(restored.modem.answerPort()));
    TEST_ASSERT_EQUAL_UINT32(4321, restored.modem.ringDelayMs());
    TEST_ASSERT_EQUAL_UINT32(567, restored.modem.connectDelayMs());
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
void test_setting_wrapper_accepts_exactly_one_setting_command() {
    Rig r;
    for (const char *bad : {"*SETTING=E0DT555", "*SETTING=E0A", "*SETTING=E1H", "*SETTING=E0E1",
                            "*SETTING=Q0S7=45", "*SETTING=S7=45E0", "*SETTING=&N8Z", "*SETTING=E1x",
                            "*SETTING=&C1H", "*SETTING=S7=4x", "*SETTING=E-1"}) {
        TEST_ASSERT_NOT_EQUAL_MESSAGE(std::string::npos, r.run(bad).find("ERROR"), bad);
    }
    TEST_ASSERT_EQUAL(1, r.modem.active().get(modemcfg::kE));  // none of them ran
    for (const char *good : {"*SETTING=E0", "*SETTING=&N8", "*SETTING=S7=45", "*SETTING=S7.1=1",
                             "*SETTING=&Z2=bbs.example.net"}) {
        TEST_ASSERT_EQUAL_MESSAGE(std::string::npos, r.run(good).find("ERROR"), good);
    }
    TEST_ASSERT_EQUAL(0, r.modem.active().get(modemcfg::kE));
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
    void resolve(const char *) override { value = calls::SocketState::kResolved; }
    const char *address() const override { return "192.0.2.1"; }
    void poll() override {}
    calls::SocketState state() const override { return value; }
    size_t write(const uint8_t *, size_t) override { return 0; }
    size_t read(uint8_t *, size_t) override { return 0; }
    void close() override {}
};
using Sound = fakes::RecordingSound;
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
    sound.active = false;
    dialer.poll();
    sound.active = false;
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
void test_browser_reserved_dial_uses_its_own_menu() {
    Rig r;
    Sink hostSink, browserSink;
    hostport::Session host(r.modem, hostSink, 38400, r.slot);
    hostport::Session browser(r.modem, browserSink, 115200, r.slot);
    browser.port().setBrowser(true);
    Terminal consoleMenu(r.session, false), hostMenu(host, true), browserMenu(browser, false);
    Socket socket;
    Sound sound;
    calls::Outgoing outgoing(r.modem, r.phonebook, r.slot, socket, sound, now, network);
    Dialer dialer(r.session, consoleMenu, hostMenu, outgoing, r.slot, sound, now);
    dialer.setBrowser(&browser, &browserMenu);
    browser.setDialer(&dialer);
    r.run("M0");
    TEST_ASSERT_TRUE(browser.dial("123", false));
    TEST_ASSERT_TRUE(browserMenu.isOpen());
    TEST_ASSERT_FALSE(consoleMenu.isOpen());
    TEST_ASSERT_FALSE(hostMenu.isOpen());
    TEST_ASSERT_TRUE(browser.callUp());
    browser.hangUp();
    TEST_ASSERT_FALSE(r.slot.busy());
    r.run("M1");
    TEST_ASSERT_TRUE(browser.dial("123", false));
    TEST_ASSERT_TRUE(r.slot.busy());
    TEST_ASSERT_TRUE(browser.port().busy());
    browser.port().discardInput();
    browser.hangUp();
    TEST_ASSERT_FALSE(browser.port().busy());
    TEST_ASSERT_FALSE(r.slot.busy());
    TEST_ASSERT_FALSE(browserMenu.isOpen());
    for (const char *number : {"666", "667", "5517546", "5517545"})
        TEST_ASSERT_FALSE(browser.dial(number, false));
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
    feed("de\r");
    drain();
    feed("d\r");
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
    feed("dg\r");
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
// These stores could not fail, so a refused write was never tested. Each command says
// ERROR and leaves the live setting as it was; with the store working again it succeeds.
void test_store_write_failures_are_reported_and_change_nothing() {
    Rig r;
    r.bookStore.failSave = true;
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          r.run("*PHONEBOOK=42,bbs.example.com,2323,RAW,28800").find("ERROR"));
    TEST_ASSERT_NULL(r.phonebook.find("42"));
    r.bookStore.failSave = false;
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          r.run("*PHONEBOOK=42,bbs.example.com,2323,RAW,28800").find("OK"));
    TEST_ASSERT_NOT_NULL(r.phonebook.find("42"));

    r.netStore.failSave = true;
    const std::string host = r.net.settings().host;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*HOSTNAME=other-name").find("ERROR"));
    TEST_ASSERT_EQUAL_STRING(host.c_str(), r.net.settings().host);
    r.netStore.failSave = false;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*HOSTNAME=other-name").find("OK"));
    TEST_ASSERT_EQUAL_STRING("other-name", r.net.settings().host);

    // Saving the modem profile (&W) with a failing store reports it; the live profile stays.
    r.run("*SETTING=S7=33");
    TEST_ASSERT_EQUAL(33, r.modem.active().s[7]);
    r.settings.failSave = true;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*SETTING=&W").find("ERROR"));
    TEST_ASSERT_EQUAL(33, r.modem.active().s[7]);
    r.settings.failSave = false;
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*SETTING=&W").find("OK"));

    // A password change that cannot be stored is refused and the old password still works.
    r.auth.failSave = true;
    const std::string digest = r.device.digestHash();
    TEST_ASSERT_FALSE(r.device.change("NewPassword99"));
    TEST_ASSERT_EQUAL_STRING(digest.c_str(), r.device.digestHash());
    r.auth.failSave = false;
    TEST_ASSERT_TRUE(r.device.change("NewPassword99"));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(digest.c_str(), r.device.digestHash()));
}

// A join, a scan or an address change would drop the call, so each is refused.
void test_wifi_join_scan_and_address_change_are_refused_during_a_call() {
    Rig r;
    callIsUp = true;
    const std::string before = r.net.settings().host;
    for (const char *line : {"*WIFISCAN", "*WIFIJOIN=\"Home\"", "*WIFIIP=DHCP",
                             "*WIFIIP=10.0.0.5,255.255.255.0,10.0.0.1"}) {
        const std::string out = r.run(line);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(std::string::npos, out.find("A call is in progress"), line);
        TEST_ASSERT_NOT_EQUAL_MESSAGE(std::string::npos, out.find("ERROR"), line);
        TEST_ASSERT_FALSE_MESSAGE(r.session.port().busy(), line);  // no password prompt left open
    }
    TEST_ASSERT_TRUE(r.net.settings().dhcp);
    TEST_ASSERT_EQUAL_STRING(before.c_str(), r.net.settings().host);
    // Showing the settings is still allowed during a call.
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*WIFIIP?").find("DHCP"));
    TEST_ASSERT_NOT_EQUAL(std::string::npos, r.run("*WIFI?").find("OK"));
    // Once the call is over the same commands work again.
    callIsUp = false;
    TEST_ASSERT_NOT_EQUAL(std::string::npos,
                          r.run("*WIFIIP=10.0.0.5,255.255.255.0,10.0.0.1").find("OK"));
    TEST_ASSERT_FALSE(r.net.settings().dhcp);
    TEST_ASSERT_EQUAL(std::string::npos, r.run("*WIFIJOIN=\"Home\"").find("A call is in progress"));
    r.session.port().cancelPrompt();
}

// --- Neither password nor any stored form of it appears in any output ---------------
struct Services : config::UiServices {
    calls::Phonebook *book;
    explicit Services(calls::Phonebook &b) : book(&b) {}
    calls::Phonebook *phonebook() override { return book; }
    config::UiState state() const override { return {}; }
    const Commands *commands = nullptr;
    bool value(const char *id, char *out, size_t cap) const override {
        return commands && commands->value(id, out, cap);
    }
};
std::string upper(std::string text) {
    for (auto &c : text) {
        c = char(toupper(static_cast<unsigned char>(c)));
    }
    return text;
}
// Reads everything the configuration screen has ready.
std::string drainScreen(Terminal &t) {
    std::string out;
    uint8_t bytes[256];
    size_t n;
    while ((n = t.read(bytes, sizeof bytes)) != 0) {
        out.append(reinterpret_cast<char *>(bytes), n);
        TEST_ASSERT_LESS_THAN(400000, out.size());
    }
    return out;
}
void press(Terminal &t, const char *keys) {
    for (; *keys; ++keys) {
        uint8_t c = uint8_t(*keys);
        t.write(&c, 1);
    }
}
// Opens every configuration screen through the menu and returns all that was drawn.
std::string screenPath(Screen target) {
    if (target == Screen::Main)
        return "";
    size_t count;
    const auto *rows = actions(count);
    for (size_t i = 0; i < count; ++i)
        if (!*rows[i].command && rows[i].target == target)
            return screenPath(rows[i].screen) + rows[i].key;
    TEST_FAIL_MESSAGE("Screen has no parent route");
    return "";
}
std::string tourScreens(Rig &r, Services &services, size_t *visited) {
    std::string all;
    bool seen[14] = {};
    size_t count;
    const auto *a = actions(count);
    for (const char *style : {"PLAIN", "ANSI"}) {
        for (unsigned width : {20u, 40u, 80u, 132u}) {
            Terminal screen(r.session, false);
            screen.services(&services);
            screen.setStyle(style);
            screen.resize(width, 25);
            screen.open();
            all += drainScreen(screen);
            seen[0] = true;
            screen.close();
            for (size_t i = 0; i < count; ++i) {
                screen.open();
                drainScreen(screen);
                const auto path = screenPath(a[i].screen);
                press(screen, path.c_str());
                all += drainScreen(screen);
                seen[unsigned(a[i].screen)] = true;
                press(screen, std::string(1, a[i].key).c_str());
                all += drainScreen(screen);
                if (!*a[i].command) {
                    seen[unsigned(a[i].target)] = true;
                    for (unsigned page = 0; page < 5; ++page) {
                        press(screen, "r");
                        all += drainScreen(screen);
                    }
                }
                screen.close();
            }
        }
    }
    *visited = 0;
    for (bool s : seen)
        *visited += s;
    return all;
}
void test_no_output_contains_either_password_or_its_stored_forms() {
    const char wifiCanary[] = "CanaryWiFi7Q";
    const char deviceCanary[] = "CanaryDev3Z9";
    Rig r;
    r.radio.up = true;
    TEST_ASSERT_TRUE(r.net.join("CanaryNet", wifiCanary, 0));
    r.net.tick(10);
    TEST_ASSERT_EQUAL(int(netcfg::JoinResult::kJoined), int(r.net.joinResult()));
    TEST_ASSERT_EQUAL_STRING(wifiCanary, r.net.settings().pass);  // it really was stored
    TEST_ASSERT_TRUE(r.device.change(deviceCanary));
    r.run("*LOG=2");
    r.run("*PHONEBOOK=42,bbs.example.com,2323,RAW,28800");
    // Everything that must stay out of every output.
    std::vector<std::string> secrets = {wifiCanary, deviceCanary, r.device.otaHash(),
                                        r.device.digestHash()};
    TEST_ASSERT_EQUAL(64, strlen(r.device.otaHash()));
    TEST_ASSERT_EQUAL(32, strlen(r.device.digestHash()));
    TEST_ASSERT_NOT_EQUAL(0, strcmp(r.device.otaHash(), r.device.digestHash()));
    const std::string ota = r.device.otaHash(), digest = r.device.digestHash();

    // Commands that would destroy the canaries, restart, or take over the port. They are
    // skipped by name; their other argument forms are not (AT*PASS? and the like).
    const std::vector<std::string> skippedBare = {"PASS", "FACTORY", "RESTART", "WIFIFORGET",
                                                  "BOOKCLEAR"};
    const std::vector<std::string> skippedAll = {"HW",       "SX",       "SY",   "SZ",     "RX",
                                                 "RY",       "RZ",       "TEST", "CONFIG", "PLAY",
                                                 "TESTCALL", "WIFIJOIN", "DIAL"};
    std::string seen;
    size_t commands = 0;
    auto sweep = [&](const std::string &line) {
        auto out = r.run(line.c_str());
        if (r.session.port().busy()) {  // a prompt or wait was started: end it
            r.session.port().cancelPrompt();
            r.session.port().feed(char(27));
        }
        seen += out;
    };
    for (size_t i = 0; i < registry.size(); ++i) {
        const std::string name = registry.data()[i].name;
        auto in = [&](const std::vector<std::string> &list) {
            for (const auto &item : list) {
                if (item == name) {
                    return true;
                }
            }
            return false;
        };
        if (in(skippedAll)) {
            continue;
        }
        ++commands;
        if (!in(skippedBare)) {
            sweep("*" + name);
        }
        sweep("*" + name + "?");
        sweep("*" + name + "=?");
    }
    TEST_ASSERT_GREATER_THAN(40, commands);
    sweep("&V");
    for (char digit = '0'; digit <= '7'; ++digit) {
        sweep(std::string("I") + digit);
    }
    sweep("*EXPORT");
    sweep("*STATUS");
    sweep("*LOG=0,200");
    sweep("*CALLLOG");
    sweep("*SETTING?");
    sweep("*ABOUT");
    sweep("*REFERENCE");
    // The sweep itself must not have changed the secrets it is looking for.
    TEST_ASSERT_EQUAL_STRING(wifiCanary, r.net.settings().pass);
    TEST_ASSERT_EQUAL_STRING(ota.c_str(), r.device.otaHash());
    TEST_ASSERT_EQUAL_STRING(digest.c_str(), r.device.digestHash());

    Services services(r.phonebook);
    services.commands = &r.commands;
    size_t visited = 0;
    const std::string drawn = tourScreens(r, services, &visited);
    TEST_ASSERT_EQUAL(14, visited);
    TEST_ASSERT_GREATER_THAN(2000, drawn.size());
    TEST_ASSERT_NOT_EQUAL(std::string::npos, drawn.find("bbs.example.com"));  // phonebook drawn
    seen += drawn;
    TEST_ASSERT_GREATER_THAN(3000, seen.size());
    for (const auto &secret : secrets) {
        const std::string lower = [&] {
            std::string t = secret;
            for (auto &c : t) {
                c = char(tolower(static_cast<unsigned char>(c)));
            }
            return t;
        }();
        TEST_ASSERT_EQUAL_MESSAGE(std::string::npos, seen.find(secret), "stored secret in output");
        TEST_ASSERT_EQUAL_MESSAGE(std::string::npos, seen.find(lower), "lower-case form in output");
        TEST_ASSERT_EQUAL_MESSAGE(std::string::npos, seen.find(upper(secret)),
                                  "upper-case form in output");
    }
}
void test_letter_profile_editor_dispatches_real_setting_commands() {
    Rig r;
    Terminal menu(r.session, false);
    menu.open();
    drainScreen(menu);
    press(menu, "dabba\r");
    TEST_ASSERT_NOT_EQUAL(std::string::npos, drainScreen(menu).find("*SETTING=E0"));
    press(menu, "y");
    drainScreen(menu);
    TEST_ASSERT_EQUAL(0, r.modem.active().get(modemcfg::kE));
    press(menu, "xrkdummy\r");
    drainScreen(menu);  // invalid numeric input remains in the form
    TEST_ASSERT_EQUAL(0, r.modem.active().s[0]);
    press(menu,
          "\x15"
          "4\r");
    drainScreen(menu);
    press(menu, "y");
    drainScreen(menu);
    TEST_ASSERT_EQUAL(4, r.modem.active().s[0]);
    menu.close();
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_about_is_compact_names_and_urls_and_ends_at_project_repository);
    RUN_TEST(test_letter_profile_editor_dispatches_real_setting_commands);
    RUN_TEST(test_menu_export_retains_complete_replayable_lines_across_pages);
    RUN_TEST(test_export_replays_nondefault_profiles_and_phonebook_without_secrets);
    RUN_TEST(test_setting_wrapper_applies_validation_and_tone_choice);
    RUN_TEST(test_setting_wrapper_accepts_exactly_one_setting_command);
    RUN_TEST(test_log_range_is_bounded_and_verbosity_does_not_capture_raw_text);
    RUN_TEST(test_journal_records_payload_and_cleanup_once_with_clock_wrap);
    RUN_TEST(test_reserved_dial_is_offline_local_and_can_escape_ato_hangup);
    RUN_TEST(test_reserved_dial_audio_stages_and_cancel_release_reservation);
    RUN_TEST(test_browser_reserved_dial_uses_its_own_menu);
    RUN_TEST(test_local_service_f0_never_echoes_password_and_escape_wipes_prompt);
    RUN_TEST(test_menu_command_results_are_readable_with_nonstandard_at_format);
    RUN_TEST(test_outgoing_history_failure_and_same_burst_redial_are_recorded_once);
    RUN_TEST(test_store_write_failures_are_reported_and_change_nothing);
    RUN_TEST(test_wifi_join_scan_and_address_change_are_refused_during_a_call);
    RUN_TEST(test_no_output_contains_either_password_or_its_stored_forms);
    return UNITY_END();
}
