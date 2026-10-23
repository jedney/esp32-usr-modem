// USR Modem Emulator - firmware entry point.
//
// Prototype: USB maintenance console (UART0, 115200 8N1) with a boot banner, a few
// commands and, from v0.3, the AT*HW... hardware tests for the electronics build
// (hwconsole.cpp). The modem itself (AT parser on RS232, Wi-Fi, calls) follows later.
#include <Arduino.h>

#include "dialstring.h"
#include "hwconsole.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif
#ifndef FW_COMMIT
#define FW_COMMIT "unknown"
#endif

namespace {

constexpr size_t kLineMax = 128;
char line[kLineMax + 1];
size_t lineLen = 0;

void printBanner() {
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
    Serial.print(
        "Prototype console. AT*HELP lists the commands; AT*HWTEST tests the hardware.\r\n");
}

void dialTest(const char *arg) {
    dialstring::Result r = dialstring::classify(arg);
    Serial.printf("kind=%s", dialstring::kindName(r.kind));
    switch (r.kind) {
        case dialstring::Kind::IpAddress:
        case dialstring::Kind::Host:
            Serial.printf(" host=%s port=%u", r.host, static_cast<unsigned>(r.port));
            break;
        case dialstring::Kind::Phonebook:
            Serial.printf(" number=%s", r.number);
            break;
        case dialstring::Kind::Special:
            Serial.printf(" number=%s special=%s", r.number, dialstring::specialName(r.special));
            break;
        case dialstring::Kind::Invalid:
        default:
            break;
    }
    Serial.print("\r\n");
    Serial.print(r.kind == dialstring::Kind::Invalid ? "ERROR\r\n" : "OK\r\n");
}

bool startsWithNoCase(const char *s, const char *prefix) {
    for (; *prefix; ++s, ++prefix) {
        if (toupper(static_cast<unsigned char>(*s)) != *prefix) {
            return false;
        }
    }
    return true;
}

void printHelp() {
    Serial.print("  AT                                                   OK\r\n");
    Serial.print("  AT*VER                                               firmware version\r\n");
    Serial.print("  AT*HELP or AT*?                                      this list\r\n");
    Serial.print(
        "  AT*DIALTEST <dial string>                            show how a number is dialled\r\n");
    hwconsole::printHelp();
    Serial.print("OK\r\n");
}

void handleLine(const char *cmd) {
    if (cmd[0] == '\0') {
        return;
    }
    if (startsWithNoCase(cmd, "AT*HW")) {
        hwconsole::handle(cmd + strlen("AT*HW"));
    } else if ((startsWithNoCase(cmd, "AT*HELP") && cmd[7] == '\0') ||
               (startsWithNoCase(cmd, "AT*?") && cmd[4] == '\0')) {
        printHelp();
    } else if (startsWithNoCase(cmd, "AT*DIALTEST")) {
        const char *arg = cmd + strlen("AT*DIALTEST");
        dialTest(arg);
    } else if (startsWithNoCase(cmd, "AT*VER") && cmd[6] == '\0') {
        Serial.printf("v%s (%s)\r\nOK\r\n", FW_VERSION, FW_COMMIT);
    } else if (startsWithNoCase(cmd, "AT") && cmd[2] == '\0') {
        Serial.print("OK\r\n");
    } else {
        Serial.print("ERROR\r\n");
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(500);
    printBanner();
}

void loop() {
    static bool lastWasCr = false;
    while (Serial.available() > 0) {
        char c = static_cast<char>(Serial.read());
        bool crlfPair = (c == '\n' && lastWasCr);
        lastWasCr = (c == '\r');
        if (crlfPair) {
            continue;  // second half of CR LF
        }
        if (c == '\r' || c == '\n') {
            Serial.print("\r\n");
            line[lineLen] = '\0';
            handleLine(line);
            lineLen = 0;
        } else if ((c == '\b' || c == 0x7f) && lineLen > 0) {
            --lineLen;
            Serial.print("\b \b");
        } else if (lineLen < kLineMax && c >= ' ') {
            line[lineLen++] = c;
            Serial.write(c);  // echo
        }
    }
    delay(1);
}
