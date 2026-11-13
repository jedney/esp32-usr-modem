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
