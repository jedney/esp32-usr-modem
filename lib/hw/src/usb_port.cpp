// The USB modem port: see usb_port.h.
//
// The USB device comes from Arduino's TinyUSB layer (USB.begin, esp32-hal-tinyusb), with
// TinyUSB from components/arduino_tinyusb. Arduino's USBCDC class is not used: it copies
// incoming data into a queue that drops bytes when full, and it reboots into the
// bootloader when a program sets 1200 baud or toggles DTR/RTS in a certain order. Here
// the CDC interface is registered directly and TinyUSB's CDC functions are polled, so
// neither can happen.
#include "usb_port.h"

#include <USB.h>
#include <cstring>

#include "esp32-hal-tinyusb.h"

namespace hw {

namespace {

constexpr uint8_t kItf = 0;
bool started = false;

// The CDC-ACM interface pair (endpoints 0x03 out, 0x84 in; tinyusb_enable_interface()
// reserves them for USB_INTERFACE_CDC). The notification endpoint is not Arduino's 0x85:
// the S3's USB core has transmit FIFOs only for IN endpoints 0 to 4, so a notification
// queued on 0x85 never leaves and the endpoint stays busy. A free IN endpoint with a FIFO
// is taken instead (0x83 with only this interface). Adapted from Arduino-ESP32 3.3.12
// cores/esp32/USBCDC.cpp load_cdc_descriptor(), Copyright 2015-2020 Espressif Systems
// (Shanghai) PTE LTD, Apache-2.0. Changed: the interface name, the notification endpoint
// and its size, and the line layout.
constexpr uint8_t kNotifySize = 16;  // one SERIAL_STATE notification is 10 bytes

uint16_t loadDescriptor(uint8_t *dst, uint8_t *itf) {
    uint8_t name = tinyusb_add_string_descriptor("USR Modem");
    uint8_t notify = tinyusb_get_free_in_endpoint();
    notify = notify ? (0x80 | notify) : 0x85;  // none free: DCD, DSR and RI don't reach the PC
    uint8_t descriptor[TUD_CDC_DESC_LEN] = {
        TUD_CDC_DESCRIPTOR(*itf, name, notify, kNotifySize, 0x03, 0x84, CFG_TUD_ENDPOINT_SIZE)};
    *itf += 2;
    memcpy(dst, descriptor, TUD_CDC_DESC_LEN);
    return TUD_CDC_DESC_LEN;
}

}  // namespace

bool usbBegin(const char *product, const char *manufacturer, const char *serial) {
    if (started) {
        return true;
    }
    if (tinyusb_enable_interface(USB_INTERFACE_CDC, TUD_CDC_DESC_LEN, loadDescriptor) != ESP_OK) {
        return false;
    }
    // Arduino copies the strings when the device starts; the vendor and product IDs stay
    // Arduino's (Espressif's).
    USB.manufacturerName(manufacturer);
    USB.productName(product);
    USB.serialNumber(serial);
    started = USB.begin();
    return started;
}

bool usbStarted() {
    return started;
}

bool usbMounted() {
    return started && tud_ready();
}

bool usbDtr() {
    return usbMounted() && tud_cdc_n_connected(kItf);
}

bool usbRts() {
    return usbMounted() && (tud_cdc_n_get_line_state(kItf) & 0x02) != 0;
}

uint32_t usbBaud() {
    if (!usbMounted()) {
        return 0;
    }
    cdc_line_coding_t coding = {};
    tud_cdc_n_get_line_coding(kItf, &coding);
    return coding.bit_rate;
}

// Read, available and flush only while a computer has configured the device. TinyUSB then
// queues the next OUT transfer, and queuing one on an endpoint a bus reset has closed
// (nothing on the rear USB-C, or a power-only lead) is not safe: an earlier ESP32-S3
// driver divided by that endpoint's packet size of 0 and the modem crash-looped.
size_t usbAvailable() {
    return usbMounted() ? tud_cdc_n_available(kItf) : 0;
}

size_t usbRead(uint8_t *data, size_t cap) {
    if (!usbMounted() || cap == 0) {
        return 0;
    }
    return tud_cdc_n_read(kItf, data, cap);
}

size_t usbWriteSpace() {
    return usbDtr() ? tud_cdc_n_write_available(kItf) : 0;
}

size_t usbWrite(const uint8_t *data, size_t length) {
    if (!usbDtr() || length == 0) {
        return 0;
    }
    size_t n = tud_cdc_n_write(kItf, data, length);
    tud_cdc_n_write_flush(kItf);
    return n;
}

bool usbTxDrained() {
    return !usbDtr() || tud_cdc_n_write_available(kItf) >= CFG_TUD_CDC_TX_BUFSIZE;
}

bool usbSendSerialState(bool dcd, bool dsr, bool ri) {
    if (!usbMounted()) {
        return false;
    }
    cdc_notify_uart_state_t state = {};
    state.bRxCarrier = dcd ? 1 : 0;
    state.bTxCarrier = dsr ? 1 : 0;
    state.bRingSignal = ri ? 1 : 0;
    return tud_cdc_n_notify_uart_state(kItf, &state);
}

void usbDiscardInput() {
    if (usbMounted()) {
        tud_cdc_n_read_flush(kItf);
    }
}

}  // namespace hw
