// USR Modem Emulator - firmware entry point.
//
// v0.5: two serial ports run the Sportster AT command parser (lib/atcmd) on the same
// settings stored in NVS (lib/modemcfg, hw::NvsStore):
//   - the USB maintenance console (UART0, fixed 115200 8N1);
//   - the RS232 host port (UART1, rs232.cpp): speed from "AT" or AT*BAUD, flow control,
//     DTR, DCD.
// Each port has a session (lib/hostport) with command, online and online-command modes;
// AT*TESTCALL makes a test call to a built-in echo. The front-panel LEDs follow brief
// table 5.4. Project commands are in the AT* table below; AT*HW... are the hardware
// tests (hwconsole.cpp).
//
// v0.6: Wi-Fi (one saved network, DHCP or static IP), mDNS name and the NTP clock
// (lib/netcfg, hw::EspRadio). ARQ and the on-board RGB LED show the Wi-Fi state. The
// boot banner waits up to 8 s for the network; later changes are printed on the console.
// v0.7: device credentials, password-authenticated OTA with an optional ten-minute
// window, and a confirmed factory reset (lib/devicecfg, hw::EspCrypto/OtaService).
// Real calls follow in v0.8.
#include <Arduino.h>

#include "atcmd.h"
#include "connection.h"
#include "device_commands.h"
#include "device_crypto.h"
#include "dialstring.h"
#include "hw.h"
#include "hwconsole.h"
#include "modemcfg.h"
#include "net_commands.h"
#include "netcfg.h"
#include "nvs_store.h"
#include "ota_service.h"
#include "panel.h"
#include "rs232.h"
#include "session.h"
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
hw::EspCrypto crypto;
devicecfg::Device device(deviceStore, crypto);
hw::OtaService ota(device);
hw::EspRadio radio;
netcfg::Net net(netStore, radio);
SerialSink consoleSink;
hostport::CallSlot callSlot;
hostport::EchoConnection echoCall;  // AT*TESTCALL
hostport::Panel panel;

// Set up in setup(), once the AT* table below exists.
atcmd::Modem *modem = nullptr;
hostport::Session *console = nullptr;
modemcfg::Store *resetStores[] = {&store, &netStore};
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

// --- Hardware tests on the port they were typed on ------------------------------------------

class ConsoleTerm : public hwconsole::Term {
public:
    void write(const char *data, size_t len) override {
        Serial.write(reinterpret_cast<const uint8_t *>(data), len);
    }
    int read() override { return Serial.available() > 0 ? Serial.read() : -1; }
    bool isRs232() const override { return false; }
};

class Rs232Term : public hwconsole::Term {
public:
    explicit Rs232Term(atcmd::Port &port) : port_(port) {}
    void write(const char *data, size_t len) override { port_.write(data, len); }
    int read() override { return rs232::readKey(); }
    bool isRs232() const override { return true; }

private:
    atcmd::Port &port_;
};

ConsoleTerm consoleTerm;

bool modemIdle() {
    return !callSlot.busy();
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
    if (isRs232(port)) {
        Rs232Term t(port);
        hwconsole::printHelp(t);
    } else {
        hwconsole::printHelp(consoleTerm);
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
    port.beginInfo();
    if (isRs232(port)) {
        Rs232Term t(port);
        hwconsole::handle(t, args);  // prints its own OK / ERROR
    } else {
        hwconsole::handle(consoleTerm, args);
    }
    return atcmd::Reply::kDone;
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

const atcmd::StarCommand kStarCommands[] = {
    {"HELP", "AT*HELP", "this list", cmdHelp, false},
    {"?", "AT*?", "same as AT*HELP", cmdHelp, false},
    {"VER", "AT*VER", "firmware version", cmdVer, false},
    {"BAUD", "AT*BAUD=<AUTO|300-115200>", "RS232 speed: from \"AT\" (AUTO) or fixed", cmdBaud,
     false},
    {"TESTCALL", "AT*TESTCALL[=<seconds>]", "test call to a built-in echo (+++ returns)",
     cmdTestCall, false},
    {"DIALTEST", "AT*DIALTEST <dial string>", "show how a number is dialled", cmdDialTest, false},
    NETCFG_STAR_COMMANDS,
    DEVICECFG_STAR_COMMANDS,
    {"HW", "AT*HW...", "hardware tests (AT*HW? lists them)", cmdHw, true},
};

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
    uint8_t buf[256];
    size_t room = console->hostSpace();
    size_t n = 0;
    while (n < sizeof buf && n < room && Serial.available() > 0) {
        buf[n++] = static_cast<uint8_t>(Serial.read());
    }
    if (n > 0) {
        console->fromHost(buf, n, now);
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
    if (console->mode() != hostport::Mode::kCommand || !console->port().lineEmpty()) {
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

uint8_t ledMask = 0xFF;  // not yet set

void pollLeds(uint32_t now) {
    hw::rgbShow(net.rgb(now));  // on-board LED: not used by the hardware tests
    if (hwconsole::ledsHeld()) {
        ledMask = 0xFF;  // set again when the test gives them back
        return;
    }
    const modemcfg::Profile &p = modem->active();
    hostport::PanelInput in = {};
    in.autoAnswer = p.s[0] >= 1;
    in.cd = (!rs232::suspended() && rs232::session().dcd()) || console->callUp();
    in.tr = rs232::dtrOn() || p.get(modemcfg::kAmpD) == 0;
    in.cs = hw::ctsOutAsserted();
    in.wifi = arqLed();
    in.toComputer = rs232::bytesToComputer();
    in.fromComputer = rs232::bytesFromComputer();
    uint8_t mask = panel.update(in, now);
    if (mask != ledMask) {
        hw::ledsSet(mask);
        ledMask = mask;
    }
}

}  // namespace

void setup() {
    Serial.begin(kConsoleBaud);
    // No ESP-IDF driver messages on the console. A long stream on the RS232 port in
    // command mode overflows the speed capture, and the RMT driver would print an error
    // from its interrupt for every chunk it drops (switchable with AT*LOG in v0.10).
    esp_log_level_set("*", ESP_LOG_NONE);
    delay(500);
    crypto.begin();
    device.begin();
    if (device.needsReset()) {
        if (device.factoryReset(resetStores, sizeof resetStores / sizeof resetStores[0])) {
            ESP.restart();
        }
        Serial.print("Pending factory reset could not finish. Retry AT*FACTORY.\r\n");
    }
    devicecfg::attachCommands(deviceCommands);
    modem = new atcmd::Modem(store, kStarCommands, sizeof kStarCommands / sizeof kStarCommands[0]);
    modemcfg::LoadResult loaded = modem->begin();
    console = new hostport::Session(*modem, consoleSink, kConsoleBaud, callSlot);
    rs232::begin(*modem, callSlot);
    hwconsole::setPortControl(hwconsole::PortControl{modemIdle, rs232::suspend, rs232::resume});
    netcfg::LoadResult netLoaded = net.begin(millis());
    netcfg::attachCommands(net, msNow);
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
    pollConsole(now);
    rs232::poll(now);
    net.tick(now);
    netcfg::pollCommands();  // a finished AT*WIFIJOIN or AT*WIFISCAN
    deviceCommands.poll();
    // The command may have opened a window after this loop's timestamp was sampled.
    ota.poll(millis(), net.up(), deviceControl.idle() && !restarting, net.settings().host,
             net.link().ip);
    pollNotices();
    pollLeds(now);
    delay(1);
}
