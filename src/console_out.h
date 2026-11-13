// Every byte the firmware writes to the maintenance console goes through here: to UART0
// (Serial) and into the console log that the network console replays and reads from
// (lib/netconsole). Nothing in src/ writes to Serial directly, so nothing bypasses the copy.
// ESP-IDF log output (esp_log) is copied in too; it reaches UART0 on its own path.
#pragma once

#include <cstddef>

#include "console_log.h"

namespace consoleout {

constexpr size_t kLogBytes = 16 * 1024;  // in PSRAM

void begin();  // right after Serial.begin(), before anything is printed
void write(const void *data, size_t len);
void print(const char *text);
void printf(const char *format, ...) __attribute__((format(printf, 1, 2)));
const netconsole::ConsoleLog &log();

}  // namespace consoleout
