// USR Modem Emulator - firmware entry point: creates the services and runs the main loop.
//
// Four local ports run the Sportster AT command parser (lib/atcmd) on one set of settings
// stored in NVS (lib/modemcfg, hw::NvsStore). Each has a session (lib/hostport) with
// command, online and online-command modes, and its own configuration screen (lib/config):
//   - the maintenance console (UART0, fixed 115200 8N1);
//   - the RS232 host port (UART1, rs232.cpp): speed from "AT" or AT*BAUD, flow control,
//     DTR, DCD, RI;
//   - the USB modem port on the S3's native USB (usbport.cpp);
//   - the browser terminal (web page, hw::WebService), read-only until E is pressed.
// One call at a time (hostport::CallSlot): outgoing Telnet/TCP (calls::Outgoing),
// incoming on port 23 (calls::Incoming), PPP/SLIP Internet dial-up (lib/internet) and the
// AT*TESTCALL echo. Wi-Fi, mDNS and the NTP clock are in lib/netcfg; device credentials,
// OTA and factory reset in lib/devicecfg. The front-panel LEDs follow brief table 5.4.
// Project commands are in lib/config (registry.cpp) with the hooks below; AT*HW... are
// the hardware tests (hwconsole.cpp), run on the port they were typed on.
#include <Arduino.h>
#include <LittleFS.h>

#include <Preferences.h>
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

#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif
#ifndef FW_COMMIT
#define FW_COMMIT "unknown"
#endif

namespace {

constexpr uint32_t kConsoleBaud = 115200;
constexpr uint32_t kBannerWaitMs = 8000;  // for Wi-Fi at boot (decided by Jon, v0.6)

class SerialSink : public atcmd::Sink {
public:
    void write(const char *data, size_t len) override {
        Serial.write(reinterpret_cast<const uint8_t *>(data), len);
    }
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
hw::NvsStore webStore("web", "cfg");
hw::NvsStore clientStore("clients", "cfg");
clienttest::Settings clientSettings(clientStore);
hw::NvsStore internetStore("internet", "cfg");
dialup::Settings internetSettings(internetStore);
hw::InternetNetwork internetNetwork;
dialup::Dialer *internetDialer = nullptr;
webinterface::Settings webSettings(webStore);
hw::WebService webServer(device);
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
bool hardwareRunning = false;
bool browserTestCancelled = false;
volatile const char imageMarker[] = "USRMODEM/1/FIRMWARE/" FW_VERSION "\n";
void pollBrowser();
void publishWebStatus();
void pollLeds(uint32_t now);
class BrowserSink : public atcmd::Sink {
public:
    void write(const char *data, size_t len) override {
        webServer.write(browserClient, reinterpret_cast<const uint8_t *>(data), len);
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
// The Wi-Fi driver's own saved network and password (firmware before v0.6 let it keep them;
// WiFi.persistent(false) stops that now). A factory reset clears them too (5.5-19).
hw::NvsStore wifiDriverStore("nvs.net80211", "unused");
modemcfg::Store *resetStores[] = {&store,        &netStore,      &bookStore,
                                  &historyStore, &resetStore,    &webStore,
                                  &clientStore,  &internetStore, &wifiDriverStore};
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

class ConsoleTerm : public hwconsole::Term {
public:
    void write(const char *data, size_t len) override {
        Serial.write(reinterpret_cast<const uint8_t *>(data), len);
    }
    int read() override { return Serial.available() > 0 ? Serial.read() : -1; }
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
        if (!webServer.read(browserClient, &c, 1)) {
            return -1;
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

class ConfigHardware : public config::Hardware, public config::UiServices {
public:
    calls::Phonebook *phonebook() override { return &::phonebook; }
    config::UiState state() const override {
        config::UiState result;
        result.hostBaud = rs232::baud();
        if (journal.active() && journal.session())
            result.reportedRate = journal.session()->reportedSpeed();
        const char *link = journal.active()                                           ? "Connected"
                           : outgoing && outgoing->stage() != calls::CallStage::kIdle ? "Dialling"
                           : incoming && incoming->stage() != calls::IncomingStage::kIdle
                               ? "Ringing"
                               : "Idle";
        snprintf(result.link, sizeof result.link, "%s", link);
        return result;
    }
    atcmd::Reply command(atcmd::Port &port, const char *name, const char *args) override {
        using atcmd::Reply;
        if (!strcmp(name, "INTERNETEXPORT")) {
            internetDialer->exportSettings(port);
            return Reply::kDone;
        }
        if (!strcmp(name, "INTERNET")) {
            bool read = !*args || !strcmp(args, "?");
            bool hangup = !strcmp(args, "=HANGUP");
            if (!read && !hangup && callSlot.busy() && !ownsMenu(port))
                return Reply::kError;
            auto result = internetDialer->command(port, args);
            if (read)
                port.printf("Network adapter drops=%lu\r\n",
                            static_cast<unsigned long>(internetNetwork.drops()));
            return result;
        }
        if (!strcmp(name, "CLIENTEXPORT")) {
            for (unsigned i = 0; i < kSerialPorts; ++i) {
                const auto &s = clientSettings.get(i);
                port.printf(
                    "; %s client: AT*TEST SET=%s,%u,%u,%u then AT*TEST APPLY (Y) on that port\r\n",
                    clienttest::portName(i), clienttest::charsetName(s.charset), s.columns, s.rows,
                    s.backspace);
            }
            return Reply::kOk;
        }
        if (!strcmp(name, "TEST") || !strcmp(name, "SX") || !strcmp(name, "SY") ||
            !strcmp(name, "SZ") || !strcmp(name, "RX") || !strcmp(name, "RY") ||
            !strcmp(name, "RZ")) {
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
        if (!strcmp(name, "WEBEXPORT")) {
            port.printf("AT*WEB=%s\r\n", webSettings.enabled() ? "ON" : "OFF");
            return Reply::kDone;
        }
        if (!strcmp(name, "WEB")) {
            if (!*args || !strcmp(args, "?")) {
                port.line(webSettings.enabled() ? "ON" : "OFF");
                return Reply::kOk;
            }
            if (strcmp(args, "=ON") && strcmp(args, "=OFF")) {
                return Reply::kError;
            }
            return webSettings.setEnabled(!strcmp(args, "=ON")) ? Reply::kOk : Reply::kError;
        }
        if (!strcmp(name, "APPLYBAUD")) {
            rs232::applyHostBaud();
            return Reply::kDone;
        }
        if (!strcmp(name, "CONFIG")) {
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
        if (!strcmp(name, "STYLE")) {
            if (!*args || !strcmp(args, "?")) {
                auto style = menuOf(port).style();
                port.line(style == config::Style::Plain   ? "PLAIN"
                          : style == config::Style::Cp437 ? "CP437"
                                                          : "ANSI");
                return Reply::kOk;
            }
            return *args == '=' && menuOf(port).setStyle(args + 1) ? Reply::kOk : Reply::kError;
        }
        if (!strcmp(name, "SIZE")) {
            auto &menu = menuOf(port);
            if (!*args || !strcmp(args, "?")) {
                port.printf("%u,%u\r\n", menu.columns(), menu.rows());
                return Reply::kOk;
            }
            return *args == '=' && menu.setSize(args + 1) ? Reply::kOk : Reply::kError;
        }
        if (!strcmp(name, "STATUS")) {
            if (*args && strcmp(args, "?")) {
                return Reply::kError;
            }
            debugLog.event(millis(), config::Event::StatusRead);
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
            port.printf("Flow &H%u &R%u; DTR &D%u; DCD &C%u; serial &B%u\r\n",
                        modem->active().get(modemcfg::kAmpH), modem->active().get(modemcfg::kAmpR),
                        modem->active().get(modemcfg::kAmpD), modem->active().get(modemcfg::kAmpC),
                        modem->active().get(modemcfg::kAmpB));
            return Reply::kOk;
        }
        if (!strcmp(name, "SOUNDS")) {
            if (*args && strcmp(args, "?")) {
                return Reply::kError;
            }
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
        if (!strcmp(name, "PLAY")) {
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
        if (!strcmp(name, "DNS") || !strcmp(name, "PING")) {
            if ((!strcmp(name, "PING") && internetDialer->active()) || *args != '=' || pending_ ||
                !net.up() || !netcfg::validServer(args + 1) ||
                !probe_.start(args + 1, !strcmp(name, "PING"), millis())) {
                return Reply::kError;
            }
            pending_ = &port;
            operation_ = Operation::Probe;
            debugLog.event(millis(), config::Event::NetworkProbe);
            port.line("Network test; any key cancels.");
            port.wait(cancel, this);
            return Reply::kPending;
        }
        if (!strcmp(name, "RESTART")) {
            if (*args || pending_ || !deviceControl.idle()) {
                return Reply::kError;
            }
            pending_ = &port;
            operation_ = Operation::Restart;
            started_ = millis();
            port.askConfirmation("Restart modem? Y/N (Enter): ", confirm, this);
            return Reply::kPending;
        }
        if (!strcmp(name, "RESETINFO")) {
            if (*args && strcmp(args, "?")) {
                return Reply::kError;
            }
            uint8_t record[4] = {};
            size_t n = resetStore.load(record, sizeof record);
            port.printf("Current reset: %s\r\n",
                        hwconsole::resetReasonName(int(esp_reset_reason())));
            if (n == 3 && record[0] == 1) {
                port.printf("Previous reset: %s\r\n", hwconsole::resetReasonName(record[2]));
            }
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH && CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF
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
        return Reply::kError;
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
    Serial.printf("Settings: %s, power-on template Y%u\r\n", modemcfg::loadResultText(loaded),
                  static_cast<unsigned>(modem->nvram().y));
    char speed[48];
    if (modem->hostBaud() == 0) {
        snprintf(speed, sizeof speed, "from \"AT\" (starts at %u)",
                 static_cast<unsigned>(rs232::baud()));
    } else {
        snprintf(speed, sizeof speed, "fixed %u (AT*BAUD)",
                 static_cast<unsigned>(modem->hostBaud()));
    }
    Serial.printf("RS232: speed %s, &B%u, flow control &H%u&R%u, &C%u &D%u\r\n", speed,
                  static_cast<unsigned>(p.get(modemcfg::kAmpB)),
                  static_cast<unsigned>(p.get(modemcfg::kAmpH)),
                  static_cast<unsigned>(p.get(modemcfg::kAmpR)),
                  static_cast<unsigned>(p.get(modemcfg::kAmpC)),
                  static_cast<unsigned>(p.get(modemcfg::kAmpD)));
    Serial.printf("USB modem port: %s; auto-answer %s\r\n",
                  usbport::started() ? "on (native USB / rear USB-C)" : "failed to start",
                  modemcfg::answerPortName(modem->answerPort()));
    Serial.printf(
        "Echo E%u, quiet Q%u, verbal V%u, results X%u\r\n",
        static_cast<unsigned>(p.get(modemcfg::kE)), static_cast<unsigned>(p.get(modemcfg::kQ)),
        static_cast<unsigned>(p.get(modemcfg::kV)), static_cast<unsigned>(p.get(modemcfg::kX)));
}

// Wi-Fi, network name and clock (brief 6.7). Never the Wi-Fi password.
void printNetwork(netcfg::LoadResult netLoaded) {
    const netcfg::Settings &s = net.settings();
    if (netLoaded == netcfg::LoadResult::kDamaged) {
        Serial.printf("Wi-Fi: %s\r\n", netcfg::loadResultText(netLoaded));
    }
    if (!s.enabled) {
        Serial.print("Wi-Fi: off (AT*WIFI=ON switches it on)\r\n");
    } else if (!s.hasNetwork()) {
        Serial.print("Wi-Fi: not set up (AT*WIFISCAN, then AT*WIFIJOIN=<n>)\r\n");
    } else if (net.up()) {
        const netcfg::LinkInfo &l = net.link();
        char ip[16];
        netcfg::formatIp(l.ip, ip, sizeof ip);
        Serial.printf("Wi-Fi: %s, %d dBm, IP %s (%s)\r\n", s.ssid, static_cast<int>(l.rssi), ip,
                      s.dhcp ? "DHCP" : "static");
    } else {
        Serial.printf("Wi-Fi: %s, %s\r\n", s.ssid, net.stateText());
    }
    Serial.printf("Name: %s.local\r\n", s.host);
    const int64_t utc = net.utcNow();
    if (utc != 0) {
        char t[48];
        netcfg::tz::formatLocal(net.zone(), utc, t, sizeof t);
        Serial.printf("Clock: %s\r\n", t);
    } else {
        Serial.printf("Clock: not set yet (NTP %s, zone %s)\r\n", s.ntp, s.tz);
    }
}

void printBanner(modemcfg::LoadResult loaded, netcfg::LoadResult netLoaded) {
    Serial.print("\r\n");
    Serial.print("  _   _ ____  ____    __  __           _\r\n");
    Serial.print(" | | | / ___||  _ \\  |  \\/  | ___   __| | ___ _ __ ___\r\n");
    Serial.print(" | | | \\___ \\| |_) | | |\\/| |/ _ \\ / _` |/ _ \\ '_ ` _ \\\r\n");
    Serial.print(" | |_| |___) |  _ <  | |  | | (_) | (_| |  __/ | | | | |\r\n");
    Serial.print("  \\___/|____/|_| \\_\\ |_|  |_|\\___/ \\__,_|\\___|_| |_| |_|\r\n");
    Serial.printf("\r\nUSR Modem Emulator v%s (%s)\r\n", FW_VERSION, FW_COMMIT);
    Serial.printf("ESP32-S3, flash %u MB, PSRAM %u KB, reset: %s\r\n",
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                  static_cast<unsigned>(ESP.getPsramSize() / 1024),
                  hwconsole::resetReasonName(static_cast<int>(esp_reset_reason())));
    printNetwork(netLoaded);
    printSettings(loaded);
    Serial.printf("OTA: %s, %s\r\n",
                  device.policy() == devicecfg::Policy::kAlways ? "ALWAYS" : "DEMAND",
                  device.wantsListener(millis(), net.up(), true) ? "listening" : "closed");
    if (!device.ready()) {
        Serial.print("Device credentials unavailable. Use AT*PASS or AT*FACTORY.\r\n");
    } else if (*device.bootPassword() != '\0') {
        Serial.print("Device login: admin\r\nDevice password: ");
        Serial.print(device.bootPassword());
        Serial.print("\r\nShown at boot until first use or change. AT*PASS replaces it.\r\n");
    }
    Serial.print(
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
    if (n > 0) {
        consoleInput.append(buf, n);
        consoleInput.poll(*console, now);
        devicecfg::wipe(buf, n);  // input can contain passwords (SEC-07e)
    }
    int space = Serial.availableForWrite();
    if (space > 0) {
        size_t cap =
            static_cast<size_t>(space) < sizeof buf ? static_cast<size_t>(space) : sizeof buf;
        size_t out = console->toHost(buf, cap);
        if (out > 0) {
            Serial.write(buf, out);
        }
    }
    console->tick(now, true);  // the console has no DTR
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
        Serial.printf("%s\r\n", text);
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
    hw::rgbShow(net.rgb(now));  // on-board LED: not used by the hardware tests
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
    // RS232 and the USB modem port both drive CD, TR, RD and SD (Jon, v2.0).
    hostport::addHostPort(in, {!rs232::suspended() && rs232::session().dcd(), rs232::dtrOn(),
                               rs232::bytesToComputer(), rs232::bytesFromComputer()});
    hostport::addHostPort(in, {usbport::session().dcd(), usbport::dtrOn(),
                               usbport::bytesToComputer(), usbport::bytesFromComputer()});
    uint8_t mask = panel.update(in, now);
    if (mask != ledMask || mask != hw::ledsMask()) {
        hw::ledsSet(mask);
        ledMask = mask;
    }
}

void browserAccess(uint32_t id, bool edit) {
    webServer.text(id,
                   edit ? "{\"t\":\"access\",\"edit\":true}" : "{\"t\":\"access\",\"edit\":false}");
}
void browserWaiting(uint32_t id) {
    static const char message[] =
        "\x1b[2J\x1b[HUSR Modem live status\r\n"
        "Read-only. Press E to open the configuration screen.\r\n"
        "(It opens only when no call, test or other menu is using the modem.)\r\n";
    webServer.write(id, reinterpret_cast<const uint8_t *>(message), sizeof message - 1);
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
    static netcfg::tz::OffsetCache offsets;  // offsetAt() is slow for every pass (PERF-02)
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
    } else if (console->callUp() || rs232::session().callUp() || usbport::session().callUp()) {
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
    std::string call = std::string("{\"t\":\"call\",\"state\":\"") + webCallState() + '"';
    if (journal.active()) {
        const auto &r = journal.current();
        const bool internet = r.kind == calls::Kind::Ppp || r.kind == calls::Kind::Slip;
        call += std::string(",\"mode\":\"") +
                (internet ? (r.kind == calls::Kind::Ppp ? "ppp" : "slip")
                 : r.raw  ? "raw"
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
    webServer.snapshot(3, call + "}");
}

// Nothing is built while no browser is connected (PERF-02). A browser that arrives
// after none gets all four messages rebuilt in the same pass.
void publishWebStatus() {
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
void pollBrowser() {
    char ip[16];
    netcfg::formatIp(net.link().ip, ip, sizeof ip);
    bool ok, fs;
    if (webServer.takeUploadDone(ok, fs)) {
        webServer.discardInput(browserClient);
        callSlot.release(&webServer);
        if (ok) {
            deviceControl.restart();
        } else if (fs) {
            LittleFS.begin(false);
        }
    }
    webServer.poll(webSettings.enabled(), net.up(), net.settings().host, ip);
    if (webServer.takeUnmountRequest()) {
        callAudio.stop();
        LittleFS.end();
        webServer.unmounted();
    }
    uint32_t uploadClient;
    if (webServer.takeUploadRequest(uploadClient)) {
        bool owner = browserClient == uploadClient && browserMenu->isOpen();
        bool allow = !hardwareRunning && owner && console->port().lineEmpty() &&
                     rs232::session().port().lineEmpty() && usbport::session().port().lineEmpty() &&
                     net.joinResult() != netcfg::JoinResult::kRunning &&
                     net.scanState() != netcfg::ScanState::kRunning;
        if (allow) {  // only the browser with the configuration screen may upload
            webServer.discardInput(browserClient);
            browserSession->hangUp();
            hwconsole::restoreBrowserResources();
            browserAccess(browserClient, false);
        }
        allow = allow && callSlot.claim(&webServer);
        if (allow) {
            callAudio.stop();
            ota.poll(millis(), net.up(), false, net.settings().host, net.link().ip);
        }
        // The server may have stopped waiting (2 s) while we checked: then allowUpload()
        // changes nothing and we release the claim, so no upload is left half-started
        // (STA-07). ArduinoOTA resumes on the next ota.poll(); the browser keeps its
        // read-only view and can press E again.
        if (!webServer.allowUpload(allow) && allow) {
            callSlot.release(&webServer);
        }
    }
    if (webServer.uploading()) {
        return;
    }
    if (browserClient && (!webServer.connected(browserClient) ||
                          (!browserSession->localOpen() && !browserSession->port().busy()))) {
        if (!webServer.connected(browserClient)) {
            netcfg::detachCommands(browserSession->port());
        }
        webServer.discardInput(browserClient);
        browserSession->port().cancelPrompt();
        browserSession->hangUp();
        hwconsole::restoreBrowserResources();
        if (webServer.connected(browserClient)) {
            browserWaiting(browserClient);
        }
        browserClient = 0;
    }
    static uint32_t seen[hw::WebService::kClients] = {};
    for (unsigned i = 0; i < hw::WebService::kClients; ++i) {
        uint32_t id = webServer.clientId(i);
        bool fresh = id && seen[i] != id;
        seen[i] = id;
        if (!id) {
            continue;
        }
        uint8_t bytes[128];
        // Every browser starts read-only (STA-02): opening the page claims nothing, so a
        // forgotten tab can't turn callers away. E takes the configuration screen when free.
        if (id != browserClient) {
            if (fresh) {
                browserWaiting(id);
            }
            size_t n = webServer.read(id, bytes, sizeof bytes);
            bool enter = n && (bytes[0] == 'e' || bytes[0] == 'E');
            while (webServer.read(id, bytes, sizeof bytes)) {
            }
            if (enter && !browserClient && !callSlot.busy() && !hardwareRunning) {
                browserClient = id;
                if (browserMenu->open() && browserSession->startLocal(*browserMenu)) {
                    browserAccess(id, true);
                } else {
                    browserMenu->close();
                    browserClient = 0;
                }
            }
            devicecfg::wipe(bytes, sizeof bytes);
            continue;
        }
        // Read at most one key: an action may enter an interactive hardware test,
        // whose adapter must retain the following queued keys for that test.
        unsigned columns, rows;
        if (webServer.takeSize(id, columns, rows))
            browserMenu->resize(columns, rows);
        if (webServer.read(id, bytes, 1)) {
            browserSession->fromHost(bytes, 1, millis());
        }
        browserSession->tick(millis(), true);
        // CP437 can expand to three UTF-8 bytes. Pull only what the transport
        // can retain, leaving the shared terminal's response cursor in place.
        size_t room = webServer.space(id) / 3;
        size_t n = browserSession->toHost(bytes, room < sizeof bytes ? room : sizeof bytes);
        if (n) {
            // Menu CP437 is converted at this transport only; serial stays CP437.
            std::string output =
                webinterface::terminalUtf8(bytes, n, browserMenu->style() == config::Style::Cp437);
            webServer.write(id, reinterpret_cast<const uint8_t *>(output.data()), output.size());
        }
        char departure[atcmd::kLineMax + 1];
        if (browserMenu->takeDeparture(departure, sizeof departure)) {
            browserSession->tick(millis(), true);
            hwconsole::restoreBrowserResources();
            if (!strncmp(departure, "*DIAL=", 6)) {
                // Check first that the RS232 port can take the dial (MNT-05a).
                auto &host = rs232::session();
                if (host.mode() == hostport::Mode::kCommand && !host.localOpen() &&
                    host.port().lineEmpty()) {
                    browserSession->port().line("Dialling on the RS232 host port.");
                    host.port().execute(departure);
                } else {
                    browserSession->port().line("The RS232 host port is busy: not dialled.");
                }
            } else {
                browserSession->port().execute(departure);
            }
        }
        devicecfg::wipe(departure, sizeof departure);
        devicecfg::wipe(bytes, sizeof bytes);
    }
    publishWebStatus();
    webServer.pump();
}

}  // namespace

void setup() {
    (void)imageMarker[0];  // retain project identity in the linked application image
    Serial.begin(kConsoleBaud);
    // No ESP-IDF driver messages on the console. A long stream on the RS232 port in
    // command mode overflows the speed capture, and the RMT driver would print an error
    // from its interrupt for every chunk it drops (switchable with AT*LOG in v0.10).
    esp_log_level_set("*", ESP_LOG_NONE);
    delay(500);
    LittleFS.begin(false);  // mount before recovering a reset which erases history
    crypto.begin();
    device.begin();
    webSettings.begin();
    clientSettings.begin();
    internetSettings.begin();
    if (device.needsReset()) {
        if (device.factoryReset(resetStores, sizeof resetStores / sizeof resetStores[0])) {
            ESP.restart();
        }
        Serial.print("Pending factory reset could not finish. Retry AT*FACTORY.\r\n");
    }
    devicecfg::attachCommands(deviceCommands);
    modem = new atcmd::Modem(store, registry.data(), registry.size());
    modemcfg::LoadResult loaded = modem->begin();
    if (!phonebook.begin()) {
        Serial.print("Phonebook unavailable/damaged; factory entry used.\r\n");
    }
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
    browserMenu->setStyle("CP437");
    browserMenu->color(config::Color::Rgb);
    consoleClient =
        new clienttest::Service(clientSettings, clienttest::kConsolePort, consoleClientPlatform);
    hostClient =
        new clienttest::Service(clientSettings, clienttest::kRs232Port, hostClientPlatform);
    usbClient = new clienttest::Service(clientSettings, clienttest::kUsbPort, usbClientPlatform);
    for (unsigned i = 0; i < kSerialPorts; ++i) {
        applyClientSettings(i);
    }
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
    config::Commands::attach(configCommands);
    outgoing =
        new calls::Outgoing(*modem, phonebook, callSlot, tcpSocket, callAudio, msNow, networkUp);
    outgoing->setObserver(&journal);
    configDialer = new config::Dialer(*console, *consoleMenu, *hostMenu, *outgoing, callSlot,
                                      callAudio, msNow);
    configDialer->setUsb(&usbport::session(), usbMenu);
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
    hwconsole::setPortControl(hwconsole::PortControl{modemIdle, rs232::suspend, rs232::resume});
    netcfg::LoadResult netLoaded = net.begin(millis());
    netcfg::attachCommands(net, msNow, [] {
        return console->callUp() || rs232::session().callUp() || usbport::session().callUp();
    });
    // With a saved network, give it a few seconds so the banner can show the address.
    const netcfg::Settings &s = net.settings();
    const uint32_t start = millis();
    while (s.enabled && s.hasNetwork() && !net.up() && millis() - start < kBannerWaitMs) {
        const uint32_t now = millis();
        net.tick(now);
        pollLeds(now);
        delay(10);
    }
    net.tick(millis());
    ota.poll(millis(), net.up(), true, net.settings().host, net.link().ip);
    printBanner(loaded, netLoaded);
    char skip[netcfg::Net::kNoticeMax];
    net.takeNotice(skip, sizeof skip);  // the banner has already said it
}

void loop() {
    if (restarting) {
        if (millis() - restartAt >= 500) {
            Serial.flush();
            ESP.restart();
        }
        delay(1);
        return;  // no late Wi-Fi join or command may save settings after the wipe
    }
    const uint32_t now = millis();
    internetNetwork.poll();
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
        Serial.print("Answer listener unavailable; retrying port 23.\r\n");
        answerListenFailed = true;
    } else if (incoming->listening() || !net.up()) {
        answerListenFailed = false;
    }
    if (!rs232::suspended() && !hostClient->cable()) {
        hw::lineSet(hwtest::OutSignal::kRI, incoming->ri());
    }
    pollConsole(now);
    rs232::poll(now);
    usbport::poll(now);
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
