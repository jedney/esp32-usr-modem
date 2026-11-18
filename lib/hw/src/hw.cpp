#include "hw.h"

#include <driver/gpio.h>
#include <driver/i2s_std.h>
#include <driver/rmt_rx.h>
#include <driver/uart.h>
#include <esp_heap_caps.h>
#include <esp_task_wdt.h>
#include <freertos/FreeRTOS.h>
#include <freertos/message_buffer.h>
#include <freertos/task.h>

#include <atomic>

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

constexpr size_t kBlockFrames = 256;  // 16 ms at 16 kHz
constexpr uint32_t kDmaBuffers = 6;   // 96 ms queued at most
// A blocking write waits for one DMA buffer to play out; this only runs out if the I2S
// clock has stopped.
constexpr uint32_t kWriteTimeoutMs = 200;
i2s_chan_handle_t i2sTx = nullptr;  // set once the channel runs

// Fills every DMA buffer with silence. The channel must be stopped (READY state).
bool i2sPreloadSilence(i2s_chan_handle_t tx) {
    static const int16_t zeros[kBlockFrames * 2] = {};
    size_t loaded = 0;
    do {
        if (i2s_channel_preload_data(tx, zeros, sizeof zeros, &loaded) != ESP_OK) {
            return false;
        }
    } while (loaded == sizeof zeros);
    return true;
}

bool i2sBegin() {
    if (i2sTx != nullptr) {
        return true;
    }
    i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan.dma_desc_num = kDmaBuffers;
    chan.dma_frame_num = kBlockFrames;
    chan.auto_clear_after_cb = true;  // an underrun plays silence, not the last buffer again
    i2s_chan_handle_t tx = nullptr;
    if (i2s_new_channel(&chan, &tx, nullptr) != ESP_OK) {
        return false;
    }
    // Philips I2S, 16-bit stereo. Both slots carry the same sample, so the amp plays it
    // whichever channel its SD level selects (left, right or (L+R)/2).
    i2s_std_config_t std = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(tones::kSampleRate),
        .slot_cfg =
            I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg =
            {
                .mclk = I2S_GPIO_UNUSED,
                .bclk = static_cast<gpio_num_t>(pins::kI2sBclk),
                .ws = static_cast<gpio_num_t>(pins::kI2sLrc),
                .dout = static_cast<gpio_num_t>(pins::kI2sDin),
                .din = I2S_GPIO_UNUSED,
                .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false},
            },
    };
    if (i2s_channel_init_std_mode(tx, &std) != ESP_OK || i2s_channel_enable(tx) != ESP_OK) {
        i2s_del_channel(tx);
        return false;
    }
    i2sTx = tx;
    return true;
}

// Replaces whatever is still queued for the DAC with silence, at once. The clocks pause
// for a moment while the channel restarts.
void i2sClear() {
    i2s_channel_disable(i2sTx);
    i2sPreloadSilence(i2sTx);
    i2s_channel_enable(i2sTx);
}

void i2sWriteFrames(const int16_t *mono, size_t n, float gain) {
    int16_t stereo[kBlockFrames * 2];
    for (size_t i = 0; i < n; ++i) {
        int16_t s = volume::scale(mono[i], gain);
        stereo[2 * i] = s;
        stereo[2 * i + 1] = s;
    }
    size_t written = 0;
    i2s_channel_write(i2sTx, stereo, n * 2 * sizeof(int16_t), &written, kWriteTimeoutMs);
}

void i2sSilence(uint32_t ms) {
    const int16_t zeros[kBlockFrames] = {};
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
    // 0 to about 3.1 V. Set for every ADC pin: a single pin's attenuation can only be set
    // once a first read has attached it.
    analogSetAttenuation(ADC_11db);
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

void ledsProgress(uint32_t done, uint32_t total) {
    uint8_t mask = hwtest::progressMask(done, total);
    if (!ledsReady || mask != ledMask) {
        ledsSet(mask);
    }
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
    return i2sTx != nullptr;
}

bool audioStreamBegin() {
    if (!i2sBegin()) {
        return false;
    }
    i2sClear();
    ampWrite(ampModeSetting != AmpMode::kOff);
    return true;
}

size_t audioStreamWrite(const int16_t *samples, size_t count, float userGain) {
    if (i2sTx == nullptr) {
        return 0;
    }
    if (count > kBlockFrames) {
        count = kBlockFrames;
    }
    int16_t stereo[kBlockFrames * 2];
    float gain = volume::outputGain(userGain);
    for (size_t i = 0; i < count; ++i) {
        stereo[i * 2] = stereo[i * 2 + 1] = volume::scale(samples[i], gain);
    }
    size_t bytes = 0;  // a time-out of 0: only what fits in the free DMA buffers now
    i2s_channel_write(i2sTx, stereo, count * sizeof(int16_t) * 2, &bytes, 0);
    return bytes / (sizeof(int16_t) * 2);
}

void audioStreamEnd() {
    if (i2sTx != nullptr) {
        i2sClear();
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

// UART1 runs from the 80 MHz bus clock (APB). The S3 copies changed settings (speed, flow
// control) into the UART core by itself only when the core runs from that clock; Arduino
// picks the crystal, where a speed change made while the port runs doesn't take effect
// properly (2400 bps came out at 1980). Set before every begin(): it only applies then.
void uartClock() {
    Serial1.setClockSource(UART_CLK_SRC_APB);
}

}  // namespace

void uartBegin(uint32_t baud) {
    hostOn = false;
    Serial1.end();
    uartClock();
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
    uartClock();
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
size_t hostWriteSpace() {
    return hostOn ? uart().availableForWrite() : 0;
}
size_t hostWrite(const uint8_t *data, size_t length) {
    if (!hostOn || !length)
        return 0;
    // With no TX ring, uart_tx_chars copies only available FIFO bytes and returns
    // immediately even when the computer holds RTS off. All host writers run on
    // the modem loop; FIFO space can only increase between measuring and writing.
    int written = uart_tx_chars(UART_NUM_1, reinterpret_cast<const char *>(data), length);
    return written > 0 ? static_cast<size_t>(written) : 0;
}

bool hostDrain(uint32_t timeoutMs) {
    return !hostOn || uart_wait_tx_done(UART_NUM_1, pdMS_TO_TICKS(timeoutMs)) == ESP_OK;
}

void hostSetBaud(uint32_t baud) {
    if (!hostOn) {
        return;
    }
    Serial1.updateBaudRate(baud);
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
}

bool ctsOutAsserted() {
    if (!hostOn) {
        return lineIsAsserted(hwtest::OutSignal::kCTS);
    }
    const gpio_num_t cts = static_cast<gpio_num_t>(pins::kCts);
    gpio_input_enable(cts);  // the UART drives this pad as an output; read it back
    return gpio_get_level(cts) == 0;
}

// --- Speed capture (RMT) ------------------------------------------------------------------

namespace {

// The channel's own memory (all 192 symbols of the S3's receive memory, ping-pong) is
// copied by the driver into the burst buffer as it fills. A burst of up to kBurstSymbols
// (two runs each) is handed over in one piece once the line has been idle for
// kCaptureIdleUs: a full command line at 10 edges per character fits. A longer burst is
// handed over in pieces of kBurstSymbols.
constexpr size_t kChannelSymbols = 192;
constexpr size_t kBurstSymbols = 1024;
constexpr size_t kBurstBytes = kBurstSymbols * sizeof(rmt_symbol_word_t);
constexpr size_t kQueueBytes = 4 * (kBurstBytes + sizeof(size_t));  // bursts not yet read
static_assert(kCaptureIdleUs <= 32767, "the RMT idle threshold has 15 bits (1 us steps)");

constexpr rmt_receive_config_t kReceive = {
    .signal_range_min_ns = 1000,                   // ignore glitches under 1 us
    .signal_range_max_ns = kCaptureIdleUs * 1000,  // idle this long ends a burst
    .flags = {.en_partial_rx = true},
};

rmt_channel_handle_t rmtChannel = nullptr;
MessageBufferHandle_t bursts = nullptr;  // filled by onReceive(), read by captureRead()
rmt_symbol_word_t *burstBuf = nullptr;   // the driver's receive buffer (internal RAM)
rmt_symbol_word_t *readBuf = nullptr;    // one burst for captureRead() (PSRAM)
std::atomic<bool> rmtRunning{false};

// Runs in the RMT interrupt: queues the burst (dropped if the queue is full) and starts
// the next receive once a burst has ended.
bool onReceive(rmt_channel_handle_t channel, const rmt_rx_done_event_data_t *edata, void *) {
    BaseType_t woken = pdFALSE;
    if (edata->num_symbols > 0) {
        xMessageBufferSendFromISR(bursts, edata->received_symbols,
                                  edata->num_symbols * sizeof(rmt_symbol_word_t), &woken);
    }
    if (edata->flags.is_last && rmtRunning.load()) {
        rmt_receive(channel, burstBuf, kBurstBytes, &kReceive);
    }
    return woken == pdTRUE;
}

bool captureCreate() {
    burstBuf = static_cast<rmt_symbol_word_t *>(
        heap_caps_malloc(kBurstBytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    readBuf = static_cast<rmt_symbol_word_t *>(heap_caps_malloc(kBurstBytes, MALLOC_CAP_SPIRAM));
    bursts = xMessageBufferCreateWithCaps(kQueueBytes, MALLOC_CAP_SPIRAM);
    rmt_rx_channel_config_t cfg = {
        .gpio_num = static_cast<gpio_num_t>(pins::kTxd),  // shared with UART1 RX
        .clk_src = RMT_CLK_SRC_DEFAULT,
        .resolution_hz = 1000000,  // 1 us steps
        .mem_block_symbols = kChannelSymbols,
        .intr_priority = 0,
        .flags = {.invert_in = false, .with_dma = false, .allow_pd = false},
    };
    const rmt_rx_event_callbacks_t callbacks = {.on_recv_done = onReceive};
    if (burstBuf != nullptr && readBuf != nullptr && bursts != nullptr &&
        rmt_new_rx_channel(&cfg, &rmtChannel) == ESP_OK) {
        if (rmt_rx_register_event_callbacks(rmtChannel, &callbacks, nullptr) == ESP_OK) {
            return true;
        }
        rmt_del_channel(rmtChannel);
    }
    rmtChannel = nullptr;
    heap_caps_free(burstBuf);
    heap_caps_free(readBuf);
    if (bursts != nullptr) {
        vMessageBufferDeleteWithCaps(bursts);
    }
    burstBuf = readBuf = nullptr;
    bursts = nullptr;
    return false;
}

}  // namespace

bool captureBegin() {
    if (rmtRunning.load()) {
        return true;
    }
    if (rmtChannel == nullptr && !captureCreate()) {
        return false;
    }
    xMessageBufferReset(bursts);  // the receiver is stopped: nothing writes to it now
    if (rmt_enable(rmtChannel) != ESP_OK) {
        return false;
    }
    rmtRunning = true;
    if (rmt_receive(rmtChannel, burstBuf, kBurstBytes, &kReceive) != ESP_OK) {
        rmtRunning = false;
        rmt_disable(rmtChannel);
        return false;
    }
    return true;
}

void captureEnd() {
    if (rmtRunning.load()) {
        rmtRunning = false;  // onReceive() no longer restarts the receiver
        rmt_disable(rmtChannel);
    }
}

bool captureActive() {
    return rmtRunning.load();
}

size_t captureRead(autobaud::Run *runs, size_t cap) {
    if (!rmtRunning.load() || cap == 0) {
        return 0;
    }
    const size_t count =
        xMessageBufferReceive(bursts, readBuf, kBurstBytes, 0) / sizeof(rmt_symbol_word_t);
    const rmt_symbol_word_t *items = readBuf;
    size_t n = 0;
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
    return n;  // the decoder treats the time after the last run as idle
}

// --- Task watchdog -------------------------------------------------------------------

void feedWatchdog() {
    esp_task_wdt_reset();  // ESP_ERR_NOT_FOUND from a task the watchdog doesn't watch
}

int stackHeadroom(const char *task) {
    TaskHandle_t handle = xTaskGetHandle(task);
    return handle ? static_cast<int>(uxTaskGetStackHighWaterMark(handle)) : -1;  // bytes
}

}  // namespace hw
