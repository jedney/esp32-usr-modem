// AT*HW... hardware-test commands (spec 3.13). They exist to check the
// electronics as they are built: each LED, the amplifier and speaker, the volume pot,
// the RS232 handshake lines and data path. Pins come from lib/hw/src/pins.h.
#include "hwconsole.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <esp_system.h>

#include <cstdarg>
#include <cstdio>
#include <cstring>

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

// Test volume: percent of full volume on the audio curve, or follow the pot.
uint32_t testVolumePct = 75;
bool testVolumeFromPot = false;

// --- Console helpers ---------------------------------------------------------------------

Term *term = nullptr;  // the port running the current command
PortControl control = {};
bool portTaken = false;  // the RS232 session is suspended for a test
// What a test leaves in place after it ends, and which kind of port set it (so a browser
// that leaves cancels only its own).
hwtest::Hold sigHold;  // AT*HWSIG keeps the port until AT*HWSIG=AUTO
hwtest::Hold ledHold;  // AT*HWLED keeps the LEDs until AT*HWLED=AUTO
hwtest::Hold ampHold;  // AT*HWAMP=ON/OFF keeps the amp mode until AT*HWAMP=AUTO

uint32_t lastKeyMs = 0;  // last key seen by the running test
bool timedOut = false;   // the running test hit hwtest::kIdleTimeoutMs

Term &out() {
    return *term;
}

bool fromBrowser() {
    return term->isBrowser();
}
void testDelay(uint32_t ms) {
    uint32_t start = millis();
    do {
        hw::feedWatchdog();  // a test can run for minutes inside one loop pass
        term->service();
        ::delay(ms < 5 ? ms : 5);
    } while (millis() - start < ms);
}

// Throws away what's left of the command line (terminals send CR LF) so a test that stops
// "on any key" doesn't stop at once. On the RS232 port, keys arrive in bursts after the
// line has been idle for 32 ms, so wait a little longer there.
void drainInput() {
    testDelay(term->isRs232() ? 60 : 30);
    while (term->connected() && term->read() >= 0) {
    }
}

int readKey() {
    hw::feedWatchdog();  // every test loop reads keys, even while keys keep coming
    int c = term->read();
    if (c >= 0) {
        lastKeyMs = millis();
    }
    return c;
}

// True when the running test must end: the terminal has gone, or no key came for
// hwtest::kIdleTimeoutMs. Every "any key" loop checks this.
bool mustStop() {
    if (!term->connected()) {
        return true;
    }
    if (!timedOut && hwtest::idleExpired(lastKeyMs, millis())) {
        timedOut = true;
        out().printf("\r\nStopped: no key for %u minutes\r\n",
                     static_cast<unsigned>(hwtest::kIdleTimeoutMs / 60000u));
    }
    return timedOut;
}

// For loops that stop on any key (and so on a closed terminal or the idle time-out).
bool keyPressed() {
    if (mustStop()) {
        return true;
    }
    if (readKey() < 0) {
        return false;
    }
    drainInput();
    return true;
}

// The RS232 line, loopback and bridge tests use the port itself: they run only on
// the USB console, with no call up, and the RS232 session is suspended meanwhile.
bool canTakePort(const char *what) {
    if (term->isRs232()) {
        out().printf(
            "%s: not available on the RS232 port, because the test uses the port\r\n"
            "itself. Run it on the USB console.\r\n",
            what);
        return false;
    }
    if (control.idle != nullptr && !control.idle()) {
        out().printf("%s: not available during a call. Hang up first (ATH).\r\n", what);
        return false;
    }
    return true;
}

bool takePort(const char *what) {
    if (!canTakePort(what)) {
        return false;
    }
    if (!portTaken && control.suspend != nullptr) {
        control.suspend();
    }
    portTaken = true;
    return true;
}

void givePort() {
    if (portTaken && !sigHold.active() && control.resume != nullptr) {
        control.resume();
        portTaken = false;
    }
}

int waitKey() {
    int c;
    while ((c = readKey()) < 0) {
        if (mustStop()) {
            return 'Q';
        }
        testDelay(5);
    }
    if (!term->connected()) {
        return 'Q';
    }
    drainInput();
    return toupper(c);
}

void ok() {
    out().print("OK\r\n");
}
void error() {
    out().print("ERROR\r\n");
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
        out().print("Test volume: follows the pot");
    } else {
        out().printf("Test volume: %u%%", static_cast<unsigned>(testVolumePct));
    }
    float g = volume::outputGain(testGain());
    if (g > 0.0f) {
        out().printf(" (%.1f dB below full scale)\r\n", static_cast<double>(-20.0f * log10f(g)));
    } else {
        out().print(" (muted)\r\n");
    }
}

bool play(tones::Source &src) {
    out().print("Playing (any key stops)...\r\n");
    drainInput();
    if (!hw::audioPlay(src, testGain, keyPressed)) {
        if (!hw::audioReady()) {
            out().print("I2S driver failed to start\r\n");
            return false;
        }
        out().print("Stopped\r\n");
    }
    return true;
}

// --- Pin map -----------------------------------------------------------------------------

void printPins() {
    out().print("GPIO pin map (lib/hw/src/pins.h) - confirmed\r\n");
    out().print("RS232 (DCE; asserted = LOW at the GPIO):\r\n");
    out().printf("  RXD out  GPIO%-2d  DB9 2   UART1 TX\r\n", pins::kRxd);
    out().printf("  TXD in   GPIO%-2d  DB9 3   UART1 RX\r\n", pins::kTxd);
    out().printf("  CTS out  GPIO%-2d  DB9 8\r\n", pins::kCts);
    out().printf("  RTS in   GPIO%-2d  DB9 7\r\n", pins::kRts);
    out().printf("  DTR in   GPIO%-2d  DB9 4\r\n", pins::kDtr);
    out().printf("  DCD out  GPIO%-2d  DB9 1\r\n", pins::kDcd);
    out().printf("  RI  out  GPIO%-2d  DB9 9\r\n", pins::kRi);
    out().print("  DSR      hard-wired on, DB9 6\r\n");
    out().printf("Audio: I2S BCLK GPIO%d, LRC GPIO%d, DIN GPIO%d, amp SD GPIO%d (HIGH = on)\r\n",
                 pins::kI2sBclk, pins::kI2sLrc, pins::kI2sDin, pins::kAmpSd);
    out().printf("Volume pot wiper: GPIO%d (ADC1)\r\n", pins::kPot);
    out().printf("LEDs (%s = lit):", pins::kLedActiveHigh ? "HIGH" : "LOW");
    for (uint8_t i = 0; i < hwtest::kLedCount; ++i) {
        out().printf(" %s=GPIO%d", hwtest::ledName(i), pins::kLeds[i]);
    }
    out().printf("\r\nOn board: console UART0 GPIO%d/%d\r\n", pins::kConsoleTx, pins::kConsoleRx);
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
            out().printf("  %-40s %8u\r\n", f.path(), static_cast<unsigned>(f.size()));
            ++*count;
        }
    }
}

void printInfo() {
    out().printf("Firmware   v%s (%s)\r\n", FW_VERSION, FW_COMMIT);
    out().printf("Chip       %s rev %d, %d cores, %u MHz\r\n", ESP.getChipModel(),
                 static_cast<int>(ESP.getChipRevision()), static_cast<int>(ESP.getChipCores()),
                 static_cast<unsigned>(ESP.getCpuFreqMHz()));
    uint64_t mac = ESP.getEfuseMac();
    out().printf(
        "MAC        %02X:%02X:%02X:%02X:%02X:%02X\r\n", static_cast<unsigned>(mac & 0xFF),
        static_cast<unsigned>((mac >> 8) & 0xFF), static_cast<unsigned>((mac >> 16) & 0xFF),
        static_cast<unsigned>((mac >> 24) & 0xFF), static_cast<unsigned>((mac >> 32) & 0xFF),
        static_cast<unsigned>((mac >> 40) & 0xFF));
    out().printf("Flash      %u MB at %u MHz; firmware %u KB, OTA slot %u KB free\r\n",
                 static_cast<unsigned>(ESP.getFlashChipSize() / (1024 * 1024)),
                 static_cast<unsigned>(ESP.getFlashChipSpeed() / 1000000),
                 static_cast<unsigned>(ESP.getSketchSize() / 1024),
                 static_cast<unsigned>(ESP.getFreeSketchSpace() / 1024));
    out().printf("PSRAM      %u KB, %u KB free\r\n",
                 static_cast<unsigned>(ESP.getPsramSize() / 1024),
                 static_cast<unsigned>(ESP.getFreePsram() / 1024));
    out().printf("Heap       %u KB, %u KB free\r\n",
                 static_cast<unsigned>(ESP.getHeapSize() / 1024),
                 static_cast<unsigned>(ESP.getFreeHeap() / 1024));
    out().printf("Reset      %s\r\n", resetReasonName(static_cast<int>(esp_reset_reason())));
    out().printf("Uptime     %lu s\r\n", static_cast<unsigned long>(millis() / 1000));
    if (mountFs()) {
        out().printf("LittleFS   %u KB used of %u KB\r\n",
                     static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                     static_cast<unsigned>(LittleFS.totalBytes() / 1024));
    } else {
        out().print("LittleFS   not mounted: upload the sounds and web files image\r\n");
    }
}

bool printFiles() {
    if (!mountFs()) {
        out().print("LittleFS not mounted: upload the sounds and web files image\r\n");
        return false;
    }
    int count = 0;
    listDir("/", &count);
    out().printf("%d files, %u KB used of %u KB\r\n", count,
                 static_cast<unsigned>(LittleFS.usedBytes() / 1024),
                 static_cast<unsigned>(LittleFS.totalBytes() / 1024));
    return true;
}

// --- LEDs --------------------------------------------------------------------------------

void ledChase() {
    out().print("Chase (any key stops)\r\n");
    drainInput();
    ledHold.take(fromBrowser());
    for (uint32_t step = 0; !keyPressed(); ++step) {
        hw::ledsSet(hwtest::chaseMask(step));
        testDelay(120);
    }
    hw::ledsSet(0);
    ledHold.release();  // back to the modem's own LED states
}

void ledWalk() {
    out().print("Each LED for 1 s, left to right (any key stops)\r\n");
    drainInput();
    ledHold.take(fromBrowser());
    for (uint8_t i = 0;; i = static_cast<uint8_t>((i + 1) % hwtest::kLedCount)) {
        hw::ledsSet(static_cast<uint8_t>(1u << i));
        out().printf("  %-3s GPIO%d\r\n", hwtest::ledName(i), pins::kLeds[i]);
        for (int t = 0; t < 100; ++t) {
            if (keyPressed()) {
                hw::ledsSet(0);
                ledHold.release();
                return;
            }
            testDelay(10);
        }
    }
}

bool cmdLed(const char *arg) {
    int led = hwtest::parseLed(arg);
    if (led >= 0) {
        ledHold.take(fromBrowser());
        hw::ledsSet(static_cast<uint8_t>(1u << led));
        out().printf("%s lit (GPIO%d). AT*HWLED=AUTO gives the LEDs back to the modem.\r\n",
                     hwtest::ledName(static_cast<uint8_t>(led)), pins::kLeds[led]);
    } else if (wordIs(arg, "ALL")) {
        ledHold.take(fromBrowser());
        hw::ledsSet(0x7F);
        out().print("All LEDs lit. AT*HWLED=AUTO gives the LEDs back to the modem.\r\n");
    } else if (wordIs(arg, "OFF") || wordIs(arg, "NONE")) {
        ledHold.take(fromBrowser());
        hw::ledsSet(0);
    } else if (wordIs(arg, "AUTO")) {
        ledHold.release();
        out().print("LEDs show the modem's state\r\n");
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
        ampHold.take(fromBrowser());
    } else if (wordIs(arg, "OFF")) {
        hw::ampSetMode(hw::AmpMode::kOff);
        ampHold.take(fromBrowser());
    } else if (wordIs(arg, "AUTO")) {
        hw::ampSetMode(hw::AmpMode::kAuto);
        ampHold.release();
    } else if (*arg != '?' && *arg != '\0') {
        return false;
    }
    const char *mode = hw::ampMode() == hw::AmpMode::kOn    ? "ON"
                       : hw::ampMode() == hw::AmpMode::kOff ? "OFF (sounds muted; I2S still runs)"
                                                            : "AUTO (on only while a sound plays)";
    out().printf("Amp %s, SD pin GPIO%d is %s\r\n", mode, pins::kAmpSd,
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
        out().print("Sweep 200 Hz to 4 kHz over 6 s\r\n");
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
    out().printf("%u Hz for %u ms\r\n", static_cast<unsigned>(hz), static_cast<unsigned>(ms));
    tones::ToneSource src(static_cast<float>(hz), 0.0f, ms);
    return play(src);
}

bool cmdDtmf(const char *arg) {
    if (*arg == '\0') {
        arg = "0123456789*#";
    }
    out().printf("DTMF %s\r\n", arg);
    tones::DtmfSource src(arg);
    return play(src);
}

bool cmdPulse(const char *arg) {
    if (*arg == '\0') {
        arg = "0123456789";
    }
    out().printf("Pulse dialling (UK, 10 pps) %s\r\n", arg);
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
    out().printf("\r  raw %4u  %4lu mV  level %3d%%  %s   ", static_cast<unsigned>(raw),
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
    out().printf("Pot on GPIO%d. Turn it end to end%s (any key stops)\r\n", pins::kPot,
                 withTone ? "; a 1 kHz tone follows it" : "");
    drainInput();
    if (withTone) {
        tones::ToneSource src(1000.0f, 0.0f, 0);
        hw::audioPlay(src, potGain, potStop);
    } else {
        while (!potStop()) {
            testDelay(10);
        }
    }
    float lo = volume::potLevel(potMinSeen, hw::kPotMax);
    float hi = volume::potLevel(potMaxSeen, hw::kPotMax);
    out().printf("\r\nRange seen: raw %u to %u (level %d%% to %d%%)\r\n",
                 static_cast<unsigned>(potMinSeen), static_cast<unsigned>(potMaxSeen),
                 static_cast<int>(lo * 100.0f + 0.5f), static_cast<int>(hi * 100.0f + 0.5f));
    return lo <= 0.0f && hi >= 1.0f;
}

// --- RS232 lines -------------------------------------------------------------------------

void printLines() {
    using hwtest::OutSignal;
    out().printf(
        "\r  in: DTR %s  RTS %s   out: DCD %s  RI %s  CTS %s  (DSR on)  ", yesNo(hw::dtrAsserted()),
        yesNo(hw::rtsAsserted()), yesNo(hw::lineIsAsserted(OutSignal::kDCD)),
        yesNo(hw::lineIsAsserted(OutSignal::kRI)), yesNo(hw::lineIsAsserted(OutSignal::kCTS)));
}

void runLines() {
    using hwtest::OutSignal;
    hw::linesBegin();
    out().print("Live RS232 handshake lines. In: from the computer. Out: from the modem.\r\n");
    out().print("Keys: D = toggle DCD, R = toggle RI, C = toggle CTS, Q = quit\r\n");
    drainInput();
    uint32_t last = 0;
    for (;;) {
        int c = toupper(readKey());
        if (c < 0 && mustStop()) {
            break;
        }
        if (c == 'D' || c == 'R' || c == 'C') {
            OutSignal s = c == 'D' ? OutSignal::kDCD : c == 'R' ? OutSignal::kRI : OutSignal::kCTS;
            hw::lineSet(s, !hw::lineIsAsserted(s));
            last = 0;
        } else if (c == 'Q' || c == kEsc || c == kCtrlX || c == '\r' || c == '\n') {
            break;
        }
        if (millis() - last >= 100) {
            last = millis();
            printLines();
        }
        testDelay(5);
    }
    for (int i = 0; i < 3; ++i) {
        hw::lineSet(static_cast<OutSignal>(i), false);
    }
    out().print("\r\nOutputs set back to off\r\n");
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
    out().printf("%s %s (stays until changed, AT*HWSIG=AUTO or AT*HWLINES)\r\n",
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
    testDelay(20);
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
            testDelay(1);
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
        testDelay(5);
        bool on = in();
        hw::lineSet(out, false);
        testDelay(5);
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
    uint32_t one[1] = {115200};
    const uint32_t *bauds = one;
    size_t n = 1;
    if (wordIs(arg, "ALL")) {
        bauds = kAll;
        n = sizeof kAll / sizeof kAll[0];
    } else if (*arg != '\0' && !hwtest::parseUint(arg, 300, 921600, &one[0])) {
        return false;
    }
    if (!quiet) {
        out().print("Loopback plug needed: RXD-TXD, CTS-RTS, DCD-DTR\r\n");
        out().print("  (DB25 3-2, 5-4, 8-20; bench DB9 2-3, 8-7, 1-4)\r\n");
    }
    bool all = true;
    for (size_t i = 0; i < n; ++i) {
        LoopResult r = loopbackAt(bauds[i]);
        out().printf("  %6lu baud: data %s (%u/%u bytes, %u wrong)  CTS->RTS %s  DCD->DTR %s\r\n",
                     static_cast<unsigned long>(bauds[i]), passFail(r.data),
                     static_cast<unsigned>(r.received), static_cast<unsigned>(r.sent),
                     static_cast<unsigned>(r.errors), passFail(r.cts), passFail(r.dcd));
        all = all && r.data && r.cts && r.dcd;
    }
    out().print(
        "  RI has no input to loop back to: check it with AT*HWLINES and the\r\n"
        "  terminal's RI light, or AT*HWSIG=RI,ON and a meter on DB25 22\r\n");
    out().printf("Loopback %s\r\n", passFail(all));
    return all;
}

void runBridge(uint32_t baud) {
    using hwtest::OutSignal;
    hw::linesBegin();
    hw::uartBegin(baud);
    hw::lineSet(OutSignal::kCTS, true);  // so terminals using RTS/CTS will send
    hw::lineSet(OutSignal::kDCD, true);
    out().printf("Bridge: console <-> RS232 at %lu 8N1, CTS and DCD on. Ctrl-X ends.\r\n",
                 static_cast<unsigned long>(baud));
    drainInput();
    HardwareSerial &u = hw::uart();
    bool dtr = hw::dtrAsserted();
    bool rts = hw::rtsAsserted();
    out().printf("[DTR %s, RTS %s]\r\n", yesNo(dtr), yesNo(rts));
    for (;;) {
        int c = readKey();
        if (c == kCtrlX || (c < 0 && mustStop())) {
            break;
        }
        if (c >= 0) {
            u.write(static_cast<uint8_t>(c));
        }
        while (u.available() > 0) {
            char ch = static_cast<char>(u.read());
            out().write(&ch, 1);
            lastKeyMs = millis();  // traffic from the RS232 side counts as activity too
        }
        if (hw::dtrAsserted() != dtr || hw::rtsAsserted() != rts) {
            dtr = hw::dtrAsserted();
            rts = hw::rtsAsserted();
            out().printf("\r\n[DTR %s, RTS %s]\r\n", yesNo(dtr), yesNo(rts));
        }
        if (c < 0) {
            testDelay(1);
        }
    }
    hw::uartEnd();
    hw::lineSet(OutSignal::kCTS, false);
    hw::lineSet(OutSignal::kDCD, false);
    out().print("\r\nBridge closed\r\n");
}

// --- Guided test -------------------------------------------------------------------------

enum class Step : uint8_t { kPass, kFail, kSkip };

const char *stepName(Step s) {
    return s == Step::kPass ? "PASS" : s == Step::kFail ? "FAIL" : "skipped";
}

// Returns false to quit.
bool prompt(const char *what, bool *skip) {
    out().printf("\r\n%s\r\nEnter = start, S = skip, Q = quit: ", what);
    int c = waitKey();
    out().print("\r\n");
    *skip = c == 'S';
    return c != 'Q';
}

Step askYesNo(const char *question) {
    out().printf("%s (Y/N/S): ", question);
    for (;;) {
        int c = waitKey();
        if (!term->connected() || timedOut) {
            return Step::kSkip;
        }
        if (c == 'Y' || c == 'N' || c == 'S') {
            out().printf("%c\r\n", c);
            return c == 'Y' ? Step::kPass : c == 'N' ? Step::kFail : Step::kSkip;
        }
    }
}

void runGuided() {
    static const char *const kNames[] = {"LEDs", "Speaker",     "Full volume",
                                         "Pot",  "RS232 lines", "Loopback"};
    constexpr int kSteps = 6;
    Step result[kSteps] = {Step::kSkip, Step::kSkip, Step::kSkip,
                           Step::kSkip, Step::kSkip, Step::kSkip};
    bool skip = false;
    out().print("Guided hardware test. Each step says what to look or listen for.\r\n\r\n");
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

        if (!prompt("3/6 Full volume: 1 kHz then a sweep at the highest volume.", &skip)) {
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
                out().print("The pot didn't reach both ends (0% and 100%): check its wiring\r\n");
            }
        }

        if (!takePort("\r\n5/6 RS232 lines and 6/6 Loopback")) {
            break;  // listed as skipped
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

    givePort();
    hw::ledsSet(0);
    out().print("\r\nSummary\r\n");
    for (int i = 0; i < kSteps; ++i) {
        out().printf("  %d %-12s %s\r\n", i + 1, kNames[i], stepName(result[i]));
    }
}

// --- Help and dispatch -------------------------------------------------------------------

struct Command {
    const char *usage;
    const char *help;
};

const Command kCommands[] = {
    {"AT*HW?", "list the hardware-test commands"},
    {"AT*HWTEST", "guided test of LEDs, speaker, full volume, pot, RS232 lines, loopback"},
    {"AT*HWPINS", "show the GPIO pin map"},
    {"AT*HWINFO", "chip, memory, firmware, reset reason, LittleFS"},
    {"AT*HWFILES", "list the files in LittleFS (sounds and web page)"},
    {"AT*HWLED=<AA|CD|RD|SD|TR|CS|ARQ|ALL|OFF|CHASE|WALK>", "light LEDs"},
    {"AT*HWLED=AUTO", "LEDs back to showing the modem's state"},
    {"AT*HWAMP=<AUTO|ON|OFF>", "amp shutdown pin (AUTO: on only while playing)"},
    {"AT*HWVOL=<0-100|POT>", "test volume, % of full volume (default 75)"},
    {"AT*HWTONE=<Hz>[,<ms>] | DIAL | BUSY | RING | SWEEP", "play a tone"},
    {"AT*HWDTMF=<digits>", "play DTMF (default 0-9 * #)"},
    {"AT*HWPULSE=<digits>", "play UK pulse-dial clicks"},
    {"AT*HWPOT[=TONE]", "live pot reading (TONE: a 1 kHz tone follows it)"},
    {"AT*HWLINES", "live DTR/RTS, toggle DCD/RI/CTS"},
    {"AT*HWSIG=<DCD|RI|CTS>,<ON|OFF>", "set one RS232 output (for a meter)"},
    {"AT*HWSIG=AUTO", "RS232 outputs back to the modem (port in use again)"},
    {"AT*HWLOOP[=<baud>|ALL]", "loopback test (needs a loopback plug)"},
    {"AT*HWBRIDGE[=<baud>]", "console <-> RS232 pass-through (default 38400, Ctrl-X ends)"},
};

void printCommands() {
    for (const Command &c : kCommands) {
        out().printf("  %-52s %s\r\n", c.usage, c.help);
    }
}

}  // namespace

void Term::print(const char *s) {
    write(s, strlen(s));
}

void Term::printf(const char *fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0) {
        write(buf, static_cast<size_t>(n) < sizeof buf ? static_cast<size_t>(n) : sizeof buf - 1);
    }
}

void setPortControl(const PortControl &pc) {
    control = pc;
}

bool ledsHeld() {
    return ledHold.active();
}

void printHelp(Term &t) {
    term = &t;
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
            return "task watchdog (the modem stopped for 15 s and restarted)";
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

void handle(Term &t, const char *rest) {
    term = &t;
    timedOut = false;
    lastKeyMs = millis();
    // Command word: letters after "AT*HW"
    char name[8] = {};
    size_t n = 0;
    while (n < sizeof name - 1 && isalpha(static_cast<unsigned char>(rest[n]))) {
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
        good = takePort("AT*HWLINES");
        if (good) {
            runLines();  // sets the outputs back to off
            sigHold.release();
            givePort();
        }
    } else if (strcmp(name, "SIG") == 0) {
        if (wordIs(arg, "AUTO")) {
            sigHold.release();
            givePort();
            out().print("RS232 outputs back to the modem\r\n");
        } else if ((good = takePort("AT*HWSIG"))) {
            good = cmdSig(arg);
            if (good) {
                sigHold.take(fromBrowser());
                out().print("The RS232 port is paused until AT*HWSIG=AUTO\r\n");
            } else {
                sigHold.release();
                givePort();
            }
        }
    } else if (strcmp(name, "LOOP") == 0) {
        good = takePort("AT*HWLOOP");
        if (good) {
            good = runLoop(arg);
            givePort();
        }
    } else if (strcmp(name, "BRIDGE") == 0) {
        uint32_t baud = 38400;
        if (*arg != '\0' && !hwtest::parseUint(arg, 300, 921600, &baud)) {
            good = false;
        } else if ((good = takePort("AT*HWBRIDGE"))) {
            runBridge(baud);
            givePort();
        }
    } else {
        good = false;
    }
    good ? ok() : error();
}

void restoreBrowserResources() {
    // Only what the browser set itself: a hold from the console, USB or RS232 stays.
    if (sigHold.releaseForBrowser()) {
        givePort();
    }
    if (ledHold.releaseForBrowser()) {
        hw::ledsSet(0);
    }
    if (ampHold.releaseForBrowser()) {
        hw::ampSetMode(hw::AmpMode::kAuto);
    }
}
}  // namespace hwconsole
