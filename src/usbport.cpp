// The USB modem port: see usbport.h.
#include "usbport.h"

#include <Arduino.h>

#include "input_buffer.h"
#include "usb_port.h"

namespace usbport {

namespace {

constexpr uint32_t kDefaultBaud = 115200;  // reported until the PC sets a speed

// Direct writes (results the Session sends straight to the sink). Like the RS232 port,
// wait at most 2 s for room, then drop the rest: a program that has the port open but
// stops reading must not stop the modem. Nothing is sent while the port is closed.
class UsbSink : public atcmd::Sink {
public:
    void write(const char *data, size_t len) override {
        size_t done = 0;
        const uint32_t start = millis();
        while (done < len && hw::usbDtr()) {
            if (millis() - start > 2000) {
                return;
            }
            size_t room = hw::usbWriteSpace();
            if (room > 0) {
                size_t n = len - done < room ? len - done : room;
                size_t written = hw::usbWrite(reinterpret_cast<const uint8_t *>(data + done), n);
                done += written;
                toComputer += written;
            } else {
                delay(1);
            }
        }
    }
    uint32_t toComputer = 0;
};

UsbSink sink;
hostport::InputBuffer input;
hostport::Session *sess = nullptr;  // made once at boot
uint32_t fromComputer = 0;
uint32_t reportedBaud = 0;

void wipe(uint8_t *data, size_t len) {
    volatile uint8_t *p = data;
    for (size_t i = 0; i < len; ++i) {
        *p++ = 0;
    }
}

void followBaud() {
    uint32_t b = hw::usbBaud();
    b = b ? b : kDefaultBaud;
    if (b != reportedBaud) {
        reportedBaud = b;
        sess->port().setBaud(b);
    }
}

void readInput(uint32_t nowMs) {
    input.poll(*sess, nowMs);
    if (input.size()) {
        return;  // the session is still busy with earlier bytes
    }
    // Take only what the session can use now. The rest stays in TinyUSB's buffer and the
    // PC is held off, so nothing is lost.
    uint8_t buf[256];
    size_t room = sess->hostSpace();
    size_t cap = room < sizeof buf ? room : sizeof buf;
    size_t n = hw::usbRead(buf, cap);
    if (n > 0) {
        fromComputer += n;
        input.append(buf, n);
        input.poll(*sess, nowMs);
        wipe(buf, n);  // input can contain passwords
    }
}

void writeOutput() {
    size_t room = hw::usbWriteSpace();
    if (room == 0) {
        return;
    }
    uint8_t buf[512];
    size_t cap = room < sizeof buf ? room : sizeof buf;
    size_t n = sess->toHost(buf, cap);
    if (n > 0) {
        sink.toComputer += hw::usbWrite(buf, n);
    }
}

}  // namespace

void begin(atcmd::Modem &modem, hostport::CallSlot &slot) {
    hw::usbBegin();
    sess = new hostport::Session(modem, sink, kDefaultBaud, slot);
    reportedBaud = kDefaultBaud;
}

void poll(uint32_t nowMs) {
    if (sess == nullptr) {
        return;
    }
    if (hw::usbStarted()) {
        followBaud();
        readInput(nowMs);
        writeOutput();
    }
    sess->tick(nowMs, hw::usbDtr());
}

hostport::Session &session() {
    return *sess;
}

bool started() {
    return hw::usbStarted();
}

bool connected() {
    return hw::usbMounted();
}

bool dtrOn() {
    return sess != nullptr && sess->dtrOn();
}

int readKey() {
    uint8_t c;
    if (hw::usbRead(&c, 1) != 1) {
        return -1;
    }
    ++fromComputer;
    return c;
}

uint32_t bytesToComputer() {
    return sink.toComputer;
}

uint32_t bytesFromComputer() {
    return fromComputer;
}

}  // namespace usbport
