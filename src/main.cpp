// USR Modem Emulator - firmware entry point.
//
// v0.4: the USB maintenance console (UART0, 115200 8N1) runs the Sportster AT command
// parser (lib/atcmd) on the settings stored in NVS (lib/modemcfg, hw::NvsStore). Project
// commands are in the AT* table below; AT*HW... are the hardware tests (hwconsole.cpp).
// The RS232 port follows in v0.5, Wi-Fi in v0.6 and calls in v0.8.
#include <Arduino.h>

#include "atcmd.h"
#include "dialstring.h"
#include "hwconsole.h"
#include "modemcfg.h"
#include "nvs_store.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif
#ifndef FW_COMMIT
#define FW_COMMIT "unknown"
#endif

namespace {

constexpr uint32_t kConsoleBaud = 115200;

class SerialSink : public atcmd::Sink {
public:
    void write(const char *data, size_t len) override {
        Serial.write(reinterpret_cast<const uint8_t *>(data), len);
    }
};

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
    hwconsole::printHelp();
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
    hwconsole::handle(args);  // prints its own OK / ERROR
    return atcmd::Reply::kDone;
}

const atcmd::StarCommand kStarCommands[] = {
    {"HELP", "AT*HELP", "this list", cmdHelp, false},
    {"?", "AT*?", "same as AT*HELP", cmdHelp, false},
    {"VER", "AT*VER", "firmware version", cmdVer, false},
    {"DIALTEST", "AT*DIALTEST <dial string>", "show how a number is dialled", cmdDialTest, false},
    {"HW", "AT*HW...", "hardware tests (AT*HW? lists them)", cmdHw, true},
};

hw::NvsStore store;
SerialSink consoleSink;
atcmd::Modem modem(store, kStarCommands, sizeof kStarCommands / sizeof kStarCommands[0]);
atcmd::Port console(modem, consoleSink, kConsoleBaud);

// --- Boot banner ------------------------------------------------------------------------

void printSettings(modemcfg::LoadResult loaded) {
    const modemcfg::Profile &p = modem.active();
    Serial.printf("Settings: %s, power-on template Y%u\r\n", modemcfg::loadResultText(loaded),
                  static_cast<unsigned>(modem.nvram().y));
    char speed[40];
    if (p.get(modemcfg::kAmpB) == 1) {
        snprintf(speed, sizeof speed, "fixed %u (&B1)", static_cast<unsigned>(p.baud));
    } else {
        snprintf(speed, sizeof speed, "from \"AT\" (&B%u)",
                 static_cast<unsigned>(p.get(modemcfg::kAmpB)));
    }
    Serial.printf("Host serial (from v0.5): speed %s, flow control &H%u&R%u, &C%u &D%u\r\n", speed,
                  static_cast<unsigned>(p.get(modemcfg::kAmpH)),
                  static_cast<unsigned>(p.get(modemcfg::kAmpR)),
                  static_cast<unsigned>(p.get(modemcfg::kAmpC)),
                  static_cast<unsigned>(p.get(modemcfg::kAmpD)));
    Serial.printf(
        "Echo E%u, quiet Q%u, verbal V%u, results X%u\r\n",
        static_cast<unsigned>(p.get(modemcfg::kE)), static_cast<unsigned>(p.get(modemcfg::kQ)),
        static_cast<unsigned>(p.get(modemcfg::kV)), static_cast<unsigned>(p.get(modemcfg::kX)));
}

void printBanner(modemcfg::LoadResult loaded) {
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
    printSettings(loaded);
    Serial.print(
        "Maintenance console. ATI4 shows the settings, AT*HELP the project commands,\r\n"
        "AT*HWTEST tests the hardware.\r\n");
}

}  // namespace

void setup() {
    Serial.begin(kConsoleBaud);
    delay(500);
    modemcfg::LoadResult loaded = modem.begin();
    printBanner(loaded);
}

void loop() {
    while (Serial.available() > 0) {
        console.feed(static_cast<char>(Serial.read()));
    }
    delay(1);
}
