// The RS232 host port: see rs232.h.
//
// Where the computer's bytes come from:
//   - command mode, speed from "AT": the RMT capture (lib/hostport autobaud), so a new
//     speed is followed from the very first "AT"; the UART's copy is thrown away;
//   - online, or a fixed AT*BAUD speed: the UART, with hardware flow control.
#include "rs232.h"

#include <Arduino.h>

#include "autobaud.h"
#include "hw.h"
#include "linectl.h"

namespace rs232 {

namespace {

using hostport::Mode;

class UartSink : public atcmd::Sink {
public:
    void write(const char *data, size_t len) override {
        if (!hw::hostActive()) {
            return;
        }
        size_t done = 0;
        const uint32_t start = millis();
        while (done < len) {
            if (millis() - start > 2000)
                return;
            int room = hw::hostWriteSpace();
            if (room > 0) {
                size_t n = len - done < static_cast<size_t>(room) ? len - done : room;
                size_t written = hw::hostWrite(reinterpret_cast<const uint8_t *>(data + done), n);
                done += written;
                toComputer += written;
            } else if (millis() - start > 2000) {
                return;  // the computer has held RTS off for 2 s: drop the rest
            } else {
                delay(1);
            }
        }
    }
    uint32_t toComputer = 0;
};

constexpr size_t kMaxRuns = 2048;  // a full command line at 10 edges per character
autobaud::Run *runs = nullptr;     // in PSRAM

atcmd::Modem *modemPtr = nullptr;
UartSink sink;
hostport::Session *sess = nullptr;  // made once at boot

uint32_t currentBaud = modemcfg::kFactoryBaud;
uint32_t fromComputer = 0;
bool isSuspended = false;
bool flowKnown = false;
hostport::FlowMode flow = {};
hostport::XoffSender xoff;
bool dcdSet = false;
bool dcdKnown = false;

// Bytes decoded for a hardware test running on this port (readKey).
uint8_t keys[64];
size_t keyCount = 0;

const modemcfg::Profile &profile() {
    return modemPtr->active();
}

void setBaud(uint32_t b) {
    currentBaud = b;
    hw::hostSetBaud(b);
    sess->port().setBaud(b);
}

void flushUart() {
    HardwareSerial &u = hw::uart();
    while (u.available() > 0) {
        u.read();
    }
}

class CallBaud : public hostport::Session::BaudControl {
public:
    void prepareOnline() override {
        // UartSink writes CONNECT directly: stop capture and discard the UART
        // copy of the AT line before the peer can respond with binary bytes.
        if (hw::captureActive()) {
            hw::captureEnd();
            flushUart();
        }
    }
    bool online(uint32_t speed) override {
        if (modemPtr->hostBaud() || profile().get(modemcfg::kAmpB) == 1) {
            return true;
        }
        commandBaud_ = currentBaud;
        if (!hw::hostDrain(2000)) {
            return false;  // CONNECT cannot leave while the computer holds RTS off
        }
        setBaud(speed);
        following_ = true;
        return true;
    }
    void command() override {
        if (following_) {
            hw::hostDrain(2000);
            setBaud(commandBaud_);
            following_ = false;
        }
        // Be ready for a new AT before Session drops DCD. Waiting for the next
        // poll can lose an immediately redialing host's first character.
        if (detecting() && !hw::captureActive()) {
            flushUart();
            hw::captureBegin();
        }
    }

private:
    uint32_t commandBaud_ = modemcfg::kFactoryBaud;
    bool following_ = false;
};
CallBaud callBaud;

void feed(const uint8_t *data, size_t len, uint32_t nowMs) {
    fromComputer += len;
    sess->fromHost(data, len, nowMs);
}

uint32_t startBaud() {
    if (modemPtr->hostBaud() != 0) {
        return modemPtr->hostBaud();
    }
    uint32_t b = profile().baud;  // the power-on template's speed, until "AT" arrives
    return modemcfg::isHostRate(b) ? b : modemcfg::kFactoryBaud;
}

void startPort() {
    currentBaud = startBaud();
    hw::hostBegin(currentBaud);
    sess->port().setBaud(currentBaud);
    flowKnown = false;
    dcdKnown = false;
    xoff.reset();
    hw::lineSet(hwtest::OutSignal::kRI, false);
}

void readCommandInput(uint32_t nowMs) {
    uint8_t buf[512];
    if (detecting()) {
        flushUart();  // the capture has the same bytes, decoded at the right speed
        size_t n = 0;
        while ((n = hw::captureRead(runs, kMaxRuns)) > 0) {
            autobaud::Burst b = autobaud::decodeBurst(runs, n, currentBaud, buf, sizeof buf);
            if (b.changed) {
                setBaud(b.baud);
            }
            feed(buf, b.len, nowMs);
            if (sess->mode() == Mode::kOnline) {
                break;  // the rest arrives through the UART
            }
        }
        return;
    }
    HardwareSerial &u = hw::uart();
    size_t n = 0;
    while (n < sizeof buf && u.available() > 0) {
        buf[n++] = static_cast<uint8_t>(u.read());
    }
    feed(buf, n, nowMs);
}

void readOnlineInput(uint32_t nowMs) {
    HardwareSerial &u = hw::uart();
    uint8_t buf[512];
    size_t room = sess->hostSpace();
    size_t n = 0;
    while (n < sizeof buf && n < room && u.available() > 0) {
        buf[n++] = static_cast<uint8_t>(u.read());
    }
    if (n > 0) {
        fromComputer += n;
        sess->fromHost(buf, n, nowMs);  // takes them all: n <= room
    }
}

void writeOutput() {
    int room = hw::hostWriteSpace();
    if (room <= 0) {
        return;
    }
    uint8_t buf[512];
    size_t cap = static_cast<size_t>(room) < sizeof buf ? static_cast<size_t>(room) : sizeof buf;
    size_t n = sess->toHost(buf, cap);
    if (n > 0) {
        sink.toComputer += hw::hostWrite(buf, n);
    }
}

}  // namespace

void begin(atcmd::Modem &modem, hostport::CallSlot &slot) {
    modemPtr = &modem;
    runs = static_cast<autobaud::Run *>(ps_malloc(kMaxRuns * sizeof(autobaud::Run)));
    if (runs == nullptr) {
        runs = static_cast<autobaud::Run *>(malloc(kMaxRuns * sizeof(autobaud::Run)));
    }
    sess = new hostport::Session(modem, sink, modemcfg::kFactoryBaud, slot);
    sess->setBaudControl(&callBaud);
    startPort();
}

hostport::Session &session() {
    return *sess;
}

uint32_t baud() {
    return currentBaud;
}

bool detecting() {
    return modemPtr->hostBaud() == 0;
}

void applyHostBaud() {
    if (isSuspended || modemPtr->hostBaud() == 0) {
        return;  // back to detection: stay at the current speed until the next "AT"
    }
    hw::hostDrain(2000);  // let OK leave at the old rate; never wait forever for RTS
    setBaud(modemPtr->hostBaud());
}

void poll(uint32_t nowMs) {
    if (isSuspended || sess == nullptr) {
        return;
    }
    const modemcfg::Profile &p = profile();

    const hostport::FlowMode f = hostport::flowMode(p);
    if (!flowKnown || f != flow) {
        hw::hostSetFlow(f.ctsFromBuffer, f.obeyRts);
        flow = f;
        flowKnown = true;
    }

    const bool wantCapture = detecting() && sess->mode() != Mode::kOnline && !sess->localOpen();
    if (wantCapture && !hw::captureActive()) {
        hw::captureBegin();
    } else if (!wantCapture && hw::captureActive()) {
        hw::captureEnd();
        flushUart();  // the UART's copy of the command that went online
    }

    if (sess->mode() == Mode::kOnline || sess->localOpen()) {
        readOnlineInput(nowMs);
    } else {
        readCommandInput(nowMs);
    }

    int c = sess->binaryLocal() || !hw::hostWriteSpace()
                ? -1
                : xoff.update(static_cast<size_t>(hw::uart().available()), hw::kHostRxBuffer, p);
    if (c >= 0) {
        uint8_t control = c;
        hw::hostWrite(&control, 1);
    }
    writeOutput();

    sess->tick(nowMs, hw::dtrAsserted());
    const bool dcd = sess->dcd();
    if (!dcdKnown || dcd != dcdSet) {
        hw::lineSet(hwtest::OutSignal::kDCD, dcd);
        dcdSet = dcd;
        dcdKnown = true;
    }
}

void suspend() {
    if (isSuspended) {
        return;
    }
    hw::captureEnd();
    hw::hostEnd();
    hw::lineSet(hwtest::OutSignal::kDCD, false);
    hw::lineSet(hwtest::OutSignal::kRI, false);
    isSuspended = true;
}

void resume() {
    if (!isSuspended) {
        return;
    }
    isSuspended = false;
    startPort();
}

bool suspended() {
    return isSuspended;
}

int readKey() {
    if (keyCount == 0 && !isSuspended) {
        if (hw::captureActive()) {
            flushUart();
            size_t n = hw::captureRead(runs, kMaxRuns);
            if (n > 0) {
                keyCount = autobaud::decode(runs, n, currentBaud, keys, sizeof keys);
            }
        } else {
            HardwareSerial &u = hw::uart();
            while (keyCount < sizeof keys && u.available() > 0) {
                keys[keyCount++] = static_cast<uint8_t>(u.read());
            }
        }
        fromComputer += keyCount;
    }
    if (keyCount == 0) {
        return -1;
    }
    int c = keys[0];
    for (size_t i = 1; i < keyCount; ++i) {
        keys[i - 1] = keys[i];
    }
    --keyCount;
    return c;
}

uint32_t bytesToComputer() {
    return sink.toComputer;
}

uint32_t bytesFromComputer() {
    return fromComputer;
}

bool dtrOn() {
    return sess != nullptr && sess->dtrOn();
}

}  // namespace rs232
