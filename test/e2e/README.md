# End-to-end tests

These drive a real modem through its serial ports and check the responses.

```powershell
pip install -r test/e2e/requirements.txt
python -m pytest test/e2e --console-port COM5                    # USB console only
python -m pytest test/e2e --console-port COM5 --rs232-port COM7  # + RS232 (later versions)
```

Find the COM port numbers in Windows Device Manager → Ports (COM & LPT). The board's
USB-serial chip appears as e.g. "USB-SERIAL CH340" or "CP210x"; the Unitek adapter
appears as "Prolific USB-to-Serial".

Tests that need a port you didn't give are skipped. Close the VS Code serial monitor
first: only one program can open a COM port at a time.

The live-dial tests (later versions) call the Amiga Retro BBS,
`www.amigaretro.com:23` (170.64.142.64:23), and need Wi-Fi configured on the modem.
A local test server will cover `BUSY`, `NO ANSWER` and `NO CARRIER`.
