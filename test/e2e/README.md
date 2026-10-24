# End-to-end tests

These drive a real modem through its serial ports and check the responses.

```powershell
pip install -r test/e2e/requirements.txt
python -m pytest test/e2e --console-port COM5                    # USB console only
python -m pytest test/e2e --console-port COM5 --rs232-port COM7  # + RS232 port (v0.5)
```

Find the COM port numbers in Windows Device Manager → Ports (COM & LPT). The board's
USB-serial chip appears as e.g. "USB-SERIAL CH340" or "CP210x"; the Unitek adapter
appears as "Prolific USB-to-Serial".

Tests that need a port you didn't give are skipped. Close the VS Code serial monitor
first: only one program can open a COM port at a time.

`test_rs232.py` (v0.5) needs both ports and the RS232 section wired (MAX3232s, bench DB9
or DB25, Unitek adapter plugged straight in). It changes only the PC's speed, from 300 to
115200, and checks the modem follows from "AT" alone; it also checks CTS, DCD, DTR
hang-up and a 16 KB echo with RTS/CTS flow control. `test_session.py` needs only the
console: it makes `AT*TESTCALL` calls and checks `+++`, `ATO`, `ATH` and `AT*BAUD`.

The live-dial tests (later versions) call the Amiga Retro BBS,
`www.amigaretro.com:23` (170.64.142.64:23), and need Wi-Fi configured on the modem.
A local test server will cover `BUSY`, `NO ANSWER` and `NO CARRIER`.

`test_wifi.py` (v0.6) needs only the console, plus the Wi-Fi name and password in `.env`
at the repository root (`USR_MODEM_WIFI_SSID`, `USR_MODEM_WIFI_PASS`; copy
`.env.example`). Without them it is skipped. It joins that network through the masked
password prompt (the network stays saved on the modem), pings `usr-modem.local` from the
PC, compares the modem's clock with the PC's, and checks the password appears nowhere:
not in the boot banner, `AT*HELP`, `AT*WIFI?`, `ATI0`-`ATI7`, `AT&V` or any other show
command. The password is never printed, even when a test fails.
