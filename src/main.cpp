// USR Modem Emulator - firmware entry point: creates the services and runs the main loop.
//
// Four local ports run the Sportster AT command parser (lib/atcmd) on one set of settings
// stored in NVS (lib/modemcfg, hw::NvsStore). Each has a session (lib/hostport) with
// command, online and online-command modes, and its own configuration screen (lib/config):
//   - the maintenance console (UART0, fixed 115200 8N1);
//   - the RS232 host port (UART1, rs232.cpp): speed from "AT" or AT*BAUD, flow control,
//     DTR, DCD, RI;
//   - the USB modem port on the S3's native USB (usbport.cpp);
//   - the browser terminal (web page, hw::WebService), explicitly opened by its owner.
// One call at a time (hostport::CallSlot): outgoing Telnet/TCP (calls::Outgoing),
// incoming on port 23 (calls::Incoming), PPP/SLIP Internet dial-up (lib/internet) and the
// AT*TESTCALL echo. Wi-Fi, mDNS and the NTP clock are in lib/netcfg; device credentials,
// OTA and factory reset in lib/devicecfg. The front-panel LEDs follow spec 2.4.
// Project commands are in lib/config (registry.cpp) with the hooks below; AT*HW... are
// the hardware tests (hwconsole.cpp), run on the port they were typed on.
#include <Arduino.h>
#include <LittleFS.h>

#include <esp_core_dump.h>
#include <lwip/inet.h>
#include <new>
#include "atcmd.h"
#include "call_audio.h"
#include "call_journal.h"
#include "client_platform.h"
#include "client_service.h"
#include "config.h"
#include "config_dialer.h"
#include "connection.h"
#include "console_out.h"
#include "device_commands.h"
#include "device_crypto.h"
#include "dialstring.h"
#include "file_store.h"
#include "hw.h"
#include "hwconsole.h"
#include "incoming.h"
#include "input_buffer.h"
#include "internet_dialer.h"
#include "internet_network.h"
#include "modemcfg.h"
#include "net_commands.h"
#include "net_console.h"
#include "net_probe.h"
#include "netcfg.h"
#include "nvs_store.h"
#include "ota_service.h"
#include "outgoing.h"
#include "panel.h"
#include "registry.h"
#include "rs232.h"
#include "session.h"
#include "tcp_listener.h"
#include "tcp_socket.h"
#include "usbport.h"
#include "web_service.h"
#include "wifi_radio.h"

// Sdkconfig.defaults turns the task watchdog's panic on; the 15 s is in the
// AT*RESETINFO text (hwconsole.cpp) and the decisions log.
static_assert(CONFIG_ESP_TASK_WDT_PANIC && CONFIG_ESP_TASK_WDT_TIMEOUT_S == 15,
              "Task watchdog: expected CONFIG_ESP_TASK_WDT_PANIC=y and a 15 s time-out");

#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif
#ifndef FW_COMMIT
#define FW_COMMIT "unknown"
#endif

namespace {

constexpr uint32_t kConsoleBaud = 115200;
constexpr size_t kConsoleTxBuffer = 16 * 1024;
constexpr uint32_t kBannerWaitMs = 8000;  // for Wi-Fi at boot

class SerialSink : public atcmd::Sink {
public:
    void write(const char *data, size_t len) override { consoleout::write(data, len); }
};

hw::NvsStore store;
hw::NvsStore netStore("net", "cfg");
hw::NvsStore deviceStore("device", "cfg");
hw::NvsStore bookStore("phonebook", "cfg");
calls::Phonebook phonebook(bookStore);
hw::TcpSocket tcpSocket;
hw::CallAudio callAudio;
calls::Outgoing *outgoing = nullptr;
hw::TcpSocket incomingSocket;
hw::TcpListener incomingListener(incomingSocket);
calls::Incoming *incoming = nullptr;
uint32_t incomingIp = 0;
bool answerListenFailed = false;
hw::EspCrypto crypto;
devicecfg::Device device(deviceStore, crypto);
hw::OtaService ota(device);
hw::NvsStore netConsoleStore("netconsole", "cfg");
netconsole::Settings netConsoleSettings(netConsoleStore);
hw::TcpSocket netConsoleSockets[2];
// Port 2001: connections go into either socket (a logged-in client and one at the prompt).
class NetConsoleListener : public netconsole::Listener {
public:
    bool begin(uint16_t port) override { return listener_.begin(port); }
    bool accept(unsigned slot) override { return listener_.accept(netConsoleSockets[slot & 1]); }
    void stop() override { listener_.stop(); }

private:
    hw::TcpListener listener_{netConsoleSockets[0]};
};
NetConsoleListener netConsoleListener;
const char *linkStatus();
const char *selectedModel();
class NetConsoleHost : public netconsole::Host {
public:
    bool passwordMatches(const char *password) override { return device.matches(password); }
    void loggedIn() override { device.markUsed(); }
    const char *modelName() override { return selectedModel(); }
    const char *status() override { return linkStatus(); }
};
NetConsoleHost netConsoleHost;
netconsole::Console netConsole(netConsoleListener, netConsoleSockets[0], netConsoleSockets[1],
                               consoleout::log(), netConsoleHost);
hw::NvsStore webStore("web", "cfg");
hw::NvsStore clientStore("clients", "cfg");
clienttest::Settings clientSettings(clientStore);
hw::NvsStore internetStore("internet", "cfg");
dialup::Settings internetSettings(internetStore);
hw::InternetNetwork internetNetwork;
dialup::Dialer *internetDialer = nullptr;
webinterface::Settings webSettings(webStore);
hw::WebService webServer(device, consoleout::log());
hw::EspRadio radio;
netcfg::Net net(netStore, radio);
SerialSink consoleSink;
hostport::InputBuffer consoleInput;
hostport::CallSlot callSlot;
hostport::EchoConnection echoCall;  // AT*TESTCALL
hostport::Panel panel;

// Set up in setup(), once the AT* table below exists.
atcmd::Modem *modem = nullptr;
hostport::Session *console = nullptr;
config::Terminal *consoleMenu = nullptr, *hostMenu = nullptr, *usbMenu = nullptr;
hostport::Session *browserSession = nullptr;
config::Terminal *browserMenu = nullptr;
uint32_t browserClient = 0;
hostport::InputBuffer browserInput;
uint32_t browserFromComputer = 0, browserToComputer = 0;
uint32_t browserCommandBytes = 0;
bool browserUtf8 = false;
bool hardwareRunning = false;
bool browserTestCancelled = false;
volatile const char imageMarker[] = "USRMODEM/1/FIRMWARE/" FW_VERSION "\n";
void pollBrowser();
void publishWebStatus();
void pollLeds(uint32_t now);
class BrowserSink : public atcmd::Sink {
public:
    void write(const char *data, size_t len) override {
        uint32_t progress = millis();
        while (len && webServer.connected(browserClient)) {
            size_t room = webServer.space(browserClient);
            size_t n = len < room ? len : room;
            if (n && webServer.write(browserClient, reinterpret_cast<const uint8_t *>(data), n)) {
                data += n;
                len -= n;
                browserToComputer += n;
                progress = millis();
            } else if (millis() - progress >= 2000) {
                webServer.close(browserClient);
                break;
            }
            webServer.pump();
            if (len)
                delay(1);
        }
    }
};
BrowserSink browserSink;
config::Dialer *configDialer = nullptr;
hw::FileStore historyStore("/call-history.bin", "/call-history.tmp", "/call-history.bak");
hw::NvsStore resetStore("diagnostics", "reset");
config::History history(historyStore);
config::Log debugLog;
hw::ClientPlatform consoleClientPlatform(debugLog, false), hostClientPlatform(debugLog, true),
    usbClientPlatform(debugLog, false, true);
clienttest::Service *consoleClient = nullptr, *hostClient = nullptr, *usbClient = nullptr;
uint32_t msNow();
int64_t utcNow() {
    return net.utcNow();
}
config::CallJournal journal(history, debugLog, msNow, utcNow);
char fallbackLog[4096];
uint8_t ledMask = 0xFF;
atcmd::Port *hardwareOwner = nullptr;
modemcfg::Store *resetStores[] = {&store,        &netStore,      &bookStore,
                                  &historyStore, &resetStore,    &webStore,
                                  &clientStore,  &internetStore, &netConsoleStore};
bool restarting = false;
uint32_t restartAt = 0;

class DeviceControl : public devicecfg::Control {
public:
    uint32_t now() override { return millis(); }
    bool idle() override {
        return !callSlot.busy() && net.joinResult() != netcfg::JoinResult::kRunning &&
               net.scanState() != netcfg::ScanState::kRunning;
    }
    bool wifiUp() override { return net.up(); }
    void restart() override {
        restarting = true;
        restartAt = millis();
    }
};
DeviceControl deviceControl;
devicecfg::Commands deviceCommands(device, deviceControl, resetStores,
                                   sizeof resetStores / sizeof resetStores[0]);

hostport::Session &sessionOf(atcmd::Port &port) {
    return *static_cast<hostport::Session *>(port.link());
}

bool isRs232(atcmd::Port &port) {
    return &port == &rs232::session().port();
}
bool isUsb(atcmd::Port &port) {
    return &port == &usbport::session().port();
}

// The local serial ports, indexed as their Client Tests records (console, RS232, USB).
constexpr unsigned kSerialPorts = clienttest::kPortCount;
hostport::Session &serialSession(unsigned i) {
    return i == clienttest::kRs232Port ? rs232::session()
           : i == clienttest::kUsbPort ? usbport::session()
                                       : *console;
}
config::Terminal *serialMenu(unsigned i) {
    return i == clienttest::kRs232Port ? hostMenu
           : i == clienttest::kUsbPort ? usbMenu
                                       : consoleMenu;
}
clienttest::Service *serialClient(unsigned i) {
    return i == clienttest::kRs232Port ? hostClient
           : i == clienttest::kUsbPort ? usbClient
                                       : consoleClient;
}
hw::ClientPlatform &serialPlatform(unsigned i) {
    return i == clienttest::kRs232Port ? hostClientPlatform
           : i == clienttest::kUsbPort ? usbClientPlatform
                                       : consoleClientPlatform;
}
unsigned serialIndex(atcmd::Port &port) {  // not for the browser
    return isRs232(port) ? clienttest::kRs232Port
           : isUsb(port) ? clienttest::kUsbPort
                         : clienttest::kConsolePort;
}

// A serial port's configuration screen and Backspace follow its Client Tests record.
void applyClientSettings(unsigned i) {
    const auto &s = clientSettings.get(i);
    auto *menu = serialMenu(i);
    menu->setStyle(s.charset == clienttest::Charset::Latin1 ? "ANSI"
                                                            : clienttest::charsetName(s.charset));
    menu->resize(std::min<unsigned>(s.columns, 132), std::min<unsigned>(s.rows, 50));
    menu->backspace(s.backspace);
    serialSession(i).port().clientBackspace(s.backspace);
}

config::Terminal &menuOf(atcmd::Port &port) {
    if (browserSession && &port == &browserSession->port()) {
        return *browserMenu;
    }
    return *serialMenu(serialIndex(port));
}
bool ownsMenu(atcmd::Port &port) {
    return consoleMenu && hostMenu && usbMenu && menuOf(port).isOpen();
}

// --- Hardware tests on the port they were typed on ------------------------------------------

void pollNetConsole();

class ConsoleTerm : public hwconsole::Term {
public:
    void write(const char *data, size_t len) override { consoleout::write(data, len); }
    int read() override {
        if (Serial.available() > 0) {
            return Serial.read();
        }
        uint8_t c;
        return netConsole.read(&c, 1) ? c : -1;
    }
    void service() override { pollNetConsole(); }
    bool isRs232() const override { return false; }
};

ConsoleTerm consoleTerm;

// The RS232 or USB modem port. Without DTR the computer has closed its port.
class HostTerm : public hwconsole::Term {
public:
    HostTerm(atcmd::Port &port, bool rs232) : port_(port), rs232_(rs232) {}
    void write(const char *data, size_t len) override { port_.write(data, len); }
    int read() override { return rs232_ ? rs232::readKey() : usbport::readKey(); }
    bool isRs232() const override { return rs232_; }
    bool connected() const override { return rs232_ ? hw::dtrAsserted() : usbport::dtrOn(); }

private:
    atcmd::Port &port_;
    bool rs232_;
};

class BrowserTerm : public hwconsole::Term {
public:
    void write(const char *data, size_t len) override { browserSink.write(data, len); }
    int read() override {
        service();
        if (!connected()) {
            return 0x18;
        }
        uint8_t c;
        if (!browserInput.read(&c, 1)) {
            if (!webServer.read(browserClient, &c, 1))
                return -1;
            ++browserFromComputer;
            ++browserCommandBytes;
        }
        if (c == 0x18) {
            browserTestCancelled = true;
        }
        return c;
    }
    void service() override {
        net.tick(millis());
        pollLeds(millis());
        publishWebStatus();
        webServer.pump();
    }
    bool connected() const override {
        return !browserTestCancelled && webServer.connected(browserClient);
    }
    bool isRs232() const override { return false; }
    bool isBrowser() const override { return true; }
};
BrowserTerm browserTerm;

// Runs `use` with the hardware-test terminal of the port a command was typed on.
template <typename Use>
void withTerm(atcmd::Port &port, Use use) {
    if (port.browser()) {
        use(browserTerm);
    } else if (isRs232(port) || isUsb(port)) {
        HostTerm t(port, isRs232(port));
        use(t);
    } else {
        use(consoleTerm);
    }
}

bool modemIdle() {
    return !callSlot.busy() ||
           (hardwareOwner && ownsMenu(*hardwareOwner) && !isRs232(*hardwareOwner) &&
            rs232::session().mode() == hostport::Mode::kCommand && !rs232::session().localOpen() &&
            rs232::session().port().lineEmpty());
}

bool networkUp() {
    return net.up();
}

// --- AT* commands -----------------------------------------------------------------------

atcmd::Reply cmdVer(atcmd::Port &port, const char *args) {
    if (*args != '\0' && *args != '?') {
        return atcmd::Reply::kError;
    }
    port.beginInfo();
    port.printf("v%s (%s)", FW_VERSION, FW_COMMIT);
    port.newline();
    return atcmd::Reply::kOk;
}

atcmd::Reply cmdHelp(atcmd::Port &port, const char *args) {
    if (*args != '\0') {
        return atcmd::Reply::kError;
    }
    atcmd::showStarHelp(port);
    withTerm(port, [](hwconsole::Term &t) { hwconsole::printHelp(t); });
    if (port.browser()) {
        port.line("The browser menus send these commands; answer prompts in the terminal.");
        port.line("Help > Diagnostics has recent console history. Hang up appears during calls.");
    }
    return atcmd::Reply::kOk;
}

atcmd::Reply cmdDialTest(atcmd::Port &port, const char *args) {
    dialstring::Result r = dialstring::classify(args);
    port.beginInfo();
    port.printf("kind=%s", dialstring::kindName(r.kind));
    switch (r.kind) {
        case dialstring::Kind::IpAddress:
        case dialstring::Kind::Host:
            port.printf(" host=%s port=%u", r.host, static_cast<unsigned>(r.port));
            break;
        case dialstring::Kind::Phonebook:
            port.printf(" number=%s", r.number);
            break;
        case dialstring::Kind::Special:
            port.printf(" number=%s special=%s", r.number, dialstring::specialName(r.special));
            break;
        case dialstring::Kind::Invalid:
        default:
            break;
    }
    port.newline();
    return r.kind == dialstring::Kind::Invalid ? atcmd::Reply::kError : atcmd::Reply::kOk;
}

atcmd::Reply cmdHw(atcmd::Port &port, const char *args) {
    if (callSlot.busy() && !ownsMenu(port) && !hwtest::allowedDuringCall(args)) {
        port.line(strncmp(args, "LED", 3) == 0 ? "LED patterns are not available during a call."
                                               : "Audio tests are not available during a call.");
        return atcmd::Reply::kError;
    }
    // Interactive tests already service their own Term. They must write prompts
    // to the initiating UART immediately rather than into a paged response.
    bool capture = ownsMenu(port) && menuOf(port).capturing();
    if (capture) {
        port.redirect(nullptr);
    }
    hardwareOwner = &port;
    port.beginInfo();
    hardwareRunning = true;
    browserTestCancelled = false;
    if (port.browser()) {
        webServer.text(browserClient,
                       !strcmp(args, "LINES")
                           ? "{\"t\":\"hardware\",\"active\":true,\"mode\":\"lines\"}"
                           : "{\"t\":\"hardware\",\"active\":true}");
    }
    // Prints its own OK / ERROR.
    withTerm(port, [args](hwconsole::Term &t) { hwconsole::handle(t, args); });
    hardwareRunning = false;
    if (port.browser() && browserTestCancelled) {
        hwconsole::restoreBrowserResources();
    }
    browserTestCancelled = false;
    if (port.browser()) {
        webServer.text(browserClient, "{\"t\":\"hardware\",\"active\":false}");
    }
    hardwareOwner = nullptr;
    if (capture) {
        port.redirect(&menuOf(port), true);
        menuOf(port).repaint();  // the test wrote over the screen
    }
    return atcmd::Reply::kDone;
}

atcmd::Reply cmdPhonebook(atcmd::Port &port, const char *args) {
    return phonebook.command(port, args, false);
}
atcmd::Reply cmdPhoneDelete(atcmd::Port &port, const char *args) {
    return phonebook.command(port, args, true);
}

atcmd::Reply cmdAnswerPort(atcmd::Port &port, const char *args) {
    modemcfg::AnswerPort choice;
    if (ownsMenu(port) && *args == '=' && modemcfg::parseAnswerPort(args + 1, &choice)) {
        return modem->setAnswerPort(choice) ? atcmd::Reply::kOk : atcmd::Reply::kError;
    }
    return incoming->command(port, args);
}

// AT*BAUD? shows the RS232 speed setting; AT*BAUD=AUTO detects it from "AT" (default);
// AT*BAUD=<300-115200> keeps the port at that speed. Saved at once.
atcmd::Reply cmdBaud(atcmd::Port &port, const char *args) {
    const char *a = hwtest::argStart(args);
    if (*a == '?' || *args == '\0') {
        port.beginInfo();
        if (modem->hostBaud() == 0) {
            port.printf("AUTO (from \"AT\"), now %u", static_cast<unsigned>(rs232::baud()));
        } else {
            port.printf("%u (fixed)", static_cast<unsigned>(modem->hostBaud()));
        }
        port.newline();
        return atcmd::Reply::kOk;
    }
    uint32_t baud = 0;
    if (!hwtest::wordIs(a, "AUTO") && !hwtest::parseUint(a, 300, 115200, &baud)) {
        return atcmd::Reply::kError;
    }
    if (callSlot.heldBy(&rs232::session()) && !isRs232(port)) {
        port.line("The RS232 port has a call: change its speed from the RS232 port.");
        return atcmd::Reply::kError;
    }
    if (!modem->setHostBaud(baud)) {
        return atcmd::Reply::kError;  // not a standard speed
    }
    port.result(atcmd::Result::kOk);  // at the old speed, before the port changes
    rs232::applyHostBaud();
    return atcmd::Reply::kDone;
}

// AT*TESTCALL[=<seconds>]: a call to a built-in echo, to try +++, ATO, ATH, DTR, DCD and
// flow control without a network. With <seconds>, the "far end" hangs up after that long.
atcmd::Reply cmdTestCall(atcmd::Port &port, const char *args) {
    const char *a = hwtest::argStart(args);
    uint32_t seconds = 0;
    if (*a != '\0' && !hwtest::parseUint(a, 1, 3600, &seconds)) {
        return atcmd::Reply::kError;
    }
    if (callSlot.busy()) {
        return atcmd::Reply::kError;  // one call at a time
    }
    echoCall.open(millis(), seconds * 1000);
    if (!sessionOf(port).startCall(echoCall)) {  // sends CONNECT
        echoCall.close();
        return atcmd::Reply::kError;
    }
    return atcmd::Reply::kDone;
}

const config::Registry registry(config::Hooks{cmdAnswerPort, cmdPhonebook, cmdPhoneDelete, cmdHelp,
                                              cmdVer, cmdBaud, cmdTestCall, cmdDialTest, cmdHw});

// The modem's state in one word, for the configuration screen and the network console.
const char *linkStatus() {
    return journal.active()                                               ? "Connected"
           : outgoing && outgoing->stage() != calls::CallStage::kIdle     ? "Dialling"
           : incoming && incoming->stage() != calls::IncomingStage::kIdle ? "Ringing"
                                                                          : "Idle";
}

// "US Robotics Sportster 14.4k" for the model AT*MODEL selected.
const char *selectedModel() {
    return modemcfg::productName(modem ? modem->model() : 0);
}

extern config::Commands configCommands;

class ConfigHardware : public config::Hardware, public config::UiServices {
public:
    calls::Phonebook *phonebook() override { return &::phonebook; }
    config::UiState state() const override {
        config::UiState result;
        result.hostBaud = rs232::baud();
        if (journal.active() && journal.session())
            result.reportedRate = journal.session()->reportedSpeed();
        snprintf(result.link, sizeof result.link, "%s", linkStatus());
        return result;
    }
    bool value(const char *id, char *out, size_t cap) const override {
        if (configCommands.value(id, out, cap))
            return true;
        const char *value = nullptr;
        if (!strcmp(id, "updates-reset.web-interface"))
            value = webSettings.enabled() ? "ON" : "OFF";
        else if (!strcmp(id, "updates-reset.network-console"))
            value = netConsoleSettings.enabled() ? "ON" : "OFF";
        if (!value)
            return false;
        snprintf(out, cap, "%s", value);
        return true;
    }
    // The project commands lib/config hands to the firmware (registry.cpp names them).
    atcmd::Reply command(atcmd::Port &port, const char *name, const char *args) override {
        static const Entry kTable[] = {
            {"INTERNETEXPORT", &ConfigHardware::cmdInternetExport},
            {"INTERNET", &ConfigHardware::cmdInternet},
            {"CLIENTEXPORT", &ConfigHardware::cmdClientExport},
            {"TEST", &ConfigHardware::cmdClientTest},
            {"SX", &ConfigHardware::cmdClientTest},
            {"SY", &ConfigHardware::cmdClientTest},
            {"SZ", &ConfigHardware::cmdClientTest},
            {"RX", &ConfigHardware::cmdClientTest},
            {"RY", &ConfigHardware::cmdClientTest},
            {"RZ", &ConfigHardware::cmdClientTest},
            {"WEBEXPORT", &ConfigHardware::cmdWebExport},
            {"WEB", &ConfigHardware::cmdWeb},
            {"NETCONSOLEEXPORT", &ConfigHardware::cmdNetConsoleExport},
            {"NETCONSOLE", &ConfigHardware::cmdNetConsole},
            {"APPLYBAUD", &ConfigHardware::cmdApplyBaud},
            {"CONFIG", &ConfigHardware::cmdConfig},
            {"STYLE", &ConfigHardware::cmdStyle},
            {"SIZE", &ConfigHardware::cmdSize},
            {"STATUS", &ConfigHardware::cmdStatus},
            {"SOUNDS", &ConfigHardware::cmdSounds},
            {"PLAY", &ConfigHardware::cmdPlay},
            {"DNS", &ConfigHardware::cmdProbe},
            {"PING", &ConfigHardware::cmdProbe},
            {"RESTART", &ConfigHardware::cmdRestart},
            {"RESETINFO", &ConfigHardware::cmdResetInfo},
        };
        for (const auto &entry : kTable) {
            if (!strcmp(name, entry.name)) {
                return (this->*entry.handler)(port, name, args);
            }
        }
        return atcmd::Reply::kError;
    }
    void poll() {
        probe_.poll(millis(), net.up());
        if (!pending_) {
            return;
        }
        if (!pending_->busy()) {
            stop();
            return;
        }
        if (operation_ == Operation::Sound) {
            callAudio.poll(millis(), modem->active());
            if (!callAudio.running() || millis() - started_ >= 60000) {
                bool failed = callAudio.failed();
                auto *port = pending_;
                stop();
                port->finish(failed ? atcmd::Reply::kError : atcmd::Reply::kOk);
            }
        } else if (operation_ == Operation::Probe && probe_.done()) {
            auto *port = pending_;
            uint32_t address = probe_.address();
            ip4_addr_t ip;
            ip.addr = address;
            port->printf("Address %s; replies %u\r\n", ip4addr_ntoa(&ip), probe_.replies());
            bool ok = probe_.ok();
            stop();
            port->finish(ok ? atcmd::Reply::kOk : atcmd::Reply::kError);
        } else if (operation_ == Operation::Restart && millis() - started_ >= 30000) {
            pending_->cancelPrompt();
            stop();
        }
    }

private:
    using Reply = atcmd::Reply;
    using Handler = Reply (ConfigHardware::*)(atcmd::Port &, const char *name, const char *args);
    struct Entry {
        const char *name;
        Handler handler;
    };

    static bool isQuery(const char *args) { return !*args || !strcmp(args, "?"); }

    Reply cmdInternetExport(atcmd::Port &port, const char *, const char *) {
        internetDialer->exportSettings(port);
        return Reply::kDone;
    }
    Reply cmdInternet(atcmd::Port &port, const char *, const char *args) {
        bool read = isQuery(args);
        bool hangup = !strcmp(args, "=HANGUP");
        if (!read && !hangup && callSlot.busy() && !ownsMenu(port))
            return Reply::kError;
        auto result = internetDialer->command(port, args);
        if (read)
            port.printf("Network adapter drops=%lu\r\n",
                        static_cast<unsigned long>(internetNetwork.drops()));
        return result;
    }
    Reply cmdClientExport(atcmd::Port &port, const char *, const char *) {
        for (unsigned i = 0; i < kSerialPorts; ++i) {
            const auto &s = clientSettings.get(i);
            port.printf(
                "; %s client: AT*TEST SET=%s,%u,%u,%u then AT*TEST APPLY (Y) on that port\r\n",
                clienttest::portName(i), clienttest::charsetName(s.charset), s.columns, s.rows,
                s.backspace);
        }
        return Reply::kOk;
    }
    // AT*TEST and the file transfers (AT*SX ... AT*RZ) on a local serial port.
    Reply cmdClientTest(atcmd::Port &port, const char *name, const char *args) {
        if (port.browser() || hardwareRunning || (callSlot.busy() && !ownsMenu(port)) ||
            net.joinResult() == netcfg::JoinResult::kRunning ||
            net.scanState() == netcfg::ScanState::kRunning)
            return Reply::kError;
        auto &service = *serialClient(serialIndex(port));
        auto &platform = serialPlatform(serialIndex(port));
        auto flow = hostport::flowMode(modem->active());
        platform.begin(port.baud(), flow.ctsFromBuffer, flow.obeyRts);
        service.hold(ownsMenu(port));
        if (!service.start(name, args, millis()))
            return Reply::kError;
        if (ownsMenu(port))
            menuOf(port).diagnostic(service);
        else if (!sessionOf(port).startLocal(service)) {
            service.close();
            return Reply::kError;
        }
        return Reply::kDone;
    }
    Reply cmdWebExport(atcmd::Port &port, const char *, const char *) {
        port.printf("AT*WEB=%s\r\n", webSettings.enabled() ? "ON" : "OFF");
        return Reply::kDone;
    }
    Reply cmdWeb(atcmd::Port &port, const char *, const char *args) {
        if (isQuery(args)) {
            port.beginInfo();
            port.line(webSettings.enabled() ? "ON" : "OFF");
            return Reply::kOk;
        }
        if (strcmp(args, "=ON") && strcmp(args, "=OFF")) {
            return Reply::kError;
        }
        return webSettings.setEnabled(!strcmp(args, "=ON")) ? Reply::kOk : Reply::kError;
    }
    Reply cmdNetConsoleExport(atcmd::Port &port, const char *, const char *) {
        port.printf("AT*NETCONSOLE=%s\r\n", netConsoleSettings.enabled() ? "ON" : "OFF");
        return Reply::kDone;
    }
    // AT*NETCONSOLE=OFF closes a connected client in the next loop pass, after its OK.
    Reply cmdNetConsole(atcmd::Port &port, const char *, const char *args) {
        if (isQuery(args)) {
            port.beginInfo();
            port.line(netConsoleSettings.enabled() ? "ON" : "OFF");
            return Reply::kOk;
        }
        if (strcmp(args, "=ON") && strcmp(args, "=OFF")) {
            return Reply::kError;
        }
        return netConsoleSettings.setEnabled(!strcmp(args, "=ON")) ? Reply::kOk : Reply::kError;
    }
    Reply cmdApplyBaud(atcmd::Port &, const char *, const char *) {
        rs232::applyHostBaud();
        return Reply::kDone;
    }
    Reply cmdConfig(atcmd::Port &port, const char *, const char *args) {
        if (*args || callSlot.busy() || port.busy()) {
            return Reply::kError;
        }
        auto &menu = menuOf(port);
        if (!menu.open()) {
            return Reply::kError;
        }
        if (!sessionOf(port).startLocal(menu)) {
            menu.close();
            return Reply::kError;
        }
        debugLog.event(millis(), config::Event::Menu);
        return Reply::kDone;
    }
    Reply cmdStyle(atcmd::Port &port, const char *, const char *args) {
        if (port.browser()) {
            if (!strcmp(args, "=UTF8")) {
                browserUtf8 = true;
                return Reply::kOk;
            }
            if (!strcmp(args, "=CP437")) {
                browserUtf8 = false;
                return Reply::kOk;
            }
        }
        if (isQuery(args)) {
            auto style = menuOf(port).style();
            port.beginInfo();
            port.line(style == config::Style::Plain ? "PLAIN" : "ANSI");
            if (port.browser())
                port.line(browserUtf8 ? "UTF8" : "CP437");
            return Reply::kOk;
        }
        return *args == '=' && menuOf(port).setStyle(args + 1) ? Reply::kOk : Reply::kError;
    }
    Reply cmdSize(atcmd::Port &port, const char *, const char *args) {
        auto &menu = menuOf(port);
        if (isQuery(args)) {
            port.beginInfo();
            port.printf("%u,%u\r\n", port.browser() ? browserSession->columns() : menu.columns(),
                        port.browser() ? browserSession->rows() : menu.rows());
            return Reply::kOk;
        }
        if (port.browser() && *args == '=') {
            std::string size(args + 1);
            size_t comma = size.find(',');
            unsigned columns, rows;
            if (comma == std::string::npos ||
                !webinterface::terminalSize("{\"t\":\"size\",\"cols\":" + size.substr(0, comma) +
                                                ",\"rows\":" + size.substr(comma + 1) + "}",
                                            columns, rows))
                return Reply::kError;
            browserSession->resize(columns, rows);
            menu.resize(columns, rows);
            return Reply::kOk;
        }
        return *args == '=' && menu.setSize(args + 1) ? Reply::kOk : Reply::kError;
    }
    Reply cmdStatus(atcmd::Port &port, const char *, const char *args) {
        if (!isQuery(args)) {
            return Reply::kError;
        }
        debugLog.event(millis(), config::Event::StatusRead);
        port.beginInfo();
        port.printf("%s\r\nPanel AA CD RD SD TR CS ARQ: ", modemcfg::modelName(modem->model()));
        for (unsigned i = 0; i < 7; ++i) {
            port.printf("%u ", (ledMask >> i) & 1);
        }
        port.newline();
        port.printf("Console: 115200; RS232: %lu; USB: %s; S0=%u; auto-answer=%s\r\n",
                    static_cast<unsigned long>(rs232::baud()),
                    !usbport::started()    ? "off"
                    : usbport::dtrOn()     ? "open (DTR on)"
                    : usbport::connected() ? "connected, closed"
                                           : "not connected",
                    modem->active().s[0], modemcfg::answerPortName(modem->answerPort()));
        port.printf("Outgoing stage=%u; incoming stage=%u; configuration=%s\r\n",
                    unsigned(outgoing->stage()), unsigned(incoming->stage()),
                    consoleMenu->isOpen()                  ? "CONSOLE"
                    : hostMenu->isOpen()                   ? "RS232"
                    : usbMenu->isOpen()                    ? "USB"
                    : browserMenu && browserMenu->isOpen() ? "BROWSER"
                                                           : "off");
        if (journal.active()) {
            const auto &r = journal.current();
            auto *session = journal.session();
            port.printf("Call %s %s %s duration %lus, speed %lu TX=%lu RX=%lu\r\n",
                        r.incoming ? "incoming" : "outgoing", calls::kindName(r.kind), r.host,
                        static_cast<unsigned long>(journal.duration()),
                        session ? static_cast<unsigned long>(session->reportedSpeed()) : 0,
                        session ? static_cast<unsigned long>(session->payloadSent()) : 0,
                        session ? static_cast<unsigned long>(session->payloadReceived()) : 0);
        }
        char ip[16];
        netcfg::formatIp(net.link().ip, ip, sizeof ip);
        port.printf("Wi-Fi: %s, %s, %d dBm, IP %s\r\n", net.stateText(), net.settings().ssid,
                    net.link().rssi, ip);
        switch (netConsole.state()) {
            case netconsole::Console::State::kOff:
                port.printf("Network console: %s\r\n",
                            netConsoleSettings.enabled() ? "on, not listening" : "off");
                break;
            case netconsole::Console::State::kListening:
                port.printf("Network console: port %u, no client\r\n", netconsole::kPort);
                break;
            case netconsole::Console::State::kLogin:
                port.printf("Network console: port %u, %s at the password prompt\r\n",
                            netconsole::kPort, netConsole.waiting());
                break;
            case netconsole::Console::State::kReady:
                port.printf("Network console: port %u, %s logged in%s%s%s\r\n", netconsole::kPort,
                            netConsole.peer(), *netConsole.waiting() ? "; " : "",
                            netConsole.waiting(),
                            *netConsole.waiting() ? " at the password prompt" : "");
                break;
        }
        // The least free stack so far, for the bench record.
        port.printf("Stack headroom: loop %d B, lwIP %d B, web %d B\r\n",
                    hw::stackHeadroom("loopTask"), hw::stackHeadroom("tcpip"),
                    hw::stackHeadroom("httpd"));
        port.printf("Flow &H%u &R%u; DTR &D%u; DCD &C%u; serial &B%u\r\n",
                    modem->active().get(modemcfg::kAmpH), modem->active().get(modemcfg::kAmpR),
                    modem->active().get(modemcfg::kAmpD), modem->active().get(modemcfg::kAmpC),
                    modem->active().get(modemcfg::kAmpB));
        return Reply::kOk;
    }
    Reply cmdSounds(atcmd::Port &port, const char *, const char *args) {
        if (!isQuery(args)) {
            return Reply::kError;
        }
        port.beginInfo();
        port.line("Generated: DIAL BUSY RING DTMF PULSE SWEEP");
        File dir = LittleFS.open("/sounds");
        File file = dir.openNextFile();
        while (file) {
            if (!file.isDirectory()) {
                port.printf("/sounds/%s (%u bytes)\r\n", file.name(), unsigned(file.size()));
            }
            file = dir.openNextFile();
        }
        port.line("Handshake recordings: Gough Lui's modem sound collection (see About).");
        return Reply::kOk;
    }
    Reply cmdPlay(atcmd::Port &port, const char *, const char *args) {
        if (*args != '=' || pending_ || (callSlot.busy() && !ownsMenu(port)) ||
            !callAudio.preview(args + 1, modem->active())) {
            return Reply::kError;
        }
        pending_ = &port;
        operation_ = Operation::Sound;
        started_ = millis();
        debugLog.event(millis(), config::Event::SoundPreview);
        port.line("Playing; any key cancels.");
        port.wait(cancel, this);
        return Reply::kPending;
    }
    // AT*DNS=<name> and AT*PING=<name>.
    Reply cmdProbe(atcmd::Port &port, const char *name, const char *args) {
        bool ping = !strcmp(name, "PING");
        if ((ping && internetDialer->active()) || *args != '=' || pending_ || !net.up() ||
            !netcfg::validServer(args + 1) || !probe_.start(args + 1, ping, millis())) {
            return Reply::kError;
        }
        pending_ = &port;
        operation_ = Operation::Probe;
        debugLog.event(millis(), config::Event::NetworkProbe);
        port.line("Network test; any key cancels.");
        port.wait(cancel, this);
        return Reply::kPending;
    }
    Reply cmdRestart(atcmd::Port &port, const char *, const char *args) {
        if (*args || pending_ || !deviceControl.idle()) {
            return Reply::kError;
        }
        pending_ = &port;
        operation_ = Operation::Restart;
        started_ = millis();
        port.askConfirmation("Restart modem? Y/N (Enter): ", confirm, this);
        return Reply::kPending;
    }
    Reply cmdResetInfo(atcmd::Port &port, const char *, const char *args) {
        if (!isQuery(args)) {
            return Reply::kError;
        }
        uint8_t record[4] = {};
        size_t n = resetStore.load(record, sizeof record);
        port.beginInfo();
        port.printf("Current reset: %s\r\n", hwconsole::resetReasonName(int(esp_reset_reason())));
        if (n == 3 && record[0] == 1) {
            port.printf("Previous reset: %s\r\n", hwconsole::resetReasonName(record[2]));
        }
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH  // always ELF in IDF 6
        esp_core_dump_summary_t summary = {};
        if (esp_core_dump_get_summary(&summary) == ESP_OK) {
            port.printf("Crash record: PC=0x%08lx, cause=%lu, backtrace depth=%lu\r\n",
                        static_cast<unsigned long>(summary.exc_pc),
                        static_cast<unsigned long>(summary.ex_info.exc_cause),
                        static_cast<unsigned long>(summary.exc_bt_info.depth));
        } else {
            port.line("Crash record: none available.");
        }
#else
        port.line("Crash record: unavailable in this build.");
#endif
        return Reply::kOk;
    }
    enum class Operation { Sound, Probe, Restart } operation_ = Operation::Sound;
    static void cancel(void *self) { static_cast<ConfigHardware *>(self)->stop(); }
    void stop() {
        if (operation_ == Operation::Sound) {
            callAudio.stop();
        }
        if (operation_ == Operation::Probe) {
            probe_.cancel();
        }
        pending_ = nullptr;
    }
    static atcmd::Reply confirm(atcmd::Port &, const char *text, void *ctx) {
        auto &self = *static_cast<ConfigHardware *>(ctx);
        bool ok = (!strcmp(text, "Y") || !strcmp(text, "y")) && millis() - self.started_ < 30000 &&
                  deviceControl.idle();
        self.pending_ = nullptr;
        if (ok) {
            deviceControl.restart();
        }
        return ok ? atcmd::Reply::kOk : atcmd::Reply::kError;
    }
    hw::NetProbe probe_;
    atcmd::Port *pending_ = nullptr;
    uint32_t started_ = 0;
};
ConfigHardware configHardware;
config::Commands configCommands(debugLog, history, phonebook, net, device, configHardware);

uint32_t msNow() {
    return millis();
}

// --- Boot banner ------------------------------------------------------------------------

void printSettings(modemcfg::LoadResult loaded) {
    const modemcfg::Profile &p = modem->active();
    consoleout::printf("Settings: %s, power-on template Y%u\r\n", modemcfg::loadResultText(loaded),
                       static_cast<unsigned>(modem->nvram().y));
    char speed[48];
    if (modem->hostBaud() == 0) {
        snprintf(speed, sizeof speed, "from \"AT\" (starts at %u)",
                 static_cast<unsigned>(rs232::baud()));
    } else {
        snprintf(speed, sizeof speed, "fixed %u (AT*BAUD)",
                 static_cast<unsigned>(modem->hostBaud()));
    }
    consoleout::printf("RS232: speed %s, &B%u, flow control &H%u&R%u, &C%u &D%u\r\n", speed,
                       static_cast<unsigned>(p.get(modemcfg::kAmpB)),
                       static_cast<unsigned>(p.get(modemcfg::kAmpH)),
                       static_cast<unsigned>(p.get(modemcfg::kAmpR)),
                       static_cast<unsigned>(p.get(modemcfg::kAmpC)),
                       static_cast<unsigned>(p.get(modemcfg::kAmpD)));
    consoleout::printf("USB modem port: %s; auto-answer %s\r\n",
                       usbport::started() ? "on (native USB / rear USB-C)" : "failed to start",
                       modemcfg::answerPortName(modem->answerPort()));
    consoleout::printf(
        "Echo E%u, quiet Q%u, verbal V%u, results X%u\r\n",
        static_cast<unsigned>(p.get(modemcfg::kE)), static_cast<unsigned>(p.get(modemcfg::kQ)),
        static_cast<unsigned>(p.get(modemcfg::kV)), static_cast<unsigned>(p.get(modemcfg::kX)));
}

// Wi-Fi, network name and clock (spec 3.7). Never the Wi-Fi password.
void printNetwork(netcfg::LoadResult netLoaded) {
    const netcfg::Settings &s = net.settings();
    if (netLoaded == netcfg::LoadResult::kDamaged) {
        consoleout::printf("Wi-Fi: %s\r\n", netcfg::loadResultText(netLoaded));
    }
    if (!s.enabled) {
        consoleout::print("Wi-Fi: off (AT*WIFI=ON switches it on)\r\n");
    } else if (!s.hasNetwork()) {
        consoleout::print("Wi-Fi: not set up (AT*WIFISCAN, then AT*WIFIJOIN=<n>)\r\n");
    } else if (net.up()) {
        const netcfg::LinkInfo &l = net.link();
        char ip[16];
        netcfg::formatIp(l.ip, ip, sizeof ip);
        consoleout::printf("Wi-Fi: %s, %d dBm, IP %s (%s)\r\n", s.ssid, static_cast<int>(l.rssi),
                           ip, s.dhcp ? "DHCP" : "static");
    } else {
        consoleout::printf("Wi-Fi: %s, %s\r\n", s.ssid, net.stateText());
    }
    consoleout::printf("Name: %s.local\r\n", s.host);
    const int64_t utc = net.utcNow();
    if (utc != 0) {
        char t[48];
        netcfg::tz::formatLocal(net.zone(), utc, t, sizeof t);
        consoleout::printf("Clock: %s\r\n", t);
    } else {
        consoleout::printf("Clock: not set yet (NTP %s, zone %s)\r\n", s.ntp, s.tz);
    }
}

void printBanner(modemcfg::LoadResult loaded, netcfg::LoadResult netLoaded) {
    consoleout::print("\r\n");
    consoleout::print("  _   _ ____  ____    __  __           _\r\n");
    consoleout::print(" | | | / ___||  _ \\  |  \\/  | ___   __| | ___ _ __ ___\r\n");
    consoleout::print(" | | | \\___ \\| |_) | | |\\/| |/ _ \\ / _` |/ _ \\ '_ ` _ \\\r\n");
    consoleout::print(" | |_| |___) |  _ <  | |  | | (_) | (_| |  __/ | | | | |\r\n");
    consoleout::print("  \\___/|____/|_| \\_\\ |_|  |_|\\___/ \\__,_|\\___|_| |_| |_|\r\n");
    consoleout::printf("\r\nUSR Modem Emulator v%s (%s)\r\n", FW_VERSION, FW_COMMIT);
    consoleout::printf("ESP32-S3, flash %u MB, PSRAM %u KB, reset: %s\r\n",
                       static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                       static_cast<unsigned>(ESP.getPsramSize() / 1024),
                       hwconsole::resetReasonName(static_cast<int>(esp_reset_reason())));
    printNetwork(netLoaded);
    printSettings(loaded);
    consoleout::printf("OTA: %s, %s\r\n",
                       device.policy() == devicecfg::Policy::kAlways ? "ALWAYS" : "DEMAND",
                       device.wantsListener(millis(), net.up(), true) ? "listening" : "closed");
    if (!device.ready()) {
        consoleout::print("Device credentials unavailable. Use AT*PASS or AT*FACTORY.\r\n");
    } else if (*device.bootPassword() != '\0') {
        consoleout::print("Device login: admin\r\nDevice password: ");
        consoleout::print(device.bootPassword());
        consoleout::print("\r\nShown at boot until first use or change. AT*PASS replaces it.\r\n");
    }
    consoleout::print(
        "Maintenance console. ATI4 shows the settings, AT*HELP the project commands,\r\n"
        "AT*HWTEST tests the hardware.\r\n");
}

// --- Main loop parts --------------------------------------------------------------------

void pollConsole(uint32_t now) {
    consoleInput.poll(*console, now);
    uint8_t buf[256];
    size_t room = consoleInput.size() ? 0 : console->hostSpace();
    size_t n = 0;
    while (n < sizeof buf && n < room && Serial.available() > 0) {
        buf[n++] = static_cast<uint8_t>(Serial.read());
    }
    if (n < sizeof buf && n < room) {
        n += netConsole.read(buf + n, std::min(sizeof buf, room) - n);
    }
    if (n > 0) {
        consoleInput.append(buf, n);
        consoleInput.poll(*console, now);
        devicecfg::wipe(buf, n);  // input can contain passwords
    }
    int space = Serial.availableForWrite();
    if (space > 0) {
        size_t cap =
            static_cast<size_t>(space) < sizeof buf ? static_cast<size_t>(space) : sizeof buf;
        size_t out = console->toHost(buf, cap);
        if (out > 0) {
            consoleout::write(buf, out);
        }
    }
    console->tick(now, true);  // the console has no DTR
}

// The network console listens while it is on, Wi-Fi is up and the device password is set.
void pollNetConsole() {
    netConsole.poll(netConsoleSettings.enabled() && device.ready(), net.up() ? net.link().ip : 0,
                    millis());
}

// Wi-Fi and clock notices go to the console only, between commands: in command mode,
// nothing typed, no join or scan waiting. Never to the RS232 port.
void pollNotices() {
    if (console->localOpen() || console->mode() != hostport::Mode::kCommand ||
        !console->port().lineEmpty()) {
        return;
    }
    char text[netcfg::Net::kNoticeMax];
    if (net.takeNotice(text, sizeof text)) {
        consoleout::printf("%s\r\n", text);
    }
}

hostport::WifiLed arqLed() {
    switch (net.arq()) {
        case netcfg::Arq::kOn:
            return hostport::WifiLed::kConnected;
        case netcfg::Arq::kBlink:
            return hostport::WifiLed::kConnecting;
        case netcfg::Arq::kOff:
        default:
            return hostport::WifiLed::kOff;
    }
}

void pollLeds(uint32_t now) {
    if (hwconsole::ledsHeld()) {
        ledMask = 0xFF;  // set again when the test gives them back
        return;
    }
    const modemcfg::Profile &p = modem->active();
    hostport::PanelInput in = {};
    in.autoAnswer = p.s[0] >= 1;
    in.ringing = incoming && incoming->ringing();
    in.cd = console->callUp();
    in.tr = p.get(modemcfg::kAmpD) == 0;
    in.cs = hw::ctsOutAsserted();  // RS232 only: USB has no CTS
    in.wifi = arqLed();
    // RS232 and the USB modem port both drive CD, TR, RD and SD.
    hostport::addHostPort(in, {!rs232::suspended() && rs232::session().dcd(), rs232::dtrOn(),
                               rs232::bytesToComputer(), rs232::bytesFromComputer()});
    hostport::addHostPort(in, {usbport::session().dcd(), usbport::dtrOn(),
                               usbport::bytesToComputer(), usbport::bytesFromComputer()});
    hostport::addHostPort(
        in, {browserSession->callUp(), browserClient != 0, browserToComputer, browserFromComputer});
    uint8_t mask = panel.update(in, now);
    if (mask != ledMask || mask != hw::ledsMask()) {
        hw::ledsSet(mask);
        ledMask = mask;
    }
}

void browserAccess(uint32_t id, bool edit) {
    webServer.terminal(id, edit);
}
void browserWaiting(uint32_t id) {
    browserAccess(id, false);
}
// --- Web page status messages (web/PROTOCOL.md) ---------------------------------------
// Each one is rebuilt on its own interval or when it changes; `force` rebuilds it now.

void publishLeds(uint32_t now, bool force) {
    static uint32_t at = 0;
    if (force || now - at >= 50) {
        at = now;
        webServer.snapshot(0, "{\"t\":\"led\",\"m\":" + std::to_string(hw::ledsMask()) + "}");
    }
}

void publishClock(uint32_t now, bool force) {
    static uint32_t at = 0;
    static int lastOffset = -9999;
    static bool clockSet = false;
    static netcfg::tz::OffsetCache offsets;  // offsetAt() is slow for every pass
    int64_t epoch = net.utcNow();
    int offset = epoch > 0 ? offsets.at(net.zone(), epoch) / 60 : 0;
    if (force || now - at >= 60000 || clockSet != (epoch > 0) || offset != lastOffset) {
        at = now;
        clockSet = epoch > 0;
        lastOffset = offset;
        webServer.snapshot(1,
                           "{\"t\":\"clock\",\"epoch\":" + std::to_string(epoch) +
                               ",\"tzo\":" + std::to_string(offset) + "}",
                           true);
    }
}

void publishWifi(uint32_t now, bool force) {
    static uint32_t at = 0;
    static netcfg::LinkState lastState = netcfg::LinkState::kOff;
    static uint32_t lastIp = 0;
    if (!force && now - at < 10000 && net.link().state == lastState && net.link().ip == lastIp) {
        return;
    }
    at = now;
    lastState = net.link().state;
    lastIp = net.link().ip;
    char ip[16];
    netcfg::formatIp(net.link().ip, ip, sizeof ip);
    const char *state = net.up()                                             ? "up"
                        : net.link().state == netcfg::LinkState::kConnecting ? "connecting"
                                                                             : "down";
    webServer.snapshot(2,
                       std::string("{\"t\":\"wifi\",\"state\":\"") + state +
                           "\",\"ssid\":" + webinterface::jsonString(net.settings().ssid) +
                           ",\"rssi\":" + std::to_string(net.link().rssi) +
                           ",\"ip\":" + webinterface::jsonString(ip) + "}",
                       true);
}

const char *webCallState() {
    const char *state = "idle";
    if ((consoleClient && consoleClient->isOpen()) || (hostClient && hostClient->isOpen()) ||
        (usbClient && usbClient->isOpen())) {
        state = "test";
    } else if (internetDialer && internetDialer->active()) {
        state = !internetDialer->connected()                                   ? "dialling"
                : internetDialer->engine().protocol() == dialup::Protocol::Ppp ? "ppp"
                                                                               : "slip";
    } else if (journal.active()) {
        state = journal.session()            ? "connected"
                : journal.current().incoming ? "ringing"
                                             : "dialling";
    } else if (console->callUp() || rs232::session().callUp() || usbport::session().callUp() ||
               browserSession->callUp()) {
        state = "test";
    } else if (consoleMenu->isOpen() || hostMenu->isOpen() || usbMenu->isOpen() ||
               browserMenu->isOpen()) {
        state = "config";
    }
    return state;
}

// The call summary is rebuilt at most ten times a second, not on every loop pass.
void publishCall(uint32_t now, bool force) {
    static uint32_t at = 0;
    if (!force && now - at < 100) {
        return;
    }
    at = now;
    std::string call = std::string("{\"t\":\"call\",\"state\":\"") + webCallState() +
                       "\",\"model\":" + webinterface::jsonString(selectedModel());
    if (journal.active()) {
        const auto &r = journal.current();
        const bool internet = r.kind == calls::Kind::Ppp || r.kind == calls::Kind::Slip;
        call += std::string(",\"mode\":\"") +
                (internet                     ? (r.kind == calls::Kind::Ppp ? "ppp" : "slip")
                 : r.kind == calls::Kind::Raw ? "raw"
                                              : "telnet") +
                "\",\"dir\":\"" + (r.incoming ? "in" : "out") +
                "\",\"number\":" + webinterface::jsonString(r.number) +
                ",\"host\":" + webinterface::jsonString(r.host) + ",\"ip\":" +
                webinterface::jsonString(internet     ? ""
                                         : r.incoming ? incomingSocket.peer()
                                                      : tcpSocket.peer()) +
                (internet ? std::string() : ",\"port\":" + std::to_string(journal.port())) +
                ",\"since\":" + std::to_string(journal.connectedUtc()) + ",\"speed\":" +
                std::to_string(journal.session() ? journal.session()->reportedSpeed() : 0);
        if (internet) {
            char local[16], peer[16];
            netcfg::formatIp(internetSettings.get().local, local, sizeof local);
            netcfg::formatIp(internetSettings.get().peer, peer, sizeof peer);
            call += std::string(",\"ready\":") +
                    (internetDialer->engine().ready() ? "true" : "false") +
                    ",\"local\":" + webinterface::jsonString(local) +
                    ",\"peer\":" + webinterface::jsonString(peer);
        }
    }
    call += std::string(",\"browser\":") +
            (journal.active() &&
                     (journal.session() == browserSession || callSlot.heldBy(browserSession))
                 ? "true"
                 : "false");
    webServer.snapshot(3, call + "}");
}

// Nothing is built while no browser is connected. A browser that arrives
// after none gets all four messages rebuilt in the same pass.
void publishWebStatus() {
    static uint32_t readyClient = 0, readyBytes = 0;
    static bool wasReady = false;
    if (browserClient) {
        bool ready = !hardwareRunning && !browserSession->callUp() && !browserMenu->isOpen() &&
                     !browserSession->port().busy() && browserSession->port().lineEmpty() &&
                     !browserInput.size() &&
                     (!callSlot.busy() || callSlot.heldBy(browserSession)) &&
                     net.joinResult() != netcfg::JoinResult::kRunning &&
                     net.scanState() != netcfg::ScanState::kRunning;
        if (readyClient != browserClient || wasReady != ready ||
            readyBytes != browserCommandBytes) {
            webServer.commandState(
                browserClient,
                std::string("{\"t\":\"ready\",\"idle\":") + (ready ? "true" : "false") +
                    ",\"rx\":" + std::to_string(browserCommandBytes) +
                    ",\"errors\":" + std::to_string(browserSession->port().errorCount()) + "}");
            readyClient = browserClient;
            readyBytes = browserCommandBytes;
            wasReady = ready;
        }
    } else {
        readyClient = 0;
    }
    static bool hadBrowser = false;
    bool browser = false;
    for (unsigned i = 0; i < hw::WebService::kClients && !browser; ++i) {
        browser = webServer.clientId(i) != 0;
    }
    bool force = browser && !hadBrowser;
    hadBrowser = browser;
    if (!browser) {
        return;
    }
    uint32_t now = millis();
    publishLeds(now, force);
    publishClock(now, force);
    publishWifi(now, force);
    publishCall(now, force);
}
// --- Browser terminal ---------------------------------------------------------------------

// A finished image upload restarts the modem; a failed file-system upload mounts it again.
void finishUpload() {
    bool ok, fs;
    if (!webServer.takeUploadDone(ok, fs)) {
        return;
    }
    webServer.discardInput(browserClient);
    callSlot.release(&webServer);
    if (ok) {
        deviceControl.restart();
    } else if (fs) {
        LittleFS.begin(false);
    }
}

// Only the browser with the configuration screen may upload, and only while no port has a
// half-typed command and no Wi-Fi join or scan runs. The upload holds the call slot.
void answerUploadRequest() {
    uint32_t uploadClient;
    if (!webServer.takeUploadRequest(uploadClient)) {
        return;
    }
    bool allow = !hardwareRunning && browserClient == uploadClient && !browserSession->callUp() &&
                 !browserSession->port().busy() && browserSession->port().lineEmpty() &&
                 !browserInput.size() && (!callSlot.busy() || callSlot.heldBy(browserSession)) &&
                 console->port().lineEmpty() && rs232::session().port().lineEmpty() &&
                 usbport::session().port().lineEmpty() &&
                 net.joinResult() != netcfg::JoinResult::kRunning &&
                 net.scanState() != netcfg::ScanState::kRunning;
    if (allow) {
        webServer.discardInput(browserClient);
        browserSession->hangUp();
        browserInput.clear();
        hwconsole::restoreBrowserResources();
        browserAccess(browserClient, false);
        browserClient = 0;
    }
    allow = allow && callSlot.claim(&webServer);
    if (allow) {
        callAudio.stop();
        ota.poll(millis(), net.up(), false, net.settings().host, net.link().ip);
    }
    // The server may have stopped waiting (2 s) while we checked: then allowUpload()
    // changes nothing and we release the claim, so no upload is left half-started.
    // ArduinoOTA resumes on the next ota.poll(); the browser keeps its
    // read-only view and can open the terminal again.
    if (!webServer.allowUpload(allow) && allow) {
        callSlot.release(&webServer);
    }
}

// The terminal belongs to its browser until that socket closes, including command mode.
void releaseConfigBrowser() {
    if (!browserClient || webServer.connected(browserClient))
        return;
    netcfg::detachCommands(browserSession->port());
    browserInput.clear();
    browserSession->port().discardInput();
    browserSession->hangUp();
    hwconsole::restoreBrowserResources();
    browserClient = 0;
}

// A status tab claims nothing; opening the terminal is a separate control message.
void serveReadOnlyBrowser(uint32_t id, bool fresh) {
    if (fresh)
        browserWaiting(id);
    if (!webServer.takeOpen(id))
        return;
    if (browserClient || callSlot.busy() || hardwareRunning) {
        browserAccess(id, id == browserClient);
        return;
    }
    browserInput.clear();
    browserUtf8 = false;
    browserSession->resize(80, 25);
    browserClient = id;
    browserCommandBytes = 0;
    browserAccess(id, true);
    // Probe the fresh parser through its normal input path. These internal bytes are
    // not browser input and do not advance the command-channel acknowledgement.
    static const uint8_t probe[] = {'A', 'T', '\r'};
    browserSession->fromHost(probe, sizeof probe, millis());
}

// Menu departures run on the same terminal, including phonebook dial actions.
void runBrowserDeparture() {
    char departure[atcmd::kLineMax + 1];
    if (browserMenu->takeDeparture(departure, sizeof departure)) {
        browserSession->tick(millis(), true);
        hwconsole::restoreBrowserResources();
        browserSession->port().execute(departure);
    }
    devicecfg::wipe(departure, sizeof departure);
}

void serveConfigBrowser(uint32_t id) {
    uint32_t now = millis();
    if (webServer.takeHangup(id)) {
        webServer.discardInput(id);
        browserCommandBytes = 0;
        browserInput.clear();
        browserSession->port().discardInput();
        if (browserSession->callUp())
            browserSession->hangUp();
    }
    bool menuWasOpen = browserMenu->isOpen();
    unsigned columns, rows;
    if (webServer.takeSize(id, columns, rows)) {
        browserSession->resize(columns, rows);
        browserMenu->resize(columns, rows);
    }
    browserSession->tick(now, true);
    browserInput.poll(*browserSession, now, true);
    uint8_t bytes[256];
    if (!browserInput.size()) {
        size_t room = browserSession->hostSpace();
        size_t n = webServer.read(id, bytes, room < sizeof bytes ? room : sizeof bytes);
        browserFromComputer += n;
        browserCommandBytes += n;
        browserInput.append(bytes, n);
        browserInput.poll(*browserSession, now, true);
    }
    browserSession->tick(millis(), true);
    // CP437 glyphs expand to at most three UTF-8 bytes. Leave undeliverable data
    // in the Session so backpressure reaches the TCP peer.
    size_t room = webServer.space(id) / 3;
    size_t n = browserSession->toHost(bytes, room < sizeof bytes ? room : sizeof bytes);
    if (n) {
        bool bbs =
            browserSession->callUp() && !browserSession->binaryLocal() && !browserMenu->isOpen();
        std::string output = bbs && browserUtf8
                                 ? std::string(reinterpret_cast<const char *>(bytes), n)
                                 : webinterface::terminalUtf8(bytes, n, bbs);
        if (webServer.write(id, reinterpret_cast<const uint8_t *>(output.data()), output.size()))
            browserToComputer += output.size();
    }
    runBrowserDeparture();
    if (menuWasOpen && !browserMenu->isOpen())
        hwconsole::restoreBrowserResources();
    devicecfg::wipe(bytes, sizeof bytes);
}

void pollBrowser() {
    char ip[16];
    netcfg::formatIp(net.link().ip, ip, sizeof ip);
    finishUpload();
    webServer.poll(webSettings.enabled(), net.up(), net.settings().host, ip);
    if (webServer.takeUnmountRequest()) {
        callAudio.stop();
        LittleFS.end();
        webServer.unmounted();
    }
    answerUploadRequest();
    if (webServer.uploading()) {
        return;
    }
    releaseConfigBrowser();
    static uint32_t seen[hw::WebService::kClients] = {};
    for (unsigned i = 0; i < hw::WebService::kClients; ++i) {
        uint32_t id = webServer.clientId(i);
        bool fresh = id && seen[i] != id;
        seen[i] = id;
        if (!id) {
            continue;
        }
        if (id == browserClient) {
            serveConfigBrowser(id);
        } else {
            serveReadOnlyBrowser(id, fresh);
        }
    }
    publishWebStatus();
    webServer.pump();
}

// --- Start-up steps ---------------------------------------------------------------------

// The four local sessions and their configuration screens (in PSRAM when there is room).
void createPorts() {
    console = new hostport::Session(*modem, consoleSink, kConsoleBaud, callSlot);
    rs232::begin(*modem, callSlot);
    usbport::begin(*modem, callSlot);
    auto makeMenu = [](hostport::Session &session, bool rs232Port) {
        void *memory = ps_malloc(sizeof(config::Terminal));
        return memory ? new (memory) config::Terminal(session, rs232Port)
                      : new config::Terminal(session, rs232Port);
    };
    consoleMenu = makeMenu(*console, false);
    hostMenu = makeMenu(rs232::session(), true);
    usbMenu = makeMenu(usbport::session(), false);
    browserSession = new hostport::Session(*modem, browserSink, kConsoleBaud, callSlot);
    browserSession->port().setBrowser(true);
    browserMenu = makeMenu(*browserSession, false);
    consoleMenu->services(&configHardware);
    hostMenu->services(&configHardware);
    usbMenu->services(&configHardware);
    browserMenu->services(&configHardware);
    browserMenu->setStyle("ANSI");
    browserMenu->color(config::Color::Rgb);
    consoleClient =
        new clienttest::Service(clientSettings, clienttest::kConsolePort, consoleClientPlatform);
    hostClient =
        new clienttest::Service(clientSettings, clienttest::kRs232Port, hostClientPlatform);
    usbClient = new clienttest::Service(clientSettings, clienttest::kUsbPort, usbClientPlatform);
    for (unsigned i = 0; i < kSerialPorts; ++i) {
        applyClientSettings(i);
    }
}

// The debug log, the call history and this boot's reset reason (AT*RESETINFO).
void startDiagnostics() {
    char *logStorage = static_cast<char *>(ps_malloc(256 * 1024));
    debugLog.begin(logStorage ? logStorage : fallbackLog,
                   logStorage ? 256 * 1024 : sizeof fallbackLog);
    debugLog.event(millis(), config::Event::Boot);
    if (!history.begin()) {
        debugLog.event(millis(), config::Event::StorageError);
    }
    uint8_t previous[4] = {}, record[3] = {1, static_cast<uint8_t>(esp_reset_reason()), 0};
    if (resetStore.load(previous, sizeof previous) == 3 && previous[0] == 1) {
        record[2] = previous[1];
    }
    resetStore.save(record, sizeof record);
}

// Outgoing, Internet dial-up and incoming calls, connected to every serial session.
void createCallServices() {
    outgoing =
        new calls::Outgoing(*modem, phonebook, callSlot, tcpSocket, callAudio, msNow, networkUp);
    outgoing->setObserver(&journal);
    configDialer = new config::Dialer(*console, *consoleMenu, *hostMenu, *outgoing, callSlot,
                                      callAudio, msNow);
    configDialer->setUsb(&usbport::session(), usbMenu);
    configDialer->setBrowser(browserSession, browserMenu);
    browserSession->setDialer(configDialer);
    internetDialer = new dialup::Dialer(*modem, internetSettings, net, callSlot, internetNetwork,
                                        callAudio, msNow, *configDialer);
    internetDialer->setObserver(&journal);
    console->setDialer(internetDialer);
    rs232::session().setDialer(internetDialer);
    usbport::session().setDialer(internetDialer);
    incoming = new calls::Incoming(*modem, callSlot, incomingListener, incomingSocket, callAudio,
                                   *console, rs232::session(), msNow, networkUp);
    console->setAnswerer(incoming);
    incoming->setObserver(&journal);
    rs232::session().setAnswerer(incoming);
    incoming->setUsb(&usbport::session());
    usbport::session().setAnswerer(incoming);
}

// With a saved network, give it a few seconds so the banner can show the address.
void waitForNetwork() {
    const netcfg::Settings &s = net.settings();
    const uint32_t start = millis();
    while (s.enabled && s.hasNetwork() && !net.up() && millis() - start < kBannerWaitMs) {
        const uint32_t now = millis();
        net.tick(now);
        pollLeds(now);
        delay(10);
    }
    net.tick(millis());
}

}  // namespace

void setup() {
    (void)imageMarker[0];  // retain project identity in the linked application image
    // Console replies go into the UART driver's buffer and out at 115200 while the loop
    // runs on, instead of holding it (AT*REFERENCE, 5.5 KB, used to hold it for 0.5 s). The
    // largest reply, AT*ABOUT, is 9.4 KB. Only more output than this at once (several long
    // replies back to back) waits for the cable, for the rest. The UART interrupt empties
    // it, so output still gets out if the loop hangs. Internal RAM (UART driver).
    Serial.setTxBufferSize(kConsoleTxBuffer);
    Serial.begin(kConsoleBaud);
    consoleout::begin();  // before anything is printed: the network console replays it
    // One network console client at a time: one that vanished frees the port in about 16 s.
    for (auto &socket : netConsoleSockets) {
        socket.setKeepAlive(hw::TcpSocket::KeepAlive{10, 2, 3});
    }
    // No ESP-IDF driver messages on the console. A long stream on the RS232 port in
    // command mode overflows the speed capture, and the RMT driver would print an error
    // from its interrupt for every chunk it drops (switchable with AT*LOG).
    esp_log_level_set("*", ESP_LOG_NONE);
    delay(500);
    LittleFS.begin(false);  // mount before recovering a reset which erases history
    crypto.begin();
    device.begin();
    webSettings.begin();
    netConsoleSettings.begin();
    clientSettings.begin();
    internetSettings.begin();
    if (device.needsReset()) {
        if (device.factoryReset(resetStores, sizeof resetStores / sizeof resetStores[0])) {
            ESP.restart();
        }
        consoleout::print("Pending factory reset could not finish. Retry AT*FACTORY.\r\n");
    }
    devicecfg::attachCommands(deviceCommands);
    modem = new atcmd::Modem(store, registry.data(), registry.size());
    modemcfg::LoadResult loaded = modem->begin();
    if (!phonebook.begin()) {
        consoleout::print("Phonebook unavailable/damaged; factory entry used.\r\n");
    }
    createPorts();
    startDiagnostics();
    config::Commands::attach(configCommands);
    createCallServices();
    hwconsole::setPortControl(hwconsole::PortControl{modemIdle, rs232::suspend, rs232::resume});
    netcfg::LoadResult netLoaded = net.begin(millis());
    netcfg::attachCommands(net, msNow, [] {
        return console->callUp() || rs232::session().callUp() || usbport::session().callUp();
    });
    waitForNetwork();
    ota.poll(millis(), net.up(), true, net.settings().host, net.link().ip);
    printBanner(loaded, netLoaded);
    char skip[netcfg::Net::kNoticeMax];
    net.takeNotice(skip, sizeof skip);  // the banner has already said it
    // From here the task watchdog restarts the modem if one loop() pass takes 15 s.
    // The core feeds it before each pass. Work that can take longer inside one pass feeds
    // it as it goes (hw::feedWatchdog()):
    //   - interactive hardware tests (hwconsole.cpp readKey() and testDelay(): every test
    //     loop, the tone and pot tests, the RS232 bridge, up to the 5-minute key time-out);
    //   - an ArduinoOTA upload, received inside OtaService::poll() (its progress callback).
    // Checked and short enough without feeding: AT*FACTORY (NVS erases and file removes),
    // RS232 speed changes (hostDrain(), 2 s at most), the restart (0.5 s), Wi-Fi join and
    // scan, DNS and TCP connects (all asynchronous). The web upload, image staging and
    // LittleFS erase run on the web server task while loop() keeps passing. setup() itself
    // (the 8 s wait for Wi-Fi, a pending factory reset) is not watched.
    enableLoopWDT();
}

void loop() {
    if (restarting) {
        if (millis() - restartAt >= 500) {
            Serial.flush();
            ESP.restart();
        }
        netConsole.flush();  // the client gets the last lines if its socket takes them
        delay(1);
        return;  // no late Wi-Fi join or command may save settings after the wipe
    }
    const uint32_t now = millis();
    internetNetwork.poll();
    pollNetConsole();
    pollBrowser();
    if (webServer.uploading()) {
        net.tick(millis());
        delay(1);
        return;
    }
    // Finish a prior incoming call before a local command can start another one
    // using the shared audio service. Listen only when other resources are ready.
    uint32_t ip = net.up() ? net.link().ip : 0;
    if (ip != incomingIp) {
        incoming->stop();
        incomingIp = ip;
    }
    incoming->poll(!rs232::suspended() && net.joinResult() != netcfg::JoinResult::kRunning &&
                   net.scanState() != netcfg::ScanState::kRunning);
    if (net.up() && !incoming->listening() && !answerListenFailed && !console->localOpen() &&
        console->mode() == hostport::Mode::kCommand && console->port().lineEmpty()) {
        consoleout::print("Answer listener unavailable; retrying port 23.\r\n");
        answerListenFailed = true;
    } else if (incoming->listening() || !net.up()) {
        answerListenFailed = false;
    }
    if (!rs232::suspended() && !hostClient->cable()) {
        hw::lineSet(hwtest::OutSignal::kRI, incoming->ri());
    }
    pollConsole(now);
    rs232::poll(now);
    usbport::poll(now, incoming->ri());  // RI as on the RS232 port above
    net.tick(millis());
    static bool wifiWasUp = false;
    if (wifiWasUp != net.up()) {
        wifiWasUp = net.up();
        debugLog.event(millis(), wifiWasUp ? config::Event::WifiUp : config::Event::WifiDown);
    }
    outgoing->poll();
    configDialer->poll();
    internetDialer->poll();
    configHardware.poll();
    for (unsigned i = 0; i < kSerialPorts; ++i) {
        if (serialClient(i)->changed()) {
            applyClientSettings(i);
        }
    }
    // Departures are dispatched after Session has closed the local service. No
    // listener poll occurs between releasing its slot and starting the action.
    char departure[atcmd::kLineMax + 1];
    for (unsigned i = 0; i < kSerialPorts; ++i) {  // console, RS232, USB, as before
        if (serialMenu(i)->takeDeparture(departure, sizeof departure)) {
            serialSession(i).port().execute(departure);
        }
    }
    devicecfg::wipe(departure, sizeof departure);
    netcfg::pollCommands();  // a finished AT*WIFIJOIN or AT*WIFISCAN
    deviceCommands.poll();
    // The command may have opened a window after this loop's timestamp was sampled.
    ota.poll(millis(), net.up(), deviceControl.idle() && !restarting, net.settings().host,
             net.link().ip);
    pollNotices();
    pollLeds(now);
    delay(1);
}
