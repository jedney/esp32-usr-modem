// The USB modem port (v2.0, brief Q25): an AT session on the S3's native USB, which the
// rear USB-C socket also carries. A PC sees a USB serial port that works like the RS232
// port: AT commands, calls, +++ and ATO, the configuration screen, Client Tests.
//
// Differences from RS232 (hw/usb_port.h): no speed detection (USB has no line speed) and
// flow control comes from USB itself. DTR is the PC program's DTR: &D applies as on RS232.
// DCD (&C), DSR (always on) and RI (the incoming call's ring cadence) go to the PC as CDC
// serial-state notifications, by the same rules as the RS232 lines.
//
// The PC sees the selected model as the USB product name ("US Robotics Sportster 14.4k"),
// "US Robotics" as the maker and the firmware version as the serial number.
#pragma once

#include <cstdint>

#include "atcmd.h"
#include "session.h"

namespace usbport {

// Starts the USB device too, named for the selected model, with `version` as its serial
// number.
void begin(atcmd::Modem &modem, hostport::CallSlot &slot, const char *version);
// Every loop. `ri`: the RI line as it should be now (the incoming call's ring cadence).
void poll(uint32_t nowMs, bool ri);

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
