#include "console_out.h"

#include <Arduino.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>

namespace consoleout {
namespace {

// A short spinlock: the log is written from the loop task and from any task that uses
// esp_log, and read by the network console in the loop task. Each hold is one memcpy.
class SpinLock : public netconsole::ConsoleLog::Lock {
public:
    void lock() override { portENTER_CRITICAL(&mux_); }
    void unlock() override { portEXIT_CRITICAL(&mux_); }

private:
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;
};

SpinLock lock;
netconsole::ConsoleLog consoleLog;
uint8_t fallback[1024];  // if PSRAM has no room
vprintf_like_t idfVprintf = nullptr;

// esp_log's output: copied into the log, then printed as before. One shared line buffer
// rather than one on each caller's stack (some IDF tasks have small stacks); a line
// logged while another task holds it reaches UART0 but not the log.
std::atomic_flag lineBusy = ATOMIC_FLAG_INIT;
char line[256];
int logVprintf(const char *format, va_list args) {
    if (!lineBusy.test_and_set()) {
        va_list copy;
        va_copy(copy, args);
        int n = vsnprintf(line, sizeof line, format, copy);
        va_end(copy);
        if (n > 0) {
            consoleLog.append(line, static_cast<size_t>(n) < sizeof line ? static_cast<size_t>(n)
                                                                         : sizeof line - 1);
        }
        lineBusy.clear();
    }
    return idfVprintf ? idfVprintf(format, args) : 0;
}

}  // namespace

void begin() {
    void *storage = ps_malloc(kLogBytes);
    if (storage) {
        consoleLog.begin(static_cast<uint8_t *>(storage), kLogBytes, &lock);
    } else {
        consoleLog.begin(fallback, sizeof fallback, &lock);
    }
    idfVprintf = esp_log_set_vprintf(logVprintf);
}

void write(const void *data, size_t len) {
    if (!len) {
        return;
    }
    Serial.write(static_cast<const uint8_t *>(data), len);
    consoleLog.append(data, len);
}

void print(const char *text) {
    write(text, strlen(text));
}

void printf(const char *format, ...) {
    char small[128];
    va_list args;
    va_start(args, format);
    va_list copy;
    va_copy(copy, args);
    int n = vsnprintf(small, sizeof small, format, copy);
    va_end(copy);
    if (n > 0 && static_cast<size_t>(n) < sizeof small) {
        write(small, static_cast<size_t>(n));
    } else if (n > 0) {
        char *big = static_cast<char *>(malloc(static_cast<size_t>(n) + 1));
        if (big) {
            vsnprintf(big, static_cast<size_t>(n) + 1, format, args);
            write(big, static_cast<size_t>(n));
            free(big);
        }
    }
    va_end(args);
}

const netconsole::ConsoleLog &log() {
    return consoleLog;
}

}  // namespace consoleout
