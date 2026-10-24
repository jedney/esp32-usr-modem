// AT*HW... hardware-test commands (project brief 6.14), on the USB console or the RS232
// port.
#pragma once

#include <cstddef>

namespace hwconsole {

// Where a test prints and reads keys: the port the command came from.
class Term {
public:
    virtual ~Term() = default;
    virtual void write(const char *data, size_t len) = 0;
    virtual int read() = 0;  // next key, or -1
    virtual bool isRs232() const = 0;
    void print(const char *s);
    void printf(const char *fmt, ...) __attribute__((format(printf, 2, 3)));
};

// What the RS232 line, loopback and bridge tests need from the rest of the firmware
// (Q79): they run only on the USB console, only with no call up, and the RS232 session is
// suspended while they use the port.
struct PortControl {
    bool (*idle)();     // no call on any port
    void (*suspend)();  // stop the RS232 session, free UART1 and the handshake lines
    void (*resume)();
};
void setPortControl(const PortControl &pc);

// `rest` is the command after "AT*HW", e.g. "LED=CD" for "AT*HWLED=CD".
// Prints the result and the final OK / ERROR.
void handle(Term &term, const char *rest);

// One line per command, for AT*HELP.
void printHelp(Term &term);

// True while a test (or AT*HWLED, until AT*HWLED=AUTO) controls the front-panel LEDs.
bool ledsHeld();

// Name for an esp_reset_reason() value ("power-on", "panic", ...).
const char *resetReasonName(int reason);

}  // namespace hwconsole
