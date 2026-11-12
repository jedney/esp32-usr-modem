#pragma once
#include <LittleFS.h>
#include <driver/uart.h>
#include "catalog.h"
#include "client_service.h"
#include "diagnostics.h"
#include "hw.h"
#include "usb_port.h"

namespace hw {
class ClientPlatform : public clienttest::Platform, public clienttest::Source {
public:
    // rs232: the RS232 port (signals, UART1). usb: the USB modem port (v2.0). Neither: the
    // console (UART0).
    ClientPlatform(config::Log &log, bool rs232, bool usb = false)
        : log_(log), rs232_(rs232), usb_(usb) {}
    void begin(uint32_t baud, bool ctsFromBuffer, bool obeyRts) {
        baud_ = baud;
        cts_ = ctsFromBuffer;
        rts_ = obeyRts;
        dcd_ = lineIsAsserted(hwtest::OutSignal::kDCD);
        ri_ = lineIsAsserted(hwtest::OutSignal::kRI);
    }
    clienttest::Source *source(const char *selection, uint32_t generatedSize) override;
    void release() override;
    uint32_t baud() const override { return baud_; }
    bool rts() const override { return usb_ ? usbRts() : rs232_ && rtsAsserted(); }
    bool dtr() const override { return usb_ ? usbDtr() : rs232_ && dtrAsserted(); }
    void signal(unsigned which, bool on) override {
        if (!rs232_ || which > 2)
            return;
        if (which == 2) {
            hostSetFlow(false, rts_);
            uart_set_rts(UART_NUM_1, on ? 1 : 0);
        }
        lineSet(which == 0   ? hwtest::OutSignal::kDCD
                : which == 1 ? hwtest::OutSignal::kRI
                             : hwtest::OutSignal::kCTS,
                on);
    }
    void restore() override {
        if (rs232_) {
            lineSet(hwtest::OutSignal::kDCD, dcd_);
            lineSet(hwtest::OutSignal::kRI, ri_);
            hostSetFlow(cts_, rts_);
        }
    }
    bool drained() const override {
        if (usb_) {
            return usbTxDrained();
        }
        return rs232_ ? uart_wait_tx_done(UART_NUM_1, 0) == ESP_OK
                      : uart_wait_tx_done(UART_NUM_0, 0) == ESP_OK;
    }
    unsigned count() const override { return count_; }
    const char *name(unsigned file) const override;
    uint32_t size(unsigned file) const override;
    size_t read(unsigned file, uint32_t offset, uint8_t *out, size_t cap) override;

private:
    config::Log &log_;
    bool rs232_, usb_, cts_ = false, rts_ = false, dcd_ = false, ri_ = false;
    uint32_t baud_ = 115200, generatedSize_ = 0;
    unsigned count_ = 0, selected_ = 0;
    char *snapshot_ = nullptr;
    size_t snapshotSize_ = 0;
    uint32_t sizes_[sounds::kAssetCount + 1] = {};  // log + one per sound (ALL)
};
}  // namespace hw
