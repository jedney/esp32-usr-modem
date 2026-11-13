// The USB modem port (v2.0, brief Q25): the S3's native USB (GPIO19/20, also wired to the
// rear USB-C socket) as a TinyUSB CDC-ACM serial device. A PC sees it as a COM port.
//
// Only the port itself lives here; the AT session is in src/usbport.cpp.
//   - Writes never wait: what doesn't fit, or arrives with no program connected, is dropped.
//   - Reads take only what the caller asks for. Unread data stays in TinyUSB's buffer and
//     the PC is held off, so there is end-to-end flow control and nothing is lost.
//   - DCD, DSR and RI go to the PC as CDC SERIAL_STATE notifications on the interrupt
//     endpoint (usbSendSerialState); hostport::SerialStateSender decides when.
//   - The PC sees the device as the selected model: product "US Robotics Sportster 14.4k"
//     (or the model chosen), maker "US Robotics", serial number the firmware version. The
//     names are fixed when the USB device starts, so a model change shows after a restart.
#pragma once

#include <cstddef>
#include <cstdint>

namespace hw {

// Starts the USB device with these descriptor strings; false if the driver couldn't start.
// Call once at boot.
bool usbBegin(const char *product, const char *manufacturer, const char *serial);
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
// Sends the lines to the PC (CDC SERIAL_STATE). False, and nothing sent, while the device
// isn't configured or the last notification is still going; never waits.
bool usbSendSerialState(bool dcd, bool dsr, bool ri);

}  // namespace hw
