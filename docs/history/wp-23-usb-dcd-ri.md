# WP-23 (v3.0): DCD and RI on the USB modem port

| Item | Value |
| --- | --- |
| Model / effort | Sonnet 5.5 / high |
| Size | M |
| Depends on | WP-20 (USB port verified on the board), WP-22 (network console, not required) |

Read [wp-19-common.md](wp-19-common.md) first (decisions, versions, board testing, rules).

Decision 10. The USB modem port shows DCD and RI to the computer's program the way the
RS232 port does, so terminal programs, BBS software and `pyserial` (`.cd`, `.ri`) see a
call's carrier and an incoming call's ringing. Today it shows calls by result codes only
(`src/usbport.h`, `lib/hw/src/usb_port.h`, `docs/COMMANDS.md`, README).

CDC-ACM sends line states to the computer as a SERIAL_STATE notification on the
interrupt endpoint (0x85, already in the descriptor): `bRxCarrier` is DCD, `bTxCarrier`
is DSR, `bRingSignal` is RI. TinyUSB 0.21.0 sends one with
`tud_cdc_n_notify_uart_state(itf, &state)` when `CFG_TUD_CDC_NOTIFY` is 1; it returns
false while the endpoint is busy or the device isn't configured.

## Steps

0. **Enumeration first.** Set `CFG_TUD_CDC_NOTIFY 1`
   (`components/arduino_tinyusb/include/tusb_config.h`), flash, and check the PC still
   sees the port and `e2e/test_usb.py` passes. esp32-arduino-lib-builder turned this
   option off on 12 Nov 2026 because the CDC port stopped enumerating with TinyUSB master,
   and back on after a TinyUSB fix. If enumeration fails with `espressif/tinyusb`
   0.21.0~6, look for that fix and a newer `espressif/tinyusb` that has it (a patch
   release of the same line is taken); otherwise stop and ask Jon.
1. **What the lines show** (the same rules as RS232, so both ports behave alike):
   - **DCD** = `usbport::session().dcd()` (`hostport::dcdOn()`: `&C1` follows the call's
     carrier, `&C0` always on). The RS232 code (`src/rs232.cpp`, `dcdSet`/`dcdKnown`)
     shows how it is pushed now.
   - **RI** follows the incoming call's ring cadence, `incoming->ri()`, as RS232 RI does
     in `src/main.cpp`, under the same conditions. If the answer-port setting
     (`AT*ANSWERPORT`) should limit which port rings, ask Jon rather than guess.
   - **DSR** always on while the port is configured, as RS232's DSR is hard-wired on.
2. **When to send** (logic in `lib/hostport`, no TinyUSB calls, native tests first in
   `test_hostport`): send when a line changes; send the current state again when the
   computer configures the device or a program opens the port (DTR on), because the
   driver doesn't remember a state it got while no program had the port; while a send
   fails (endpoint busy, not configured), keep the newest state and try again on the next
   loop pass, never wait. A ring burst shorter than one send must still reach the
   computer as RI on, then off. `lib/hw/src/usb_port.*` gets one call that sends a state
   (only while `tud_ready()`, like the other calls there); `src/usbport.cpp` wires the
   two together.
3. **Check what the PC reports.** In the CDC specification RI (`bRingSignal`) is an
   "irregular" signal, an event rather than a level, and drivers differ: Linux `cdc-acm`
   keeps it as a modem-status bit; Windows `usbser.sys` may report it only as a ring
   event. Find out what `pyserial`'s `.cd` and `.ri` read on Jon's Windows PC during a
   call and a ring, and write it down (decisions log, `docs/COMMANDS.md`). If RI can't be
   seen as a level on Windows, keep sending it (Linux and other drivers use it) and say
   so in the docs.
4. **End-to-end** (`test/e2e/test_usb.py`, with the board): `.cd` off at idle with
   `&C1`, on during `AT*TESTCALL` or a call, off after the hang-up; on at idle with
   `&C0`; `.ri` (or the ring event) during an incoming call on port 23 (the file's
   `answer_setup()` and `call_in()`); DSR on. Restore the settings each test changes.
5. **Docs:** `src/usbport.h`, `lib/hw/src/usb_port.h` and the `tusb_config.h` comment;
   `docs/COMMANDS.md` (the USB port paragraph, `&C`), README (the rear USB-C row),
   `AT*HELP` if it mentions the USB port's limits, the firmware spec (WP-25 trims it later;
   `docs/project-brief.md`, the USB modem port), `test/README.md` matrix,
   decisions-log row (it supersedes the old "USB limits" decision; with what Windows
   reports), `CHANGES.md` [3.0] "Added".
6. End checks (native tests, firmware build, lint, clang-format, `test/scripts`), and the
   USB e2e file on the board. WP-26's bench run repeats it with a terminal program.

Cut line: 0-2 and 4 for DCD; RI's e2e test and the Windows check (3) can move to the
bench run (WP-26).

## Result (14 Nov 2026)

Done, with Jon's branding changes in the same session. Reasons and details are in the
decisions log (rows "USB modem port DCD, DSR and RI (WP-23)" and "USB names and network
console greeting").

- **Step 0:** with `CFG_TUD_CDC_NOTIFY 1` the port enumerates with `espressif/tinyusb`
  0.21.0~6; no newer TinyUSB needed.
- **Root cause found on the board:** Arduino's notification endpoint 0x85 has no transmit
  FIFO on the S3, so the first notification never left and the endpoint stayed busy.
  `lib/hw/src/usb_port.cpp` now takes a free IN endpoint (0x83).
- **Code:** `hostport::SerialStateSender` (`lib/hostport/src/serial_state.*`, 6 native
  cases in `test_hostport`), `hw::usbSendSerialState()`, wired in `src/usbport.cpp`; RI
  is `incoming->ri()` on every port, as on RS232 (`AT*ANSWERPORT` doesn't limit it).
- **Windows 11 (step 3):** pyserial `.cd`, `.dsr` and `.ri` all work; RI is a level that
  follows the ring cadence, and `.cts` reads on.
- **Branding (Jon):** USB product name = the selected model ("US Robotics Sportster
  14.4k"), maker "US Robotics", serial number the firmware version (VID/PID unchanged);
  the network console greets with the model and status before `Password: `. The new
  serial number moved the bench USB port from COM21 to COM22.
- **Tests on the board (OTA, network console):** `test_usb.py` 11 passed, 1 skipped (the
  manual unplug test); `test_net_console.py` 7 passed. The on-board `test_usb` suite got
  a serial-state case but wasn't run (it needs the console cable); WP-26 can run it.
