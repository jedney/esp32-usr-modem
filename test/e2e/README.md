# End-to-end tests

These drive a real modem through its serial ports (and Wi-Fi) and check the responses.

```powershell
pip install -r test/e2e/requirements.txt
python -m pytest test/e2e --console-port COM15                      # USB console only
python -m pytest test/e2e --console-port COM15 --rs232-port COM18   # + RS232 port
```

Find the COM ports in Device Manager → Ports (COM & LPT). The S3 board's USB-serial chip shows as "USB-SERIAL CH340" (or similar); the Unitek adapter as "Prolific USB-to-Serial". Tests that need a port or option you didn't give are skipped. Close the VS Code serial monitor and any terminal (PuTTY) first: only one program can open a COM port.

## Options

| Option | Enables |
|---|---|
| `--console-port COMx` | All console tests (required) |
| `--rs232-port COMy` | RS232 tests through the Unitek adapter, plugged straight into the modem |
| `--rs232-baud <rate>` | RS232 speed the tests start at (default 38400) |
| `--call-server-ip <PC LAN IP>` | Outgoing-call tests against a PC TCP server |
| `--call-server-port <port>` | That server's port (default 32321) |
| `--live-bbs` | One real call to `www.amigaretro.com:23` |
| `--live-internet` | Tests that need the Internet (the DNS lookup in the NAT test) |
| `--web-tests --web-host usr-modem.local` | HTTP/WebSocket tests (device password from `.env`; `--web-host` may be an IP) |
| `--browser-upload` | Install `.pio/build/s3/firmware.bin` through the web interface (with `--web-tests` and a serial console; build it first with `pio run -e s3`) |
| `--flood-tests` | Fourth browser refused, idle connection floods on ports 23 and 80 (with `--web-tests`) |
| `--upload-tests` | A web upload cut off half way must free the modem (with `--web-tests`; writes nothing that can be installed) |
| `--usb-unplug` | Pull the rear USB cable during a call when asked (needs a person; with `--usb-port`) |
| `--udp-tests` | Two optional PPP/SLIP UDP echo cases (PC UDP port 32322 must be reachable) |
| `--device-tests` | Device password changes (use a serial console: a password change or a restart ends a network console session) |
| `--factory-reset` | `AT*FACTORY` (with `--device-tests`); rejoins the Wi-Fi in `.env` |
| `--ota-upload` | Real OTA uploads (with `--device-tests`) |
| `--save-password` | Save the final device password to `.env` as `USR_MODEM_PASSWORD` |

Secrets come from the git-ignored `.env` at the repository root (`USR_MODEM_WIFI_SSID`, `USR_MODEM_WIFI_PASS`, `USR_MODEM_PASSWORD`); never put them on the command line. Test arguments and locals hold credentials, so `conftest.py` always uses short tracebacks without them (`--tb=long` and `--showlocals` are overridden). Failure messages leave out boot banners and upload output.

Every test has a 300 s time-out (`pytest.ini`, plugin `pytest-timeout`; install with `pip install -r test/e2e/requirements.txt`) and the serial ports have a 5 s write time-out, so a silent modem or a missing CTS fails the test instead of hanging the run. A slow test can raise its own limit with `@pytest.mark.timeout(600)`. `--device-tests` refuses to run without a valid `USR_MODEM_PASSWORD` in `.env` unless `--save-password` is given.

## Suites

| File | Needs | Covers |
|---|---|---|
| `test_console.py` | Console | Banner, version, `AT*DIALTEST` |
| `test_settings.py` | Console | AT parser, result codes, S-registers, templates, `ATI`, settings kept across a restart |
| `test_session.py` | Console | `AT*TESTCALL`: `+++`, `ATO`, `ATH`, `AT*BAUD`, LEDs |
| `test_rs232.py` | Both ports | Speed detection 300-115200 from "AT", CTS, DCD, DTR hang-up, 16 KB echo with RTS/CTS |
| `test_hwtest.py` | Console (bridge: both ports) | `AT*HW...` commands, pin map, bridge |
| `test_wifi.py` | Console, Wi-Fi in `.env` | Join through the masked prompt, `ping usr-modem.local`, clock, the password appears in no output, prompt ownership across both ports |
| `test_device.py` | Console | Device password, OTA policy; with options, password changes, factory reset and authenticated uploads |
| `test_calls.py` | Console, `--call-server-ip` | RAW binary calls, Telnet negotiation, BUSY/NO ANSWER/NO DIAL TONE/NO CARRIER, Wi-Fi loss, model reporting, ring and connect delays, hostname digits |
| `test_ssh.py` | Console, an SSH server with password login (`USR_MODEM_SSH_TEST_HOST`, `_USER`, `_PASS` in `.env`) | Basic SSH: phonebook entry, export, login prompts, wrong password, shell round trip, terminal size, `+++`/`ATH`, direct dial to port 22, `AT*STATUS` kind and stack headroom |
| `test_answer.py` | Console | Calls the modem on port 23 from the PC: `ATA`, `S0`, binary data, remote-caller isolation, extra callers, Wi-Fi recovery, `AT*ANSWERPORT` |
| `test_config.py` | Console (RS232 case: both ports) | Configuration screen styles, dial 123, escape/ATO, help and hardware screens, caller rejection, export, no idle repaint |
| `test_client.py` | Both ports | Client Tests, per-port client settings kept across a restart, RTS pauses, cable lines |
| `test_internet.py` | Both ports | PPP/SLIP and aliases, LCP/DTR hang-up, negotiation timeout, NAT round trips, RTS pauses, DNS, settings/export, Wi-Fi loss |
| `test_usb.py` | `--usb-port` (cross-port and AUTO answer cases: also `--rs232-port`) | USB modem port: AT session, 16 KB echo call, `+++`/`ATH`, DTR drop hangs up, 1200 baud and DTR/RTS toggles don't reboot, one call across ports, `AT*ANSWERPORT=AUTO` picks USB or RS232 by DTR, DCD, DSR and RI reach the PC |
| `test_web.py` | Both ports, `--web-tests` | Form login, sessions, logout, login lines on the console, socket tickets, one menu at a time, read-only views, CSRF, line controls |

The suites restore what they change (answer port, model, styles, active settings, client records). The normal web suite never uploads images, changes passwords or erases history.

## Outgoing calls and the Windows firewall

`test_calls.py` runs a TCP server on the PC (port 32321) that the modem calls. Windows may need an inbound rule for the test Python: `scripts/allow_call_tests.ps1 -ModemAddress <modem IP>` (Administrator) creates one limited to that modem on private networks. Remove it with `Remove-NetFirewallRule -DisplayName 'USR modem local call tests TCP 32321'`. The tests never change firewall settings themselves. Refusal (BUSY) uses conditional accept, so it works with that rule.

```powershell
python -m pytest test/e2e/test_calls.py --console-port COM15 --call-server-ip 10.0.0.208 --live-bbs
```

## Device tests

```powershell
python -m pytest test/e2e/test_device.py --console-port COM15 --device-tests --factory-reset --ota-upload --save-password
```

The factory reset erases all settings, rejoins the Wi-Fi in `.env` and leaves modem settings at defaults. The configured device password is restored afterwards, or a new one is generated (`--save-password` stores it). Uploads use `pio run -e s3_ota -t upload`, with the unit-test gate each time. Wrong and old passwords must fail, and the first successful upload stops the boot password display.

## Other tools

- `test/interop/serial_lrzsz.py`: real X/Y/ZMODEM transfers against lrzsz.
- `test/interop/serial_pppd.py`: independent PPP and kernel TCP/IP (HTTP, DNS) through the modem.

See `test/interop/README.md`. Real PC terminal and Amiga rendering, the loopback plug, the speaker and the front-panel lights need a person to check them.
