#include "hw.h"

#include <driver/i2s.h>

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

void uartBegin(uint32_t baud) {
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

}  // namespace hw
