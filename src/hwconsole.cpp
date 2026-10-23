// AT*HW... hardware-test commands (project brief 6.14). They exist to check the
// electronics as they are built: each LED, the amplifier and speaker, the volume pot,
// the RS232 handshake lines and data path. Pins come from lib/hw/src/pins.h.
#include "hwconsole.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>

#include "hw.h"
#include "hwtest.h"
#include "pins.h"
#include "tones.h"
#include "volume.h"

#ifndef FW_VERSION
#define FW_VERSION "0.0"
#endif
#ifndef FW_COMMIT
#define FW_COMMIT "unknown"
#endif

namespace hwconsole {

namespace {

using hwtest::argStart;
using hwtest::wordIs;

constexpr char kCtrlX = 0x18;
constexpr char kEsc = 0x1B;

// Test volume: percent of the speaker cap on the audio curve, or follow the pot.
uint32_t testVolumePct = 75;
bool testVolumeFromPot = false;

// --- Console helpers ---------------------------------------------------------------------

// Throws away what's left of the command line (terminals send CR LF) so a test that stops
// "on any key" doesn't stop at once.
void drainInput() {
    delay(30);
    while (Serial.available() > 0) {
        Serial.read();
    }
}

int readKey() {
    return Serial.available() > 0 ? Serial.read() : -1;
}

bool keyPressed() {
    if (Serial.available() <= 0) {
        return false;
    }
    drainInput();
    return true;
}

int waitKey() {
    int c;
    while ((c = readKey()) < 0) {
        delay(5);
    }
    drainInput();
    return toupper(c);
}

void ok() {
    Serial.print("OK\r\n");
}
void error() {
    Serial.print("ERROR\r\n");
}

const char *yesNo(bool b) {
    return b ? "on " : "off";
}

// --- Volume ------------------------------------------------------------------------------

float testGain() {
    if (testVolumeFromPot) {
        return volume::levelToGain(volume::potLevel(hw::potRaw(), hw::kPotMax));
    }
    return volume::levelToGain(static_cast<float>(testVolumePct) / 100.0f);
}

void printVolume() {
    if (testVolumeFromPot) {
        Serial.print("Test volume: follows the pot");
    } else {
        Serial.printf("Test volume: %u%%", static_cast<unsigned>(testVolumePct));
    }
    float g = volume::outputGain(testGain());
    if (g > 0.0f) {
        Serial.printf(" (%.1f dB below full scale; cap %.1f dB)\r\n",
                      static_cast<double>(-20.0f * log10f(g)),
                      static_cast<double>(-volume::kCapDb));
    } else {
        Serial.print(" (muted)\r\n");
    }
}

bool play(tones::Source &src) {
    Serial.print("Playing (any key stops)...\r\n");
    drainInput();
    if (!hw::audioPlay(src, testGain, keyPressed)) {
        if (!hw::audioReady()) {
            Serial.print("I2S driver failed to start\r\n");
            return false;
        }
        Serial.print("Stopped\r\n");
    }
    return true;
}

// --- Pin map -----------------------------------------------------------------------------

void printPins() {
    Serial.print(
        "GPIO pin map (lib/hw/src/pins.h) - PROPOSED, not yet confirmed by Electronics\r\n");
    Serial.print("RS232 (DCE; asserted = LOW at the GPIO):\r\n");
    Serial.printf("  RXD out  GPIO%-2d  DB25 3   UART1 TX\r\n", pins::kRxd);
    Serial.printf("  TXD in   GPIO%-2d  DB25 2   UART1 RX\r\n", pins::kTxd);
    Serial.printf("  CTS out  GPIO%-2d  DB25 5\r\n", pins::kCts);
    Serial.printf("  RTS in   GPIO%-2d  DB25 4\r\n", pins::kRts);
    Serial.printf("  DTR in   GPIO%-2d  DB25 20\r\n", pins::kDtr);
    Serial.printf("  DCD out  GPIO%-2d  DB25 8\r\n", pins::kDcd);
    Serial.printf("  RI  out  GPIO%-2d  DB25 22\r\n", pins::kRi);
    Serial.print("  DSR      hard-wired on, DB25 6\r\n");
    Serial.printf("Audio: I2S BCLK GPIO%d, LRC GPIO%d, DIN GPIO%d, amp SD GPIO%d (HIGH = on)\r\n",
                  pins::kI2sBclk, pins::kI2sLrc, pins::kI2sDin, pins::kAmpSd);
    Serial.printf("Volume pot wiper: GPIO%d (ADC1)\r\n", pins::kPot);
    Serial.printf("LEDs (%s = lit):", pins::kLedActiveHigh ? "HIGH" : "LOW");
    for (uint8_t i = 0; i < hwtest::kLedCount; ++i) {
        Serial.printf(" %s=GPIO%d", hwtest::ledName(i), pins::kLeds[i]);
    }
    Serial.printf("\r\nOn board: RGB LED GPIO%d; console UART0 GPIO%d/%d\r\n", pins::kRgbLed,
                  pins::kConsoleTx, pins::kConsoleRx);
}

// --- Info and files ----------------------------------------------------------------------

bool mountFs() {
    static bool mounted = false;
    if (!mounted) {
        mounted = LittleFS.begin(false);
    }
    return mounted;
}

void listDir(const char *path, int *count) {
    File dir = LittleFS.open(path);
    if (!dir || !dir.isDirectory()) {
        return;
    }
    for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
        if (f.isDirectory()) {
            listDir(f.path(), count);
        } else {
            Serial.printf("  %-40s %8u\r\n", f.path(), static_cast<unsigned>(f.size()));
            ++*count;
        }
    }
}

void printInfo() {
    Serial.printf("Firmware   v%s (%s)\r\n", FW_VERSION, FW_COMMIT);
    Serial.printf("Chip       %s rev %d, %d cores, %u MHz\r\n", ESP.getChipModel(),
                  static_cast<int>(ESP.getChipRevision()), static_cast<int>(ESP.getChipCores()),
                  static_cast<unsigned>(ESP.getCpuFreqMHz()));
    uint64_t mac = ESP.getEfuseMac();
    Serial.printf(
        "MAC        %02X:%02X:%02X:%02X:%02X:%02X\r\n", static_cast<unsigned>(mac & 0xFF),
        static_cast<unsigned>((mac >> 8) & 0xFF), static_cast<unsigned>((mac >> 16) & 0xFF),
        static_cast<unsigned>((mac >> 24) & 0xFF), static_cast<unsigned>((mac >> 32) & 0xFF),
        static_cast<unsigned>((mac >> 40) & 0xFF));
    Serial.printf("Flash      %u MB at %u MHz; firmware %u KB, OTA slot %u KB free\r\n",
                  static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                  static_cast<unsigned>(ESP.getFlashChipSpeed() / 1000000),
                  static_cast<unsigned>(ESP.getSketchSize() / 1024),
                  static_cast<unsigned>(ESP.getFreeSketchSpace() / 1024));
    Serial.printf("PSRAM      %u KB, %u KB free\r\n",
                  static_cast<unsigned>(ESP.getPsramSize() / 1024),
                  static_cast<unsigned>(ESP.getFreePsram() / 1024));
    Serial.printf("Heap       %u KB, %u KB free\r\n",
                  static_cast<unsigned>(ESP.getHeapSize() / 1024),
                  static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    Serial.printf("Reset      %s\r\n", resetReasonName(static_cast<int>(esp_reset_reason())));
    Serial.printf("Uptime     %lu s\r\n", static_cast<unsigned long>(millis() / 1000));
    if (mountFs()) {
        Serial.printf("LittleFS   %u KB used of %u KB\r\n",
                      static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                      static_cast<unsigned>(LittleFS.totalBytes() / 1024));
    } else {
        Serial.print("LittleFS   not mounted: upload the sounds and web files image\r\n");
    }
}

bool printFiles() {
    if (!mountFs()) {
        Serial.print("LittleFS not mounted: upload the sounds and web files image\r\n");
        return false;
    }
    int count = 0;
    listDir("/", &count);
    Serial.printf("%d files, %u KB used of %u KB\r\n", count,
                  static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                  static_cast<unsigned>(LittleFS.totalBytes() / 1024));
    return true;
}

// --- LEDs --------------------------------------------------------------------------------

void ledChase() {
    Serial.print("Chase (any key stops)\r\n");
    drainInput();
    for (uint32_t step = 0; !keyPressed(); ++step) {
        hw::ledsSet(hwtest::chaseMask(step));
        delay(120);
    }
    hw::ledsSet(0);
}

void ledWalk() {
    Serial.print("Each LED for 1 s, left to right (any key stops)\r\n");
    drainInput();
    for (uint8_t i = 0;; i = static_cast<uint8_t>((i + 1) % hwtest::kLedCount)) {
        hw::ledsSet(static_cast<uint8_t>(1u << i));
        Serial.printf("  %-3s GPIO%d\r\n", hwtest::ledName(i), pins::kLeds[i]);
        for (int t = 0; t < 100; ++t) {
            if (keyPressed()) {
                hw::ledsSet(0);
                return;
            }
            delay(10);
        }
    }
}

bool cmdLed(const char *arg) {
    int led = hwtest::parseLed(arg);
    if (led >= 0) {
        hw::ledsSet(static_cast<uint8_t>(1u << led));
        Serial.printf("%s lit (GPIO%d)\r\n", hwtest::ledName(static_cast<uint8_t>(led)),
                      pins::kLeds[led]);
    } else if (wordIs(arg, "ALL")) {
        hw::ledsSet(0x7F);
        Serial.print("All LEDs lit\r\n");
    } else if (wordIs(arg, "OFF") || wordIs(arg, "NONE")) {
        hw::ledsSet(0);
    } else if (wordIs(arg, "CHASE") || *arg == '\0') {
        ledChase();
    } else if (wordIs(arg, "WALK")) {
        ledWalk();
    } else {
        return false;
    }
    return true;
}

// --- Audio -------------------------------------------------------------------------------

bool cmdAmp(const char *arg) {
    if (wordIs(arg, "ON")) {
        hw::ampSetMode(hw::AmpMode::kOn);
    } else if (wordIs(arg, "OFF")) {
        hw::ampSetMode(hw::AmpMode::kOff);
    } else if (wordIs(arg, "AUTO")) {
        hw::ampSetMode(hw::AmpMode::kAuto);
    } else if (*arg != '?' && *arg != '\0') {
        return false;
    }
    const char *mode = hw::ampMode() == hw::AmpMode::kOn    ? "ON"
                       : hw::ampMode() == hw::AmpMode::kOff ? "OFF (sounds muted; I2S still runs)"
                                                            : "AUTO (on only while a sound plays)";
    Serial.printf("Amp %s, SD pin GPIO%d is %s\r\n", mode, pins::kAmpSd,
                  hw::ampIsOn() ? "HIGH" : "LOW");
    return true;
}

bool cmdVol(const char *arg) {
    uint32_t pct = 0;
    if (wordIs(arg, "POT")) {
        testVolumeFromPot = true;
    } else if (hwtest::parseUint(arg, 0, 100, &pct)) {
        testVolumePct = pct;
        testVolumeFromPot = false;
    } else if (*arg != '?' && *arg != '\0') {
        return false;
    }
    printVolume();
    return true;
}

bool cmdTone(const char *arg) {
    if (wordIs(arg, "DIAL")) {
        tones::ToneSource src(tones::kUkDialTone, 0);
        return play(src);
    }
    if (wordIs(arg, "BUSY")) {
        tones::ToneSource src(tones::kUkBusyTone, 0);
        return play(src);
    }
    if (wordIs(arg, "RING")) {
        tones::ToneSource src(tones::kUkRingTone, 0);
        return play(src);
    }
    if (wordIs(arg, "SWEEP")) {
        Serial.print("Sweep 200 Hz to 4 kHz over 6 s\r\n");
        tones::SweepSource src(200.0f, 4000.0f, 6000);
        return play(src);
    }
    uint32_t hz = 0;
    uint32_t ms = 2000;
    const char *rest = nullptr;
    if (!hwtest::parseUint(arg, tones::kMinToneHz, tones::kMaxToneHz, &hz, &rest)) {
        return false;
    }
    if (*rest != '\0' && !hwtest::parseUint(rest, 10, 60000, &ms)) {
        return false;
    }
    Serial.printf("%u Hz for %u ms\r\n", static_cast<unsigned>(hz), static_cast<unsigned>(ms));
    tones::ToneSource src(static_cast<float>(hz), 0.0f, ms);
    return play(src);
}

bool cmdDtmf(const char *arg) {
    if (*arg == '\0') {
        arg = "0123456789*#";
    }
    Serial.printf("DTMF %s\r\n", arg);
    tones::DtmfSource src(arg);
    return play(src);
}

bool cmdPulse(const char *arg) {
    if (*arg == '\0') {
        arg = "0123456789";
    }
    Serial.printf("Pulse dialling (UK, 10 pps) %s\r\n", arg);
    tones::PulseSource src(arg);
    return play(src);
}

// --- Pot ---------------------------------------------------------------------------------

uint16_t potMinSeen = hw::kPotMax;
uint16_t potMaxSeen = 0;
uint32_t potLastPrint = 0;

void printPotLine() {
    uint16_t raw = hw::potRaw();
    potMinSeen = raw < potMinSeen ? raw : potMinSeen;
    potMaxSeen = raw > potMaxSeen ? raw : potMaxSeen;
    float level = volume::potLevel(raw, hw::kPotMax);
    float gain = volume::levelToGain(level);
    char db[12];
    if (gain > 0.0f) {
        snprintf(db, sizeof db, "%6.1f dB", static_cast<double>(20.0f * log10f(gain)));
    } else {
        snprintf(db, sizeof db, "   muted");
    }
    Serial.printf("\r  raw %4u  %4lu mV  level %3d%%  %s   ", static_cast<unsigned>(raw),
                  static_cast<unsigned long>(hw::potMillivolts()),
                  static_cast<int>(level * 100.0f + 0.5f), db);
}

bool potStop() {
    if (millis() - potLastPrint >= 200) {
        potLastPrint = millis();
        printPotLine();
    }
    return keyPressed();
}

float potGain() {
    return volume::levelToGain(volume::potLevel(hw::potRaw(), hw::kPotMax));
}

// Returns true if the pot reached both ends.
bool runPot(bool withTone) {
    potMinSeen = hw::kPotMax;
    potMaxSeen = 0;
    Serial.printf("Pot on GPIO%d. Turn it end to end%s (any key stops)\r\n", pins::kPot,
                  withTone ? "; a 1 kHz tone follows it" : "");
    drainInput();
    if (withTone) {
        tones::ToneSource src(1000.0f, 0.0f, 0);
        hw::audioPlay(src, potGain, potStop);
    } else {
        while (!potStop()) {
            delay(10);
        }
    }
    float lo = volume::potLevel(potMinSeen, hw::kPotMax);
    float hi = volume::potLevel(potMaxSeen, hw::kPotMax);
    Serial.printf("\r\nRange seen: raw %u to %u (level %d%% to %d%%)\r\n",
                  static_cast<unsigned>(potMinSeen), static_cast<unsigned>(potMaxSeen),
                  static_cast<int>(lo * 100.0f + 0.5f), static_cast<int>(hi * 100.0f + 0.5f));
    return lo <= 0.0f && hi >= 1.0f;
}

// --- RS232 lines -------------------------------------------------------------------------

void printLines() {
    using hwtest::OutSignal;
    Serial.printf(
        "\r  in: DTR %s  RTS %s   out: DCD %s  RI %s  CTS %s  (DSR on)  ", yesNo(hw::dtrAsserted()),
        yesNo(hw::rtsAsserted()), yesNo(hw::lineIsAsserted(OutSignal::kDCD)),
        yesNo(hw::lineIsAsserted(OutSignal::kRI)), yesNo(hw::lineIsAsserted(OutSignal::kCTS)));
}

void runLines() {
    using hwtest::OutSignal;
    hw::linesBegin();
    Serial.print("Live RS232 handshake lines. In: from the computer. Out: from the modem.\r\n");
    Serial.print("Keys: D = toggle DCD, R = toggle RI, C = toggle CTS, Q = quit\r\n");
    drainInput();
    uint32_t last = 0;
    for (;;) {
        int c = toupper(readKey());
        if (c == 'D' || c == 'R' || c == 'C') {
            OutSignal s = c == 'D' ? OutSignal::kDCD : c == 'R' ? OutSignal::kRI : OutSignal::kCTS;
            hw::lineSet(s, !hw::lineIsAsserted(s));
            last = 0;
        } else if (c == 'Q' || c == kEsc || c == '\r' || c == '\n') {
            break;
        }
        if (millis() - last >= 100) {
            last = millis();
            printLines();
        }
        delay(5);
    }
    for (int i = 0; i < 3; ++i) {
        hw::lineSet(static_cast<OutSignal>(i), false);
    }
    Serial.print("\r\nOutputs set back to off\r\n");
    drainInput();
}

bool cmdSig(const char *arg) {
    hwtest::OutSignal s = hwtest::OutSignal::kDCD;
    bool on = false;
    if (!hwtest::parseOutSignal(arg, &s)) {
        return false;
    }
    const char *p = arg;  // skip the signal name ("CD" and "CS" are accepted too)
    while (*p && *p != ',' && *p != ' ') {
        ++p;
    }
    while (*p == ',' || *p == ' ') {
        ++p;
    }
    if (!hwtest::parseOnOff(p, &on)) {
        return false;
    }
    hw::lineSet(s, on);
    Serial.printf("%s %s (stays until changed, AT*HWLINES or a restart)\r\n",
                  hwtest::outSignalName(s),
                  on ? "asserted (+V on the cable)" : "off (-V on the cable)");
    return true;
}

// Loopback plug: RXD-TXD, CTS-RTS, DCD-DTR (DB25 3-2, 5-4, 8-20; DB9 2-3, 8-7, 1-4).
struct LoopResult {
    bool data, cts, dcd;
    size_t sent, received, errors;
};

LoopResult loopbackAt(uint32_t baud) {
    using hwtest::OutSignal;
    LoopResult r = {};
    hw::linesBegin();
    hw::uartBegin(baud);
    HardwareSerial &u = hw::uart();
    delay(20);
    while (u.available() > 0) {
        u.read();
    }
    size_t count = baud / 20;  // about half a second of data
    count = count < 8 ? 8 : (count > 256 ? 256 : count);
    for (size_t i = 0; i < count; ++i) {
        u.write(hwtest::loopbackByte(i));
    }
    uint32_t timeoutMs = static_cast<uint32_t>((count * 10 * 1000) / baud) * 2 + 200;
    uint32_t start = millis();
    while (r.received < count && millis() - start < timeoutMs) {
        int b = u.read();
        if (b < 0) {
            delay(1);
            continue;
        }
        if (static_cast<uint8_t>(b) != hwtest::loopbackByte(r.received)) {
            ++r.errors;
        }
        ++r.received;
    }
    r.sent = count;
    r.data = r.received == count && r.errors == 0;
    hw::uartEnd();

    auto follows = [](OutSignal out, bool (*in)()) {
        hw::lineSet(out, true);
        delay(5);
        bool on = in();
        hw::lineSet(out, false);
        delay(5);
        bool off = !in();
        return on && off;
    };
    r.cts = follows(OutSignal::kCTS, hw::rtsAsserted);
    r.dcd = follows(OutSignal::kDCD, hw::dtrAsserted);
    return r;
}

const char *passFail(bool b) {
    return b ? "PASS" : "FAIL";
}

bool runLoop(const char *arg, bool quiet = false) {
    static const uint32_t kAll[] = {300, 1200, 2400, 9600, 19200, 38400, 57600, 115200, 230400};
    uint32_t one = 115200;
    const uint32_t *bauds = &one;
    size_t n = 1;
    if (wordIs(arg, "ALL")) {
        bauds = kAll;
        n = sizeof kAll / sizeof kAll[0];
    } else if (*arg != '\0' && !hwtest::parseUint(arg, 300, 921600, &one)) {
        return false;
    }
    if (!quiet) {
        Serial.print("Loopback plug needed: RXD-TXD, CTS-RTS, DCD-DTR\r\n");
        Serial.print("  (DB25 3-2, 5-4, 8-20; bench DB9 2-3, 8-7, 1-4)\r\n");
    }
    bool all = true;
    for (size_t i = 0; i < n; ++i) {
        LoopResult r = loopbackAt(bauds[i]);
        Serial.printf("  %6lu baud: data %s (%u/%u bytes, %u wrong)  CTS->RTS %s  DCD->DTR %s\r\n",
                      static_cast<unsigned long>(bauds[i]), passFail(r.data),
                      static_cast<unsigned>(r.received), static_cast<unsigned>(r.sent),
                      static_cast<unsigned>(r.errors), passFail(r.cts), passFail(r.dcd));
        all = all && r.data && r.cts && r.dcd;
    }
    Serial.print(
        "  RI has no input to loop back to: check it with AT*HWLINES and the\r\n"
        "  terminal's RI light, or AT*HWSIG=RI,ON and a meter on DB25 22\r\n");
    Serial.printf("Loopback %s\r\n", passFail(all));
    return all;
}

void runBridge(uint32_t baud) {
    using hwtest::OutSignal;
    hw::linesBegin();
    hw::uartBegin(baud);
    hw::lineSet(OutSignal::kCTS, true);  // so terminals using RTS/CTS will send
    hw::lineSet(OutSignal::kDCD, true);
    Serial.printf("Bridge: console <-> RS232 at %lu 8N1, CTS and DCD on. Ctrl-X ends.\r\n",
                  static_cast<unsigned long>(baud));
    drainInput();
    HardwareSerial &u = hw::uart();
    bool dtr = hw::dtrAsserted();
    bool rts = hw::rtsAsserted();
    Serial.printf("[DTR %s, RTS %s]\r\n", yesNo(dtr), yesNo(rts));
    for (;;) {
        int c = readKey();
        if (c == kCtrlX) {
            break;
        }
        if (c >= 0) {
            u.write(static_cast<uint8_t>(c));
        }
        while (u.available() > 0) {
            Serial.write(static_cast<uint8_t>(u.read()));
        }
        if (hw::dtrAsserted() != dtr || hw::rtsAsserted() != rts) {
            dtr = hw::dtrAsserted();
            rts = hw::rtsAsserted();
            Serial.printf("\r\n[DTR %s, RTS %s]\r\n", yesNo(dtr), yesNo(rts));
        }
        if (c < 0) {
            delay(1);
        }
    }
    hw::uartEnd();
    hw::lineSet(OutSignal::kCTS, false);
    hw::lineSet(OutSignal::kDCD, false);
    Serial.print("\r\nBridge closed\r\n");
}

// --- Guided test -------------------------------------------------------------------------

enum class Step : uint8_t { kPass, kFail, kSkip };

const char *stepName(Step s) {
    return s == Step::kPass ? "PASS" : s == Step::kFail ? "FAIL" : "skipped";
}

// Returns false to quit.
bool prompt(const char *what, bool *skip) {
    Serial.printf("\r\n%s\r\nEnter = start, S = skip, Q = quit: ", what);
    int c = waitKey();
    Serial.print("\r\n");
    *skip = c == 'S';
    return c != 'Q';
}

Step askYesNo(const char *question) {
    Serial.printf("%s (Y/N/S): ", question);
    for (;;) {
        int c = waitKey();
        if (c == 'Y' || c == 'N' || c == 'S') {
            Serial.printf("%c\r\n", c);
            return c == 'Y' ? Step::kPass : c == 'N' ? Step::kFail : Step::kSkip;
        }
    }
}

void runGuided() {
    static const char *const kNames[] = {"LEDs", "Speaker",     "Volume cap",
                                         "Pot",  "RS232 lines", "Loopback"};
    constexpr int kSteps = 6;
    Step result[kSteps] = {Step::kSkip, Step::kSkip, Step::kSkip,
                           Step::kSkip, Step::kSkip, Step::kSkip};
    bool skip = false;
    Serial.print("Guided hardware test. Each step says what to look or listen for.\r\n\r\n");
    printPins();
    drainInput();

    do {
        if (!prompt("1/6 LEDs: each LED lights for 1 s with its name. Press a key when done.",
                    &skip)) {
            break;
        }
        if (!skip) {
            ledWalk();
            result[0] = askYesNo("Did each LED light, in the right window, with the right name?");
        }

        if (!prompt("2/6 Speaker: 1 kHz, sweep, UK dial tone, DTMF, pulse clicks, busy tone.",
                    &skip)) {
            break;
        }
        if (!skip) {
            printVolume();
            tones::ToneSource t1(1000.0f, 0.0f, 1000);
            tones::SweepSource sw(200.0f, 4000.0f, 4000);
            tones::ToneSource dial(tones::kUkDialTone, 2000);
            tones::DtmfSource dtmf("0123456789*#");
            tones::PulseSource pulse("21");
            tones::ToneSource busy(tones::kUkBusyTone, 2250);
            tones::Source *seq[] = {&t1, &sw, &dial, &dtmf, &pulse, &busy};
            for (tones::Source *s : seq) {
                if (!play(*s)) {
                    break;
                }
            }
            result[1] = askYesNo("Did you hear all six sounds clearly?");
        }

        if (!prompt("3/6 Volume cap: 1 kHz then a sweep at the maximum the firmware allows.",
                    &skip)) {
            break;
        }
        if (!skip) {
            uint32_t savedPct = testVolumePct;
            bool savedPot = testVolumeFromPot;
            testVolumePct = 100;
            testVolumeFromPot = false;
            printVolume();
            tones::ToneSource t(1000.0f, 0.0f, 3000);
            tones::SweepSource sw(200.0f, 4000.0f, 6000);
            if (play(t)) {
                play(sw);
            }
            testVolumePct = savedPct;
            testVolumeFromPot = savedPot;
            result[2] = askYesNo("Clean at full volume: no distortion, buzz or rattle?");
        }

        if (!prompt("4/6 Pot: turn the knob slowly from one end to the other and back.", &skip)) {
            break;
        }
        if (!skip) {
            bool full = runPot(true);
            Step heard = askYesNo("Did the tone go smoothly from silent to loud?");
            result[3] = full && heard == Step::kPass ? Step::kPass
                        : heard == Step::kSkip       ? Step::kSkip
                                                     : Step::kFail;
            if (!full) {
                Serial.print("The pot didn't reach both ends (0% and 100%): check its wiring\r\n");
            }
        }

        if (!prompt("5/6 RS232 lines: connect the PC, open a terminal on its COM port and watch\r\n"
                    "its DTR/RTS/CD/RI/CTS indicators while you toggle them here.",
                    &skip)) {
            break;
        }
        if (!skip) {
            runLines();
            result[4] =
                askYesNo("Did DTR and RTS follow the terminal, and DCD, RI, CTS show there?");
        }

        if (!prompt("6/6 Loopback: unplug the PC cable and fit the loopback plug\r\n"
                    "(DB25 3-2, 5-4, 8-20; bench DB9 2-3, 8-7, 1-4).",
                    &skip)) {
            break;
        }
        if (!skip) {
            result[5] = runLoop("ALL", true) ? Step::kPass : Step::kFail;
        }
    } while (false);

    hw::ledsSet(0);
    Serial.print("\r\nSummary\r\n");
    for (int i = 0; i < kSteps; ++i) {
        Serial.printf("  %d %-12s %s\r\n", i + 1, kNames[i], stepName(result[i]));
    }
}

// --- Help and dispatch -------------------------------------------------------------------

struct Command {
    const char *usage;
    const char *help;
};

const Command kCommands[] = {
    {"AT*HW?", "list the hardware-test commands"},
    {"AT*HWTEST", "guided test of LEDs, speaker, volume cap, pot, RS232 lines, loopback"},
    {"AT*HWPINS", "show the GPIO pin map"},
    {"AT*HWINFO", "chip, memory, firmware, reset reason, LittleFS"},
    {"AT*HWFILES", "list the files in LittleFS (sounds and web page)"},
    {"AT*HWLED=<AA|CD|RD|SD|TR|CS|ARQ|ALL|OFF|CHASE|WALK>", "light LEDs"},
    {"AT*HWAMP=<AUTO|ON|OFF>", "amp shutdown pin (AUTO: on only while playing)"},
    {"AT*HWVOL=<0-100|POT>", "test volume, % of the speaker cap (default 75)"},
    {"AT*HWTONE=<Hz>[,<ms>] | DIAL | BUSY | RING | SWEEP", "play a tone"},
    {"AT*HWDTMF=<digits>", "play DTMF (default 0-9 * #)"},
    {"AT*HWPULSE=<digits>", "play UK pulse-dial clicks"},
    {"AT*HWPOT[=TONE]", "live pot reading (TONE: a 1 kHz tone follows it)"},
    {"AT*HWLINES", "live DTR/RTS, toggle DCD/RI/CTS"},
    {"AT*HWSIG=<DCD|RI|CTS>,<ON|OFF>", "set one RS232 output (for a meter)"},
    {"AT*HWLOOP[=<baud>|ALL]", "loopback test (needs a loopback plug)"},
    {"AT*HWBRIDGE[=<baud>]", "console <-> RS232 pass-through (default 38400, Ctrl-X ends)"},
};

void printCommands() {
    for (const Command &c : kCommands) {
        Serial.printf("  %-52s %s\r\n", c.usage, c.help);
    }
}

}  // namespace

void printHelp() {
    printCommands();
}

const char *resetReasonName(int reason) {
    switch (static_cast<esp_reset_reason_t>(reason)) {
        case ESP_RST_POWERON:
            return "power-on";
        case ESP_RST_EXT:
            return "external pin";
        case ESP_RST_SW:
            return "software restart";
        case ESP_RST_PANIC:
            return "crash (panic)";
        case ESP_RST_INT_WDT:
            return "interrupt watchdog";
        case ESP_RST_TASK_WDT:
            return "task watchdog";
        case ESP_RST_WDT:
            return "other watchdog";
        case ESP_RST_DEEPSLEEP:
            return "wake from deep sleep";
        case ESP_RST_BROWNOUT:
            return "brown-out (supply voltage dipped)";
        case ESP_RST_SDIO:
            return "SDIO";
        case ESP_RST_UNKNOWN:
        default:
            return "unknown";
    }
}

void handle(const char *rest) {
    // Command word: letters after "AT*HW"
    char name[8] = {};
    size_t n = 0;
    while (isalpha(static_cast<unsigned char>(rest[n])) && n < sizeof name - 1) {
        name[n] = static_cast<char>(toupper(static_cast<unsigned char>(rest[n])));
        ++n;
    }
    if (isalpha(static_cast<unsigned char>(rest[n]))) {
        error();  // too long to be a command
        return;
    }
    const char *arg = argStart(rest + n);
    bool good = true;

    if (strcmp(name, "") == 0) {
        if (*arg != '?' && *arg != '\0') {
            good = false;
        } else {
            printCommands();
        }
    } else if (strcmp(name, "TEST") == 0) {
        runGuided();
    } else if (strcmp(name, "PINS") == 0) {
        printPins();
    } else if (strcmp(name, "INFO") == 0) {
        printInfo();
    } else if (strcmp(name, "FILES") == 0) {
        good = printFiles();
    } else if (strcmp(name, "LED") == 0) {
        good = cmdLed(arg);
    } else if (strcmp(name, "AMP") == 0) {
        good = cmdAmp(arg);
    } else if (strcmp(name, "VOL") == 0) {
        good = cmdVol(arg);
    } else if (strcmp(name, "TONE") == 0) {
        good = cmdTone(arg);
    } else if (strcmp(name, "DTMF") == 0) {
        good = cmdDtmf(arg);
    } else if (strcmp(name, "PULSE") == 0) {
        good = cmdPulse(arg);
    } else if (strcmp(name, "POT") == 0) {
        if (wordIs(arg, "TONE")) {
            runPot(true);
        } else if (*arg == '\0') {
            runPot(false);
        } else {
            good = false;
        }
    } else if (strcmp(name, "LINES") == 0) {
        runLines();
    } else if (strcmp(name, "SIG") == 0) {
        good = cmdSig(arg);
    } else if (strcmp(name, "LOOP") == 0) {
        good = runLoop(arg);
    } else if (strcmp(name, "BRIDGE") == 0) {
        uint32_t baud = 38400;
        if (*arg != '\0' && !hwtest::parseUint(arg, 300, 921600, &baud)) {
            good = false;
        } else {
            runBridge(baud);
        }
    } else {
        good = false;
    }
    good ? ok() : error();
}

}  // namespace hwconsole
