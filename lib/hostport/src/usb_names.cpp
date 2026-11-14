// The USB modem port's serial number: see usb_names.h.
#include "usb_names.h"

namespace hostport {

void usbSerialNumber(const uint8_t mac[6], char out[kUsbSerialLength + 1]) {
    static const char kHex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < 6; ++i) {
        out[2 * i] = kHex[mac[i] >> 4];
        out[2 * i + 1] = kHex[mac[i] & 0x0F];
    }
    out[kUsbSerialLength] = '\0';
}

}  // namespace hostport
