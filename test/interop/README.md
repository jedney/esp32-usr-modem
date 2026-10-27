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
