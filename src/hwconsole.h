// AT*HW... hardware-test commands on the USB maintenance console (project brief 6.14).
#pragma once

namespace hwconsole {

// `rest` is the command after "AT*HW", e.g. "LED=CD" for "AT*HWLED=CD".
// Prints the result and the final OK / ERROR.
void handle(const char *rest);

// One line per command, for AT*HELP.
void printHelp();

// Name for an esp_reset_reason() value ("power-on", "panic", ...).
const char *resetReasonName(int reason);

}  // namespace hwconsole
