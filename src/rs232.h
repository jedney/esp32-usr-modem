// The RS232 host port (spec 3.2): UART1 through the MAX3232s, with speed
// detection from "AT", flow control, DTR/DCD and the port's AT session.
#pragma once

#include <cstddef>
#include <cstdint>

#include "atcmd.h"
#include "session.h"

namespace rs232 {

void begin(atcmd::Modem &modem, hostport::CallSlot &slot);
void poll(uint32_t nowMs);  // call every loop

hostport::Session &session();
uint32_t baud();       // the port's current speed
bool detecting();      // speed from "AT" (AT*BAUD=AUTO)
void applyHostBaud();  // after AT*BAUD changes

// The hardware tests take the port over (spec 3.13): suspend stops the session and
// frees UART1 and the handshake lines; resume starts them again.
void suspend();
void resume();
bool suspended();

// For a hardware test running on the RS232 port: the next byte from the computer, or -1.
int readKey();

// For the front-panel LEDs.
uint32_t bytesToComputer();
uint32_t bytesFromComputer();
bool dtrOn();

}  // namespace rs232
