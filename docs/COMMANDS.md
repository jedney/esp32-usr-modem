# Command reference

Every command the USR Modem Emulator accepts, on the USB maintenance console (115200 8N1),
the RS232 host port and the USB modem port (v2.0). The modem also lists them itself:
`AT*HELP` (project commands), `AT*HW?` (hardware tests) and `AT*REFERENCE` (everything).

The **USB modem port** is the S3's native USB, carried by the rear USB-C socket. A PC sees
it as a USB serial port (a new COM port on Windows). It works like the RS232 port, with
its own session, configuration screen and Client Tests settings. Only one port can have a
call at a time. Differences: it has no line speed (the speed the PC sets is shown but not
used), USB does the flow control, and it can't show DCD or RI, so calls show by result
codes only. DTR is the PC program's DTR: closing the port drops DTR, and `&D` applies.

- Commands start with `AT` and end with Enter (CR, `S3`). Several standard commands can
  share a line: `ATE1V1X4`. A project command (`AT*...`) takes the rest of the line.
- `A/` (no Enter) repeats the last command line. Lines with a password are not repeated.
- Upper or lower case both work, but `AT` must be `AT` or `at`.
- `?` after a command shows its value; `=?` shows the allowed values where supported.
- A remote caller (someone who dialled in on port 23) can never reach command mode, any
  `AT*` command or the configuration screen.

## Standard commands

These follow the US Robotics Sportster. "Profile" means the setting is part of the active
profile: `AT&W` saves it, `ATZ` reloads it.

### Calls

| Command | What it does |
|---|---|
| `ATD<string>` | Dial. `ATDT` tone, `ATDP` pulse; plain `ATD` uses the default (`P`/`T`). See [Dialling](#dialling) |
| `ATDL` | Redial the last number (`AT&ZL?` shows it) |
| `ATDS<n>` | Dial stored number `n` (0-3, saved with `AT&Z<n>=`) |
| `ATA` | Answer a ringing incoming call on this port |
| `ATH` / `ATH0` | Hang up, or reject a ringing call. `ATH1` is accepted and does nothing |
| `ATO` / `ATO1` | Go back online after `+++` |
| `+++` | Escape to online-command mode: one second of silence (`S12`), three `+` (`S2`), one second of silence. The call stays up; `OK` follows |
| `ATZ` / `ATZ<n>` | Hang up and reload: `Z0` the power-on template, `Z1`/`Z2` stored template 0/1, `Z3`-`Z5` factory profile 0-2. The rest of the line is ignored |
| `AT&F` / `AT&F<n>` | Load factory profile 0 (no flow control), 1 (hardware flow control, default) or 2 (software flow control). Wi-Fi, passwords, model and phonebook are kept |

### Settings in the profile

Factory values are for `&F1`, the power-on default.

| Command | Values | Factory |
|---|---|---|
| `B` | Answer sequence: 0 ITU-T, 1 Bell (emulated) | 0 |
| `E` | Command echo: 0 off, 1 on. Passwords are always masked | 1 |
| `F` | Online local echo: 0 on, 1 off | 1 |
| `L` | Speaker level within the volume knob: 0/1 low, 2 medium, 3 high | 2 |
| `M` | Speaker: 0 off (calls connect at once), 1 on until CONNECT, 2 always (carrier tone stays on), 3 like 1 but no dial tone | 1 |
| `Q` | Results: 0 shown, 1 none, 2 none for answered calls | 0 |
| `V` | Results as 0 numbers, 1 words | 1 |
| `X` | Result set 0-4 (USR): `X0` basic, `X4` all including BUSY and NO DIAL TONE | 4 |
| `P` / `T` | Default dial method pulse / tone | T |
| `&A` | Extra CONNECT text: 0 none, 1 `/ARQ`, 2 modulation, 3 protocol | 0 |
| `&B` | RS232 speed: 0 follows the call speed, 1 stays at the speed of "AT", 2 mixed (follows the call speed, as 0). Ignored when `AT*BAUD` fixes the speed | 1 |
| `&C` | DCD: 0 always on, 1 follows the call | 1 |
| `&D` | DTR: 0 ignored, 2 dropping DTR hangs up, 3 dropping DTR hangs up and runs `ATZ` | 2 |
| `&G` | Guard tone (stored for compatibility) | 0 |
| `&H` | Transmit flow control: 0 none, 1 CTS, 2 XON/XOFF, 3 both | 1 |
| `&I` | Receive software flow control: 0 off, 1 or 2 XON/XOFF (2 removes them), 5 compatibility | 0 |
| `&K` | Data compression (stored for compatibility; data is never changed) | 1 |
| `&M` | Error control: 0, 4 or 5 (stored for compatibility) | 4 |
| `&N` | Line speed: 0 highest (56000), 1-38 fixed (table below). Sets the reported CONNECT speed and, with `&B0`, the RS232 speed | 8 (14400) |
| `&P` | Pulse ratio: 0 US, 1 UK | 1 |
| `&R` | Receive hardware flow control: 1 ignore RTS, 2 obey RTS | 2 |
| `&S` | DSR: only 0. DSR is wired on permanently | 0 |
| `&T` | Remote digital loopback: 4 allow, 5 refuse (emulated) | 5 |
| `&U` | Lowest line speed, as an `&N` index: 0 none. A call slower than this gives NO CARRIER | 0 |
| `&Y` | Break handling (stored for compatibility) | 1 |

`&N` speeds: 1 300, 2 1200, 3 2400, 4 4800, 5 7200, 6 9600, 7 12000, 8 14400, 9 16800,
10 19200, 11 21600, 12 24000, 13 26400, 14 28800, 15 31200, 16 33600, 17 28000, 18 29333,
19 30666, 20 32000, 21 33333, 22 34666, 23 36000, 24 37333, 25 38666, 26 40000, 27 41333,
28 42666, 29 44000, 30 45333, 31 46666, 32 48000, 33 49333, 34 50666, 35 52000, 36 53333,
37 54666, 38 56000.

If no `&N` was typed since `&F`, CONNECT reports the speed of the selected model
(`AT*MODEL`). A phonebook entry's own speed wins over both.

### S-registers

`ATS<r>?` reads a register, `ATS<r>=<n>` sets it (0-255), `ATS<r>.<bit>=<0|1>` sets one bit,
and `ATS<r>` selects it for a following `?` or `=`. Registers 0-63 exist; those not listed
are stored for compatibility only.

| Register | Meaning | Factory |
|---|---|---|
| S0 | Auto-answer after this many rings; 0 manual (`ATA`) only. A call nobody answers is dropped after 8 rings (about 24 s), so a value above 8 answers on ring 8 | 0 |
| S1 | Ring count of the current call (read only, never saved) | 0 |
| S2 | Escape character; above 127 disables `+++` | 43 (`+`) |
| S3 | Command terminator | 13 (CR) |
| S4 | Line feed character for results | 10 (LF) |
| S5 | Backspace character (DEL also works) | 8 |
| S6 | Dial tone length, seconds | 2 |
| S7 | Time limit for an outgoing call (name lookup and connect), seconds | 60 |
| S8 | Comma pause (compatibility) | 2 |
| S10 | Carrier loss delay (compatibility; a dropped TCP link is seen at once) | 14 |
| S11 | DTMF tone length, milliseconds | 70 |
| S12 | `+++` guard time, fiftieths of a second | 50 |
| S22 / S23 | XON / XOFF character | 17 / 19 |
| S25 | DTR drop filter, hundredths of a second | 5 |

### Stored numbers and templates

| Command | What it does |
|---|---|
| `AT&W` / `AT&W<n>` | Save the active profile, with the current RS232 speed, as template 0 or 1 |
| `ATY<n>` | Power-on profile: 0/1 template 0/1, 2-4 factory profile 0-2. Saved at once |
| `AT&Z<n>=<number>` | Store a number in slot 0-3 (up to 40 characters; `AT&Z<n>=L` stores the last number dialled) |
| `AT&Z<n>?` / `AT&ZL?` | Show slot `n` / the last number dialled |

### Information

| Command | What it shows |
|---|---|
| `ATI0` | Product code of the selected model |
| `ATI1` | ROM checksum (placeholder) |
| `ATI2` | RAM test (`OK`) |
| `ATI3` / `ATI9` | Model name |
| `ATI4` | Current settings and S-registers |
| `ATI5` | Stored templates and numbers |
| `ATI6` | Last call diagnostics |
| `ATI7` | Configuration (ROM and DSP details are marked as emulated) |
| `AT&V` | `ATI4` and `ATI5` together |
| `AT+FCLASS?` / `AT+FCLASS=0` | Data mode only; fax classes give ERROR |

### Result codes

`OK`, `CONNECT [speed]`, `RING`, `NO CARRIER`, `ERROR`, `NO DIAL TONE`, `BUSY`,
`NO ANSWER`, with numeric forms under `ATV0`. For calls:

- `BUSY`: the remote host refused the connection.
- `NO ANSWER`: no connection within `S7` seconds. `S7` counts only the wait for the network
  connection; once it is open, the handshake sound is not cut short.
- `NO DIAL TONE`: no Wi-Fi, or the host name could not be looked up.
- `NO CARRIER`: the call ended, a key was pressed while dialling, or a sound file is missing.
  It also follows when the other end vanishes without hanging up: the modem sends TCP
  keepalive probes (idle 60 s, then 6 probes 10 s apart), so a dead peer ends the call
  after about 2 minutes. This applies to outgoing and incoming calls.

### Incoming calls

A call on port 23 rings every 3 s (`RING` on every port, RI and the ring sound). Nobody
answering is not the end of the line: after **8 rings** (about 24 s) the modem hangs up on
the caller, frees the line and records it in the call log. The limit is fixed: it has no
`AT*` command. `S0` above 8 counts as 8. `ATA` before the 8th ring answers as usual.

## Dialling

What follows `ATDT`/`ATDP`:

| Dial string | Meaning |
|---|---|
| `1` ... up to 11 digits | Phonebook entry with that number |
| `bbs.example.com` | Host name, port 23. Any string with `.` or `:`, or in quotes, is a host |
| `bbs.example.com:2323` / `"name:2323"` | Host and port |
| `10.0.0.5:2323` | IP address and port |
| `170064142064` | 12 digits: zero-padded IP address, port 23 (170.64.142.64) |
| `0010020030047890` | 13-17 digits: zero-padded IP address then port (1.2.3.4:7890) |
| `123` | Configuration screen, after dial sounds |
| `666` or `5517546` | Internet by PPP |
| `667` or `5517545` | Internet by SLIP |

In numbers, spaces, `-`, brackets and the modifiers `, W @ ! ;` are ignored. Invalid strings
give ERROR without any sound. `AT*DIALTEST <string>` shows how a string would be dialled.

With the speaker on (`M1`-`M3`) an outgoing call plays: dial tone (`S6`), the digits
(DTMF or pulse), ringing (`AT*RINGDELAY`), the handshake for the line speed, a pause
(`AT*CONNECTDELAY`), then `CONNECT`. For a host name the name is looked up during the dial
tone and the digits of its IP address are played. `ATM0` connects without sounds or
pauses. Pressing a key before `CONNECT` cancels the call. Telnet entries negotiate binary
mode, ANSI terminal type and an 80x24 window; RAW entries pass bytes unchanged.

## Project commands (`AT*`)

"Saved" means stored at once and kept after a restart, `ATZ` and `AT&F`; `AT*FACTORY`
resets it. "This boot" means it lasts until the next restart.

### Modem and calls

| Command | What it does |
|---|---|
| `AT*MODEL[?]` / `=?` / `=<14400\|28800\|33600\|56000>` | Sportster model: changes `ATI` identity and the default CONNECT speed only. Saved; factory 14400 |
| `AT*THROTTLE[?]` / `=<ON\|OFF>` | Limit data in both directions to the line speed. Saved; factory OFF |
| `AT*RINGDELAY[?]` / `=?` / `=<0-60000>` | Ringing before an outgoing call connects, in ms (0 skips it). Saved; factory 1500 |
| `AT*CONNECTDELAY[?]` / `=?` / `=<0-60000>` | Pause after the handshake sound, before CONNECT, in ms. Saved; factory 250 |
| `AT*ANSWERPORT[?]` / `=?` / `=<RS232\|USB\|CONSOLE\|AUTO>` | Which port auto-answers (`S0`). AUTO uses the first port with DTR on, in the order RS232, USB, console (the console has no DTR, so it is the fallback). Not during a call. Saved; factory AUTO (RS232 before v2.0; a saved choice is kept) |
| `AT*BAUD[?]` / `=AUTO` / `=<rate>` | RS232 speed: AUTO detects it from each "AT"; or fixed at 300, 600, 1200, 2400, 4800, 9600, 19200, 38400, 57600 or 115200. Saved; factory AUTO (starting at 38400). Setting it from the console, USB or browser is refused (`ERROR`) while a call is up on the RS232 port; change it from the RS232 port, or hang up first |
| `AT*PHONEBOOK[?]` | List the phonebook (64 entries) |
| `AT*PHONEBOOK=<number>,<host>,<port>,<TELNET\|RAW>[,<speed>]` | Add or replace an entry. Number 1-11 digits (leading zeros count, special numbers refused); host up to 63 characters; port 1-65535; speed 0 or omitted = no override |
| `AT*PHONEDELETE=<number>` | Delete an entry |
| `AT*BOOKCLEAR` | Empty the phonebook (used by export restore) |
| `AT*DIAL=<number\|host:port>` | Dial like `ATDT`; from the browser the call goes to the RS232 port |
| `AT*DIALTEST <string>` | Show how a dial string is understood, without dialling |
| `AT*TESTCALL[=<seconds>]` | Test call to a built-in echo, to try `+++`, `ATO`, `ATH`, DTR, DCD and flow control. With seconds (1-3600), the far end hangs up after that long |
| `AT*INTERNET[?]` | Internet (PPP/SLIP) state, addresses, DNS, counters and last end reason |
| `AT*INTERNET=IP,<local>,<peer>` | Private address pair for PPP/SLIP (default 10.64.64.1 and 10.64.64.2). Saved |
| `AT*INTERNET=DNS,<AUTO\|ip>` | DNS given to the computer: AUTO uses the Wi-Fi DNS. Saved |
| `AT*INTERNET=HANGUP` | End an Internet call from the other local port |

### Wi-Fi and clock

| Command | What it does |
|---|---|
| `AT*WIFI[?]` | Wi-Fi state, network, signal, address. The password shows only as "set" or "not set" |
| `AT*WIFI=<ON\|OFF>` | Switch Wi-Fi on or off. Saved |
| `AT*WIFISCAN` | List networks in range |
| `AT*WIFIJOIN=<n\|"name">` | Join network `n` from the scan, or by name. Asks for the password at a masked prompt; saved only once connected |
| `AT*WIFIFORGET` | Forget the network and its password |
| `AT*WIFIIP=DHCP` / `=<ip>,<mask>,<gateway>[,<dns>]` | Automatic or static address. Saved |
| `AT*HOSTNAME=<name>` | Network name, reached as `<name>.local`. Saved; factory `usr-modem` |
| `AT*TIME` | Date and time (from NTP) |
| `AT*TZ=<zone\|POSIX rule>` / `AT*TZ=?` | Time zone; `?` lists the named zones. Saved; factory Pacific/Auckland |
| `AT*NTP=<server>` | Time server. Saved; factory `pool.ntp.org` |

### Device, web and updates

| Command | What it does |
|---|---|
| `AT*PASS` | New random device password, shown once on this port (not from the browser) |
| `AT*PASS=<text>` | Set the device password: 8-32 printable characters. Masked as you type |
| `AT*OTA[?]` | OTA policy and state |
| `AT*OTA=<ALWAYS\|DEMAND>` | OTA always listening, or only on demand. Saved; factory ALWAYS |
| `AT*OTA=<ON\|OFF>` | In DEMAND mode, open OTA for 10 minutes / close it |
| `AT*WEB[?]` / `=<ON\|OFF>` | Web interface on or off. Saved; factory ON |
| `AT*FACTORY` | Erase everything (settings, Wi-Fi, phonebook, history, passwords) and restart. Asks Y/N, then Enter, within 30 seconds |
| `AT*RESTART` | Restart, after Y/N |
| `AT*VER` | Firmware version and commit |

The device password (user `admin`) protects the web interface and OTA uploads. A new one
is made at first boot and printed on the USB console at each boot until it is first used
or changed.

### Configuration screen and terminal

| Command | What it does |
|---|---|
| `AT*CONFIG` | Open the configuration screen now (`ATDT123` opens it after dial sounds) |
| `AT*STYLE[?]` / `=<ANSI\|CP437\|PLAIN>` | Screen style for this port. This boot |
| `AT*SIZE[?]` / `=<cols>,<rows>` | Screen size for this port, 1-132 by 1-50. This boot |
| `AT*STATUS` | Model, front panel, ports, call and Wi-Fi |
| `AT*SETTING[?]` / `=<modem command>` | Show the profiles, or change a setting through the normal checks (`AT*SETTING=S0=2`) |
| `AT*HELP` / `AT*?` | List project commands and hardware tests |
| `AT*REFERENCE` | All AT and AT* commands |
| `AT*ABOUT` | Version, credits and licences |
| `AT*SOUNDS` | List generated tones and sound files |
| `AT*PLAY=<name\|speed>` | Play `DIAL`, `BUSY`, `RING`, `DTMF`, `PULSE`, `SWEEP`, a line speed (handshake) or a `/sounds/...` file. Any key stops it |

### Logs and diagnostics

| Command | What it does |
|---|---|
| `AT*LOG` | Show the last 8 KB of the debug log (events only, never typed text, passwords or call data) |
| `AT*LOG?` / `=<0\|1\|2>` | Log level: 0 off, 1 calls and changes, 2 also diagnostics. This boot |
| `AT*LOG=CLEAR` / `=<offset>,<count>` | Clear the log / read part of it (up to 8192 bytes) |
| `AT*CALLLOG` | The last 50 calls, kept across restarts |
| `AT*DNS=<host>` | Look up a name. Any key cancels |
| `AT*PING=<host>` | Ping a host (not during an Internet call). Any key cancels |
| `AT*RESETINFO` | Why the modem last restarted, and any crash summary |
| `AT*EXPORT` | Print commands that restore this configuration, without passwords |

To restore an export, paste it in command mode with no call or menu open; join Wi-Fi and
set the device password separately. Export uses these restore commands, which you don't
normally type yourself: `AT*BOOKCLEAR`, `AT*PROFILE=<slot>,<baud>`,
`AT*PROFILECTL=<slot>,<cr>,<lf>,<q>,<v>`, `AT*EXPLICIT=<0|1>` and
`AT*RESTOREEND=<baud>,<q>,<v>,<cr>,<lf>`.

### Client Tests and file transfers

Run on the USB console, RS232 port or USB modem port, not in the browser. Each port keeps
its own client settings (from v2.0 there are three; settings saved before v2.0 go back to
defaults once).

| Command | What it does |
|---|---|
| `AT*TEST ANSI` / `PC` / `PLAIN` / `LATIN1` | Character and colour test screens |
| `AT*TEST SIZE` | Measure the screen size |
| `AT*TEST KEYS` / `ID` / `ECHO` | Show key codes / ask the terminal's identity / echo test (Ctrl-] ends) |
| `AT*TEST BELL` / `FORMAT` | Bell / show the serial format |
| `AT*TEST TX=<s>` / `RX=<s>` | Throughput test, 1-30 seconds |
| `AT*TEST CABLE` | Check RS232 handshake lines (RS232 only; restores after six seconds) |
| `AT*TEST SUMMARY` | Results so far |
| `AT*TEST SET=<ANSI\|CP437\|PLAIN\|LATIN1>,<cols>,<rows>,<8\|127>` | Stage client settings: charset, size, Backspace code |
| `AT*TEST APPLY` / `RESET` | Save the staged settings for this port / back to defaults (asks Y/N) |
| `AT*SX=<source>[,SUM\|CRC\|1K[,bytes]]` | Send by XMODEM |
| `AT*SY=<source\|ALL>[,bytes]` | Send by YMODEM (ALL = log and all handshake sounds) |
| `AT*SZ=<source\|ALL>[,bytes]` | Send by ZMODEM |
| `AT*RX[=SUM\|CRC\|1K]` / `AT*RY` / `AT*RZ` | Receive by XMODEM / YMODEM / ZMODEM, check and discard |

Sources: `TEST` (generated, 1 byte to 16 MB), `LOG`, `HS1200.WAV`, `HS2400.WAV`,
`HS9600.WAV`, `HS14400.WAV`, `HS28800.WAV`, `HS33600.WAV`, `HS56000.WAV`. Received files are
never stored. Cancel a transfer with the protocol's own cancel (Ctrl-X several times).

### Hardware tests (`AT*HW...`)

On the USB console and in the browser all tests run. On the RS232 port the line, loopback
and bridge tests are not available. Remote callers can't run any. Audio tests need the
modem idle.

| Command | What it does |
|---|---|
| `AT*HW?` | List the hardware tests |
| `AT*HWTEST` | Guided test of LEDs, speaker, full volume, pot, RS232 lines and loopback |
| `AT*HWPINS` | GPIO pin map |
| `AT*HWINFO` | Chip, memory, firmware, reset reason, LittleFS |
| `AT*HWFILES` | Files in LittleFS |
| `AT*HWLED=<AA\|CD\|RD\|SD\|TR\|CS\|ARQ\|ALL\|OFF\|CHASE\|WALK>` | Light LEDs; `AT*HWLED=AUTO` gives them back to the modem |
| `AT*HWAMP=<AUTO\|ON\|OFF>` | Amplifier shutdown pin (AUTO: on only while playing) |
| `AT*HWVOL=<0-100\|POT>` | Test volume, % of full volume (default 75) |
| `AT*HWTONE=<Hz>[,<ms>]` / `DIAL` / `BUSY` / `RING` / `SWEEP` | Play a tone |
| `AT*HWDTMF=<digits>` / `AT*HWPULSE=<digits>` | Play DTMF / UK pulse clicks |
| `AT*HWPOT[=TONE]` | Live pot reading (TONE: a 1 kHz tone follows the knob) |
| `AT*HWLINES` | Live DTR/RTS; toggle DCD/RI/CTS |
| `AT*HWSIG=<DCD\|RI\|CTS>,<ON\|OFF>` / `AT*HWSIG=AUTO` | Set one RS232 output / give them back |
| `AT*HWLOOP[=<baud>\|ALL]` | Loopback test; needs a plug joining RXD-TXD, CTS-RTS, DCD-DTR (DB25 3-2, 5-4, 8-20) |
| `AT*HWBRIDGE[=<baud>]` | Pass bytes between the console and RS232 (default 38400; Ctrl-X ends) |

**Hardware tests cannot freeze the modem** (v2.3):

- A test that waits for a key (`AT*HWLED` patterns, `AT*HWTONE`, `AT*HWDTMF`, `AT*HWPULSE`,
  `AT*HWPOT`, `AT*HWLINES`, `AT*HWBRIDGE`, the `AT*HWTEST` prompts) stops by itself after
  **5 minutes with no key**, printing `Stopped: no key for 5 minutes`.
- It also stops when its terminal goes away: DTR drops on the RS232 or USB port, or the
  browser closes or leaves. The console UART has no DTR, so only the time-out applies there.
- During a call on another port, `AT*HWLED=CHASE`, `=WALK` and `AT*HWLED` with no argument
  are refused (`ERROR`, "LED patterns are not available during a call."). Lighting one LED,
  `ALL`, `OFF`, `NONE` and `AUTO` still work. Tones, volume and the other audio tests are
  refused during a call, as before.
- When the browser leaves, it gives back only the LEDs, amp mode and RS232 outputs it set
  itself. Holds made from the console, USB or RS232 stay until `AUTO`.

There is no volume cap: full volume is full scale, which the 2 W speaker can take.
