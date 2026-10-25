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

`test_calls.py` (v0.8) uses a PC TCP server on port 32321 and needs the PC LAN
address reachable from the modem. It checks RAW binary transfer, fragmented Telnet
negotiation, errors, Wi-Fi loss and stable model reporting. Model persistence needs
only COM15. The live BBS test is opt-in and calls `www.amigaretro.com:23`.

```powershell
python -m pytest test/e2e/test_calls.py --console-port COM15 --call-server-ip 10.0.0.208 --live-bbs --tb=short
```

Windows may require a scoped inbound rule for the actual test Python executable.
`scripts/allow_call_tests.ps1 -ModemAddress 10.0.0.221` creates the documented
TCP 32321 rule from that modem only on Domain/Private profiles, when run as
Administrator after approval. Tests do not alter firewall settings. Remove the rule
when no longer needed with `Remove-NetFirewallRule -DisplayName 'USR modem local
call tests TCP 32321'`. Use current LAN addresses, not these example addresses.

TCP refusal is covered by the normal call suite. On Windows the listener uses
`SO_CONDITIONAL_ACCEPT` / `WSAAccept` to explicitly reject requests while the existing
program-scoped Python rule applies. A filtered closed port would test timeout instead.
To run just refusal and DNS checks, use a normal PowerShell:

```powershell
./scripts/test_refusal.ps1 -ServerAddress 10.0.0.208 -ConsolePort COM15
```

The helper does not change firewall settings or require Administrator access. It
records secret-safe pytest output in `.pio/v08-refusal.log`. DNS failure also runs
independently in the normal suite.

`test_wifi.py` (v0.6) needs only the console, plus the Wi-Fi name and password in `.env`
at the repository root (`USR_MODEM_WIFI_SSID`, `USR_MODEM_WIFI_PASS`; copy
`.env.example`). Without them it is skipped. It joins that network through the masked
password prompt (the network stays saved on the modem), pings `usr-modem.local` from the
PC, compares the modem's clock with the PC's, and checks the password appears nowhere:
not in the boot banner, `AT*HELP`, `AT*WIFI?`, `ATI0`-`ATI7`, `AT&V` or any other show
command. The password is never printed, even when a test fails.

`test_device.py` (v0.7) checks the password, OTA policy and factory reset. Its default
checks are read-only. Password changes require `--device-tests`; the reset additionally
requires `--factory-reset`, and real firmware uploads require `--ota-upload`. The reset
erases all settings, rejoins the Wi-Fi in `.env`, and leaves modem settings at defaults.
The configured OTA password is restored afterwards; if none exists, one is generated.
`--save-ota-password` saves that final credential to the git-ignored `.env`, preserving
the other entries, so the VS Code OTA task works afterwards.

```powershell
python -m pytest test/e2e/test_device.py --console-port COM15 --device-tests --factory-reset --ota-upload --save-ota-password --tb=short
```

Uploads use `pio run -e s3_ota -t upload`, including the native-test gate for each
attempt. The suite verifies wrong/old credentials fail, correct/new credentials work,
and the first successful authenticated upload removes the first-boot password. The
10-minute boundary and millisecond rollover are tested with a fake clock on the PC.
Do not enable pytest `--showlocals`: test locals hold credentials. Failure messages
omit boot banners and upload output. COM18 is the current Unitek port; pass it only
after the MAX3232/DB9 section is wired.
