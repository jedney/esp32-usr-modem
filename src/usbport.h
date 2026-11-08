// The USB modem port (v2.0, brief Q25): an AT session on the S3's native USB, which the
// rear USB-C socket also carries. A PC sees a USB serial port that works like the RS232
// port: AT commands, calls, +++ and ATO, the configuration screen, Client Tests.
//
// Differences from RS232 (hw/usb_port.h): no speed detection (USB has no line speed), flow
// control comes from USB itself, and DCD/RI can't be sent, so calls show by result codes.
// DTR is the PC program's DTR: &D applies as on RS232.
#pragma once

#include <cstdint>

#include "atcmd.h"
#include "session.h"

namespace usbport {

void begin(atcmd::Modem &modem, hostport::CallSlot &slot);  // starts the USB device too
void poll(uint32_t nowMs);                                  // call every loop

hostport::Session &session();
bool started();    // the USB driver started
bool connected();  // a PC has the port enumerated
bool dtrOn();      // the PC program has the port open (DTR on)

// For a hardware test running on this port: the next byte from the PC, or -1.
int readKey();

// For the front-panel LEDs.
uint32_t bytesToComputer();
uint32_t bytesFromComputer();

}  // namespace usbport
