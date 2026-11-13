// The USB modem port (v2.0, brief Q25): the S3's native USB (GPIO19/20, also wired to the
// rear USB-C socket) as a TinyUSB CDC-ACM serial device. A PC sees it as a COM port.
//
// Only the port itself lives here; the AT session is in src/usbport.cpp.
//   - Writes never wait: what doesn't fit, or arrives with no program connected, is dropped.
//   - Reads take only what the caller asks for. Unread data stays in TinyUSB's buffer and
//     the PC is held off, so there is end-to-end flow control and nothing is lost.
//   - DCD and RI aren't sent: CDC serial-state notifications are off
//     (CFG_TUD_CDC_NOTIFY in components/arduino_tinyusb/include/tusb_config.h).
#pragma once

#include <cstddef>
#include <cstdint>

namespace hw {

bool usbBegin();     // false if the driver couldn't start; call once at boot
bool usbStarted();   // usbBegin succeeded
bool usbMounted();   // a PC has enumerated the port and it isn't suspended
bool usbDtr();       // mounted and the PC's program has DTR on (port open)
bool usbRts();       // mounted and RTS on
uint32_t usbBaud();  // the speed the PC's program set (0 if none yet); has no effect on USB
size_t usbAvailable();
size_t usbRead(uint8_t *data, size_t cap);
size_t usbWriteSpace();  // 0 while no program has the port open
size_t usbWrite(const uint8_t *data, size_t length);
void usbDiscardInput();
bool usbTxDrained();  // nothing waiting to go to the PC (or no PC to send to)

}  // namespace hw
