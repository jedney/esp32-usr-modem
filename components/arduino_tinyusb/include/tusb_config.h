/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org),
 * Additions Copyright (c) 2020, Espressif Systems (Shanghai) PTE LTD
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

// TinyUSB settings for the USB modem port: ESP32-S3 full-speed device, CDC-ACM only.
//
// Based on components/arduino_tinyusb/include/tusb_config.h of Espressif's
// esp32-arduino-lib-builder (branch release/v6.1, commit dc06dec270dd). Changed for this
// project: device only (no host stack), only the CDC class, the S3 only. CDC serial-state
// notifications are on (Arduino 3.3.12 turns them off): the USB modem port sends DCD, DSR
// and RI to the PC with them (lib/hw/src/usb_port.h).
//
// TinyUSB includes this file from tusb_option.h, after the OPT_* values are defined, so
// it must not include tusb_option.h itself.
#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

// Common. The tinyusb component sets CFG_TUSB_MCU for the components that use it;
// PlatformIO compiles the project's lib/ folder without that option.
#ifndef CFG_TUSB_MCU
#define CFG_TUSB_MCU OPT_MCU_ESP32S3
#endif
#define CFG_TUSB_OS OPT_OS_FREERTOS
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN TU_ATTR_ALIGNED(4)

// Device on the S3's only USB OTG port (rhport 0), full speed; no host.
#define CFG_TUD_ENABLED 1
#define CFG_TUH_ENABLED 0
#define CFG_TUD_MAX_SPEED OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT_SIZE 64  // bulk endpoint size at full speed (Arduino's CDC glue)
#define CFG_TUD_ENDPOINT0_SIZE 64

// Classes: CDC only.
#if CONFIG_TINYUSB_CDC_ENABLED
#define CFG_TUD_CDC 1
#define CFG_TUD_CDC_RX_BUFSIZE CONFIG_TINYUSB_CDC_RX_BUFSIZE
#define CFG_TUD_CDC_TX_BUFSIZE CONFIG_TINYUSB_CDC_TX_BUFSIZE
#else
#define CFG_TUD_CDC 0
#endif
#define CFG_TUD_CDC_NOTIFY 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_AUDIO 0
#define CFG_TUD_VIDEO 0
#define CFG_TUD_VENDOR 0
#define CFG_TUD_DFU 0
#define CFG_TUD_DFU_RUNTIME 0
#define CFG_TUD_NCM 0
#define CFG_TUD_ECM_RNDIS 0

#ifdef __cplusplus
}
#endif
