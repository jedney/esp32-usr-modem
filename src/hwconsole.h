// AT*HW... hardware-test commands (project brief 6.14). They run on any local port: the
// maintenance console, the USB modem port, the RS232 host port, or the browser terminal.
// A test prints and reads keys on the port the command came from. Every "any key" test
// ends by itself when that terminal closes (DTR drops, the browser leaves) or when no key
// arrives for hwtest::kIdleTimeoutMs.
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
    virtual bool isBrowser() const { return false; }
    // False once the terminal has gone (DTR off, browser closed): running tests then stop.
    virtual bool connected() const { return true; }
    virtual void service() {}
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
// The browser terminal has gone: releases the LED, amp and RS232-output holds the browser
// made. Holds made from other ports stay.
void restoreBrowserResources();

// One line per command, for AT*HELP.
void printHelp(Term &term);

// True while a test (or AT*HWLED, until AT*HWLED=AUTO) controls the front-panel LEDs.
bool ledsHeld();

// Name for an esp_reset_reason() value ("power-on", "panic", ...).
const char *resetReasonName(int reason);

}  // namespace hwconsole
