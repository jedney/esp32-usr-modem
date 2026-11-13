# WP-20 (v3.0): I2S, RMT capture and first link

| Item | Value |
| --- | --- |
| Model / effort | Opus 5.5 / high |
| Size | L |
| Depends on | WP-19 sessions 1 and 2 (done) |

Read [wp-19-common.md](wp-19-common.md) first (decisions, versions, board testing, rules).

## Modernisation review rows

| Area | Now | Look at |
| --- | --- | --- |
| I2S | legacy `driver/i2s.h` | `i2s_std` channel API (or Arduino's `ESP_I2S` if it meets the non-blocking need) |
| RMT | legacy `driver/rmt.h` | RX channel API, ISR callback into a stream buffer |
| UART | `hal/uart_ll.h`, `uartSync()` | IDF `uart_*` driver calls only; drop `uartSync()` if the bench shows it isn't needed (WP-26 tests a build without it) |

## Steps

1. **I2S** standard-mode driver (`driver/i2s_std.h`): one TX channel, 16-bit stereo, same
   pins and rate, DMA sizes like now; the non-blocking write stays non-blocking; every
   sample through `volume::outputGain()`; MAX98357A SD pin handling unchanged.
2. **RMT speed capture:** `rmt_new_rx_channel` on `pins::kTxd` (shared with UART1 RX),
   1 MHz, `signal_range_min_ns` 1000, `signal_range_max_ns` from `kCaptureIdleUs`
   (32 ms; check the 15-bit limit), largest memory (consider `with_dma`),
   `en_partial_rx` (`rmt_receive` reports `is_last`). The ISR callback copies symbols into
   a FreeRTOS ring or stream buffer; `captureRead()` keeps its contract and run format
   (`autobaud::Run`, merge rule, zero duration = end), so `lib/hostport` and its tests
   don't change. Restart `rmt_receive()` after each `is_last`. With the RGB LED gone, the
   capture is the only RMT user.
3. **First full link.** Record flash and RAM beside v2.17's in the decisions log; check
   the 4 MB app slot.
4. **UART:** speed changes through IDF `uart_*` calls only, no `hal/uart_ll.h`.
   `uartSync()` stays only if it can be written that way. Arduino 3.3.12 changes speed
   through IDF's `uart_set_baudrate()`, which calls `uart_ll_update()`, so it may not be
   needed at all.
5. **First run on the board** (once it links): full erase, USB flash of the firmware and
   LittleFS, setup from `.env`, then a smoke test (boot banner, `ATI`, a test call, the
   sound, `AT*BAUD=AUTO` at one speed, the USB modem port). Problems found here are
   fixed in this session where they belong to its work.

## First board run: also check

From the earlier sessions:

- `crypto.begin()` and the new `test_device` tests (they also prove MD5 and HMAC-SHA-256
  exist in the PSA build).
- The clock syncs after a Wi-Fi join (`esp_netif_sntp`).
- The two `%.1f` dB prints in `src/hwconsole.cpp` (`printVolume()` and the pot read-out)
  under Picolibc.
- The volume pot still reads after `crypto.begin()`.
- **USB modem port:** it enumerates on the PC, `e2e/test_usb.py` passes, and the rear
  USB-C is fine empty and with a power-only lead (the `tud_ready()` guard was for the old
  `dcd_esp32sx.c` crash; the DWC2 driver is new).
- `esp_core_dump_get_summary` (`AT*RESETINFO`) still works.

## Result (13 Nov 2026)

Done. The firmware links and runs on the board. Details and reasons are in the decisions
log (rows "v3.0 I2S, RMT capture and UART speed", "v3.0 first full link" and the three
"found on the first board run" rows).

- **I2S:** `driver/i2s_std.h` channel driver. The legacy "zero the DMA buffers" step is
  now stop, preload silence, start (`i2sClear()`). `ESP_I2S` not used.
- **RMT:** `driver/rmt_rx.h`, partial receive, the interrupt callback queues bursts in a
  PSRAM message buffer. No DMA (reasons in the log). `captureRead()` and `lib/hostport`
  unchanged.
- **UART:** the plan's premise was wrong for the S3 (IDF 6.1 never sets `reg_update`
  there). UART1 now runs from the APB clock, which syncs settings by itself; `uartSync()`
  and `hal/uart_ll.h` are gone.
- **Sizes:** app 1,496,025 bytes (35.7 % of the 4 MB slot; v2.17 1,256,657), static RAM
  151,696 bytes (v2.17 145,232).
- **Fixed on the first run:** `AT*WIFISCAN` (Arduino 3.3 ignores a scan it didn't
  start), the `AT*RESETINFO` crash summary (compiled out), the lwIP stack read-out (task
  renamed `tcpip`), an ADC error on `AT*HWVOL=POT`, and in the e2e tests a Wi-Fi password
  printed by failing tracebacks plus two `test_wifi.py` bugs.
- **Board run (full erase, USB flash, LittleFS, setup from `.env`):** e2e `test_console`,
  `test_wifi` (15), `test_device --device-tests`, `test_rs232` (21: speed detection
  300-115200, flow control, 16 KB echo), `test_settings`, `test_session`, `test_hwtest`,
  `test_config`, `test_client`, `test_answer`, `test_internet` pass. On-board
  `test_device` (8) and `test_hw` (6) pass. The `%.1f` prints, the pot, NTP and
  `AT*RESETINFO` were checked by hand.

**Left for later sessions:**

- **WP-21:** browser WebSockets are reset (12 of 18 `test_web.py` cases fail); added to
  its plan as step 5.
- **WP-25/26:** the USB modem port (rear USB-C not connected), the sound (needs a
  listener), a real crash record in `AT*RESETINFO`, `test_calls.py` (needs the PC call
  server), and the intermittent Q2 failure in `test_answer.py`.
