// The USB modem port's serial number: the Wi-Fi MAC address as 12 hex digits
// ("A1B2C3D4E5F6"). It stays the same across firmware versions, so a PC keeps the same COM
// port for the modem, and differs between modems, so two of them on one PC get their own.
#pragma once

#include <cstddef>
#include <cstdint>

namespace hostport {

constexpr size_t kUsbSerialLength = 12;  // without the terminating NUL

// Writes the serial number for this MAC into `out` (kUsbSerialLength + 1 bytes).
void usbSerialNumber(const uint8_t mac[6], char out[kUsbSerialLength + 1]);

}  // namespace hostport
