#include "hw.h"

#include <driver/i2s.h>
#include <driver/rmt.h>
#include <driver/uart.h>
#include <hal/uart_ll.h>
#include <soc/gpio_periph.h>

#include "pins.h"
#include "volume.h"

namespace hw {

namespace {

// --- LEDs --------------------------------------------------------------------------------

bool ledsReady = false;
uint8_t ledMask = 0;

void ledsBegin() {
    for (int pin : pins::kLeds) {
        pinMode(pin, OUTPUT);
        digitalWrite(pin, pins::kLedActiveHigh ? LOW : HIGH);
    }
    ledsReady = true;
}

// --- Amp ---------------------------------------------------------------------------------

bool ampReady = false;
bool ampOn = false;
AmpMode ampModeSetting = AmpMode::kAuto;

void ampWrite(bool on) {
    if (!ampReady) {
        pinMode(pins::kAmpSd, OUTPUT);
        ampReady = true;
    }
    digitalWrite(pins::kAmpSd, on ? HIGH : LOW);
    ampOn = on;
}

// --- I2S ---------------------------------------------------------------------------------

constexpr i2s_port_t kI2sPort = I2S_NUM_0;
constexpr size_t kBlockFrames = 256;  // 16 ms at 16 kHz
bool i2sReady = false;

bool i2sBegin() {
    if (i2sReady) {
        return true;
    }
    i2s_config_t cfg = {};
    cfg.mode = static_cast<i2s_mode_t>(I2S_MODE_MASTER | I2S_MODE_TX);
    cfg.sample_rate = tones::kSampleRate;
    cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    // Same sample in both slots, so the amp plays it whichever channel its SD level
    // selects (left, right or (L+R)/2).
    cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    cfg.intr_alloc_flags = 0;
    cfg.dma_buf_count = 6;
    cfg.dma_buf_len = kBlockFrames;
    cfg.use_apll = false;
    cfg.tx_desc_auto_clear = true;  // underrun plays silence, not the last buffer again
    if (i2s_driver_install(kI2sPort, &cfg, 0, nullptr) != ESP_OK) {
        return false;
    }
    i2s_pin_config_t p = {};
    p.mck_io_num = I2S_PIN_NO_CHANGE;
    p.bck_io_num = pins::kI2sBclk;
    p.ws_io_num = pins::kI2sLrc;
    p.data_out_num = pins::kI2sDin;
    p.data_in_num = I2S_PIN_NO_CHANGE;
    if (i2s_set_pin(kI2sPort, &p) != ESP_OK) {
        i2s_driver_uninstall(kI2sPort);
        return false;
    }
    i2s_zero_dma_buffer(kI2sPort);
    i2sReady = true;
    return true;
}

void i2sWriteFrames(const int16_t *mono, size_t n, float gain) {
    int16_t stereo[kBlockFrames * 2];
    for (size_t i = 0; i < n; ++i) {
        int16_t s = volume::scale(mono[i], gain);
        stereo[2 * i] = s;
        stereo[2 * i + 1] = s;
    }
    size_t written = 0;
    i2s_write(kI2sPort, stereo, n * 2 * sizeof(int16_t), &written, portMAX_DELAY);
}

void i2sSilence(uint32_t ms) {
    int16_t zeros[kBlockFrames] = {};
    uint32_t frames = tones::samplesFor(ms);
    while (frames > 0) {
        size_t n = frames < kBlockFrames ? frames : kBlockFrames;
        i2sWriteFrames(zeros, n, 0.0f);
        frames -= n;
    }
}

// --- Pot ---------------------------------------------------------------------------------

bool potReady = false;

void potBegin() {
    analogReadResolution(12);
    analogSetPinAttenuation(pins::kPot, ADC_11db);  // 0 to about 3.1 V
    potReady = true;
}

// --- RS232 lines -------------------------------------------------------------------------

bool linesReady = false;
bool outState[3] = {false, false, false};

int outPin(hwtest::OutSignal s) {
    switch (s) {
        case hwtest::OutSignal::kDCD:
            return pins::kDcd;
        case hwtest::OutSignal::kRI:
            return pins::kRi;
        case hwtest::OutSignal::kCTS:
        default:
            return pins::kCts;
    }
}

}  // namespace

// --- LEDs --------------------------------------------------------------------------------

void ledsSet(uint8_t mask) {
    if (!ledsReady) {
        ledsBegin();
    }
    ledMask = mask & ((1u << hwtest::kLedCount) - 1);
    for (uint8_t i = 0; i < hwtest::kLedCount; ++i) {
        bool lit = (ledMask >> i) & 1u;
        digitalWrite(pins::kLeds[i], lit == pins::kLedActiveHigh ? HIGH : LOW);
    }
}

uint8_t ledsMask() {
    return ledMask;
}

// --- Amp ---------------------------------------------------------------------------------

void ampSetMode(AmpMode mode) {
    ampModeSetting = mode;
    ampWrite(mode == AmpMode::kOn);
}

AmpMode ampMode() {
    return ampModeSetting;
}

bool ampIsOn() {
    return ampOn;
}

// --- Audio -------------------------------------------------------------------------------

bool audioReady() {
    return i2sReady;
}

bool audioStreamBegin() {
    if (!i2sBegin()) {
        return false;
    }
    i2s_zero_dma_buffer(kI2sPort);
    ampWrite(ampModeSetting != AmpMode::kOff);
    return true;
}

size_t audioStreamWrite(const int16_t *samples, size_t count, float userGain) {
    if (!i2sReady) {
        return 0;
    }
    if (count > kBlockFrames) {
        count = kBlockFrames;
    }
    int16_t stereo[kBlockFrames * 2];
    float gain = volume::outputGain(userGain);  // cap last, including WAV playback
    for (size_t i = 0; i < count; ++i) {
        stereo[i * 2] = stereo[i * 2 + 1] = volume::scale(samples[i], gain);
    }
    size_t bytes = 0;
    i2s_write(kI2sPort, stereo, count * sizeof(int16_t) * 2, &bytes, 0);
    return bytes / (sizeof(int16_t) * 2);
}

void audioStreamEnd() {
    if (i2sReady) {
        i2s_zero_dma_buffer(kI2sPort);
    }
    if (ampModeSetting != AmpMode::kOn) {
        ampWrite(false);
    }
}

bool audioPlay(tones::Source &src, GainFn gain, StopFn stop) {
    if (!i2sBegin()) {
        return false;
    }
    const bool autoAmp = ampModeSetting == AmpMode::kAuto;
    if (ampModeSetting == AmpMode::kOff) {
        ampWrite(false);  // muted on purpose: the I2S signals still run (scope/analyser)
    } else if (!ampOn) {
        i2sSilence(10);
        ampWrite(true);
        i2sSilence(20);  // the amp starts up in about 1 ms; let it settle before sound
    }

    int16_t block[kBlockFrames];
    bool finished = true;
    for (;;) {
        if (stop && stop()) {
            finished = false;
            break;
        }
        size_t n = src.fill(block, kBlockFrames);
        float g = volume::outputGain(gain ? gain() : 1.0f);
        i2sWriteFrames(block, n, g);
        if (n < kBlockFrames) {
            break;
        }
    }
    // Let the DMA buffers drain (6 x 16 ms) before the amp is switched off.
    i2sSilence(120);
    if (autoAmp) {
        ampWrite(false);
    }
    return finished;
}

// --- Pot ---------------------------------------------------------------------------------

uint16_t potRaw() {
    if (!potReady) {
        potBegin();
    }
    uint32_t sum = 0;
    for (int i = 0; i < 16; ++i) {
        sum += static_cast<uint32_t>(analogRead(pins::kPot));
    }
    return static_cast<uint16_t>(sum / 16);
}

uint32_t potMillivolts() {
    if (!potReady) {
        potBegin();
    }
    return analogReadMilliVolts(pins::kPot);
}

// --- RS232 lines -------------------------------------------------------------------------

void linesBegin() {
    if (linesReady) {
        return;
    }
    // The MAX3232 receivers drive these; the pull-ups only matter when nothing is fitted,
    // and read as "not asserted".
    pinMode(pins::kDtr, INPUT_PULLUP);
    pinMode(pins::kRts, INPUT_PULLUP);
    for (int i = 0; i < 3; ++i) {
        int pin = outPin(static_cast<hwtest::OutSignal>(i));
        pinMode(pin, OUTPUT);
        digitalWrite(pin, HIGH);  // HIGH = not asserted
        outState[i] = false;
    }
    linesReady = true;
}

bool dtrAsserted() {
    linesBegin();
    return digitalRead(pins::kDtr) == LOW;
}

bool rtsAsserted() {
    linesBegin();
    return digitalRead(pins::kRts) == LOW;
}

void lineSet(hwtest::OutSignal s, bool asserted) {
    linesBegin();
    digitalWrite(outPin(s), asserted ? LOW : HIGH);
    outState[static_cast<int>(s)] = asserted;
}

bool lineIsAsserted(hwtest::OutSignal s) {
    return outState[static_cast<int>(s)];
}

// --- UART1 (Arduino's Serial1) ---------------------------------------------------------

namespace {

bool hostOn = false;

// On the S3 the UART runs from the 40 MHz crystal, not the 80 MHz bus clock, so changed
// settings (speed, flow control) don't reach the UART core by themselves; Arduino core
// 2.0.17 doesn't do this step (found on the bench, v0.5: a speed change ran at 1980
// instead of 2400 baud). Copy them across by hand.
void uartSync() {
    uart_dev_t *u = UART_LL_GET_HW(1);
    u->id.high_speed = 0;
    u->id.reg_update = 1;
    while (u->id.reg_update) {
    }
    u->id.high_speed = 1;
}

}  // namespace

void uartBegin(uint32_t baud) {
    hostOn = false;
    Serial1.end();
    Serial1.setRxBufferSize(2048);
    Serial1.begin(baud, SERIAL_8N1, pins::kTxd, pins::kRxd);  // RX pin, TX pin
}

void uartEnd() {
    Serial1.end();
}

HardwareSerial &uart() {
    return Serial1;
}

// --- RS232 host port ----------------------------------------------------------------------

void hostBegin(uint32_t baud) {
    linesBegin();
    Serial1.end();
    Serial1.setRxBufferSize(kHostRxBuffer);
    Serial1.setTxBufferSize(kHostTxBuffer);
    Serial1.begin(baud, SERIAL_8N1, pins::kTxd, pins::kRxd);
    // Attach the flow-control pins through the IDF driver: the Arduino core remembers
    // pins across end() and may skip attaching them again.
    uart_set_pin(UART_NUM_1, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE, pins::kCts, pins::kRts);
    hostOn = true;
    hostSetFlow(false, false);
}

void hostEnd() {
    if (!hostOn) {
        return;
    }
    // Release flow-control state before tearing down this idle port, including
    // when the computer is absent or holds RTS off.
    uart_set_hw_flow_ctrl(UART_NUM_1, UART_HW_FLOWCTRL_DISABLE, 100);
    Serial1.end();
    hostOn = false;
    // Back to plain GPIO, deasserted (HIGH).
    pinMode(pins::kCts, OUTPUT);
    digitalWrite(pins::kCts, HIGH);
    pinMode(pins::kRts, INPUT_PULLUP);
}

bool hostActive() {
    return hostOn;
}

bool hostDrain(uint32_t timeoutMs) {
    return !hostOn || uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(timeoutMs)) == ESP_OK;
}

void hostSetBaud(uint32_t baud) {
    if (!hostOn) {
        return;
    }
    Serial1.updateBaudRate(baud);
    uartSync();
}

void hostSetFlow(bool ctsFromBuffer, bool obeyRts) {
    if (!hostOn) {
        return;
    }
    uart_hw_flowcontrol_t mode = UART_HW_FLOWCTRL_DISABLE;
    if (ctsFromBuffer && obeyRts) {
        mode = UART_HW_FLOWCTRL_CTS_RTS;
    } else if (ctsFromBuffer) {
        mode = UART_HW_FLOWCTRL_RTS;
    } else if (obeyRts) {
        mode = UART_HW_FLOWCTRL_CTS;
    }
    // RTS drops when the 128-byte FIFO holds 100 bytes, which only happens once the
    // driver's buffer is full and it stops emptying the FIFO.
    uart_set_hw_flow_ctrl(UART_NUM_1, mode, 100);
    if (!ctsFromBuffer) {
        uart_set_rts(UART_NUM_1, 1);  // 1 = RTS output low = our CTS asserted
    }
    uartSync();
}

bool ctsOutAsserted() {
    if (!hostOn) {
        return lineIsAsserted(hwtest::OutSignal::kCTS);
    }
    PIN_INPUT_ENABLE(GPIO_PIN_MUX_REG[pins::kCts]);  // read the pad the UART drives
    return gpio_get_level(static_cast<gpio_num_t>(pins::kCts)) == 0;
}

// --- Speed capture (RMT) ------------------------------------------------------------------

namespace {

constexpr rmt_channel_t kRmtChannel = RMT_CHANNEL_4;  // the S3's receive channels are 4-7
RingbufHandle_t rmtRing = nullptr;
bool rmtInstalled = false;
bool rmtRunning = false;

void drainRing() {
    size_t size = 0;
    void *item = nullptr;
    while ((item = xRingbufferReceive(rmtRing, &size, 0)) != nullptr) {
        vRingbufferReturnItem(rmtRing, item);
    }
}

}  // namespace

bool captureBegin() {
    if (!rmtInstalled) {
        rmt_config_t c = {};
        c.rmt_mode = RMT_MODE_RX;
        c.channel = kRmtChannel;
        c.gpio_num = static_cast<gpio_num_t>(pins::kTxd);  // shared with UART1 RX
        c.clk_div = 80;                                    // 80 MHz / 80 = 1 us
        c.mem_block_num = 4;                               // all of the receive memory
        c.rx_config.idle_threshold = static_cast<uint16_t>(kCaptureIdleUs);
        c.rx_config.filter_ticks_thresh = 80;  // ignore glitches under 1 us
        c.rx_config.filter_en = true;
        if (rmt_config(&c) != ESP_OK || rmt_driver_install(kRmtChannel, 8192, 0) != ESP_OK) {
            return false;
        }
        // Ping-pong: the driver empties the memory as it fills, so a burst can be longer
        // than the 192 items the memory holds (a full 128-character command line).
        rmt_set_rx_thr_intr_en(kRmtChannel, true, 96);
        rmt_get_ringbuf_handle(kRmtChannel, &rmtRing);
        rmtInstalled = true;
    }
    if (!rmtRunning) {
        drainRing();
        rmt_rx_start(kRmtChannel, true);
        rmtRunning = true;
    }
    return true;
}

void captureEnd() {
    if (rmtRunning) {
        rmt_rx_stop(kRmtChannel);
        rmtRunning = false;
    }
}

bool captureActive() {
    return rmtRunning;
}

size_t captureRead(autobaud::Run *runs, size_t cap) {
    if (!rmtRunning || rmtRing == nullptr || cap == 0) {
        return 0;
    }
    size_t size = 0;
    auto *items = static_cast<rmt_item32_t *>(xRingbufferReceive(rmtRing, &size, 0));
    if (items == nullptr) {
        return 0;
    }
    size_t n = 0;
    const size_t count = size / sizeof(rmt_item32_t);
    auto add = [&](uint32_t level, uint32_t us) {
        if (n > 0 && runs[n - 1].level == level && runs[n - 1].us != 0) {
            runs[n - 1].us += us;  // merge (doesn't happen with a clean capture)
        } else if (n < cap) {
            runs[n++] = autobaud::Run{us, static_cast<uint8_t>(level)};
        }
    };
    for (size_t i = 0; i < count; ++i) {
        // A zero duration marks the end: the line stayed at that level until idle.
        add(items[i].level0, items[i].duration0);
        if (items[i].duration0 == 0) {
            break;
        }
        add(items[i].level1, items[i].duration1);
        if (items[i].duration1 == 0) {
            break;
        }
    }
    vRingbufferReturnItem(rmtRing, items);
    return n;  // the decoder treats the time after the last run as idle
}

}  // namespace hw
