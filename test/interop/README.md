# Independent transfer acceptance

`client_peer.cpp` is a POSIX byte-transport adapter around the production portable
transfer engine. `run_lrzsz.py` checks every XMODEM variant in both directions,
YMODEM/ZMODEM batches, binary content/CRC and empty files against independent
lrzsz processes. It does not implement a second copy of the firmware protocol.

On Linux or Ubuntu in WSL:

```sh
g++ -std=c++17 -Wall -Wextra -Ilib/clienttest/src -Ilib/calls/src test/interop/client_peer.cpp lib/clienttest/src/transfer.cpp -o .pio/client-peer
python3 test/interop/run_lrzsz.py --peer .pio/client-peer
```

Install lrzsz first. `USR_LRZSZ_RX` and `USR_LRZSZ_TX` can point to alternate
receiver/sender executables, for example a local current upstream build.

The PTY input gives lrzsz terminal semantics. Protocol output has a separate pipe:
lrzsz's exit-time TCIOFLUSH must not discard the final ACK from a pseudo-terminal.
Raw output never shares a stream with diagnostic text.

On Windows, `serial_lrzsz.py --port COM18 --baud 38400` connects the real modem to
Ubuntu lrzsz through `stdio_lrzsz.py` and Windows pyserial. No network port,
firewall change or WSL serial-device mapping is used. Only the Windows process
opens COM18. Close PuTTY/monitors first. The console remains COM15.

The wired runner uses generated data under ignored `.pio/client-acceptance/`,
checks received content and the modem's discard-sink CRC, and prints statistics.
It does not save profiles or change credentials. Amiga acceptance remains a
separate physical check.

## PPP Internet interoperability

On the wired Windows bench, close monitors and run:

```powershell
python test/interop/serial_pppd.py --console-port COM15 --rs232-port COM18 --server-ip 10.0.0.208
```

The existing Ubuntu pppd 2.4.9 runs as root in a temporary network namespace,
connected by binary stdin/stdout pipes to Windows pyserial. No WSL COM mapping,
global route/DNS change or firewall edit is needed. Controlled HTTP uses the
existing PC test allowance for Python TCP 32321; supply the actual PC LAN address.
The runner checks a 64 KiB file, two concurrent 8 KiB flows, upstream UDP DNS,
the actual NAT source and peer LCP hang-up. When the existing device credential
is available it also checks authenticated local HTTP while PPP is active.
Only metadata/hashes are printed, never serial frames or credentials.

Linux CI builds `internet_peer.cpp` against the portable core and runs
`sudo python3 test/interop/run_pppd.py --peer .pio/internet-peer`. The real
pppd/kernel stack sends full binary UDP through PPP and negotiates LCP hang-up
inside a disposable namespace. This tests protocol interoperability separately
from physical NAPT; it does not simulate the ESP network stack. Required packages
are g++, Python, iproute2 and ppp, with kernel PPP support and `/dev/ppp`.

Real Windows 9x/Amiga stack/browser acceptance remains deferred by Jon because
neither client is available. Automated modern-stack results do not close it.

Complete packaged-sound batch regression:

```powershell
python test/interop/serial_lrzsz.py --port COM18 --baud 115200 --sounds
```

Use the matching host UART speed (AUTO can detect the AT prefix). This downloads
ALL through both YMODEM and ZMODEM, checks all seven WAVs against `data/sounds`, and
checks aggregate file count/bytes/CRC including the frozen log. It writes only
under `.pio/client-acceptance`; it does not replace firmware or saved settings.
