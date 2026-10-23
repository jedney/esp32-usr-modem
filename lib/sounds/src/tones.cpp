#include "tones.h"

#include <cctype>
#include <cmath>

namespace tones {

namespace {

constexpr float kTwoPi = 6.283185307f;
constexpr uint32_t kClickMs = 2;

const uint16_t kBusyMs[] = {375, 375};
const uint16_t kRingMs[] = {400, 200, 400, 2000};

int16_t toSample(float v) {
    if (v > 1.0f) {
        v = 1.0f;
    } else if (v < -1.0f) {
        v = -1.0f;
    }
    return static_cast<int16_t>(std::lround(v * 32767.0f));
}

}  // namespace

const ToneSpec kUkDialTone = {350.0f, 450.0f, {nullptr, 0}};
const ToneSpec kUkBusyTone = {400.0f, 0.0f, {kBusyMs, 2}};
const ToneSpec kUkRingTone = {400.0f, 450.0f, {kRingMs, 4}};

bool cadenceOn(const Cadence &c, uint32_t elapsedMs) {
    if (c.count == 0 || c.ms == nullptr) {
        return true;
    }
    uint32_t period = 0;
    for (uint8_t i = 0; i < c.count; ++i) {
        period += c.ms[i];
    }
    if (period == 0) {
        return true;
    }
    uint32_t t = elapsedMs % period;
    for (uint8_t i = 0; i < c.count; ++i) {
        if (t < c.ms[i]) {
            return (i % 2) == 0;  // even entries are "on"
        }
        t -= c.ms[i];
    }
    return false;
}

bool dtmfFreqs(char digit, float *low, float *high) {
    static const char kKeys[] = "123A456B789C*0#D";
    static const float kLow[] = {697.0f, 770.0f, 852.0f, 941.0f};
    static const float kHigh[] = {1209.0f, 1336.0f, 1477.0f, 1633.0f};
    char d = static_cast<char>(toupper(static_cast<unsigned char>(digit)));
    for (int i = 0; i < 16; ++i) {
        if (kKeys[i] == d) {
            *low = kLow[i / 4];
            *high = kHigh[i % 4];
            return true;
        }
    }
    return false;
}

int pulseCount(char digit) {
    if (digit >= '1' && digit <= '9') {
        return digit - '0';
    }
    return digit == '0' ? 10 : -1;
}

// --- Osc -------------------------------------------------------------------------------

void Osc::setFreq(float hz, uint32_t sampleRate) {
    step_ = sampleRate ? hz / static_cast<float>(sampleRate) : 0.0f;
}

float Osc::next() {
    float v = std::sin(kTwoPi * phase_);
    phase_ += step_;
    if (phase_ >= 1.0f) {
        phase_ -= std::floor(phase_);
    }
    return v;
}

// --- ToneSource ------------------------------------------------------------------------

ToneSource::ToneSource(const ToneSpec &spec, uint32_t durationMs, uint32_t sampleRate)
    : spec_(spec), sampleRate_(sampleRate), total_(samplesFor(durationMs, sampleRate)) {
    a_.setFreq(spec_.f1, sampleRate_);
    b_.setFreq(spec_.f2, sampleRate_);
}

ToneSource::ToneSource(float f1, float f2, uint32_t durationMs, uint32_t sampleRate)
    : ToneSource(ToneSpec{f1, f2, {nullptr, 0}}, durationMs, sampleRate) {}

size_t ToneSource::fill(int16_t *out, size_t n) {
    const bool dual = spec_.f2 > 0.0f;
    size_t i = 0;
    for (; i < n; ++i) {
        if (total_ != 0 && pos_ >= total_) {
            break;
        }
        uint32_t ms = static_cast<uint32_t>((pos_ * 1000u) / sampleRate_);
        if (cadenceOn(spec_.cadence, ms)) {
            float v = dual ? 0.5f * (a_.next() + b_.next()) : a_.next();
            out[i] = toSample(v);
        } else {
            a_.reset();  // each burst starts at zero phase: no click at the start
            b_.reset();
            out[i] = 0;
        }
        ++pos_;
    }
    return i;
}

// --- DtmfSource ------------------------------------------------------------------------

DtmfSource::DtmfSource(const char *digits, uint32_t onMs, uint32_t offMs, uint32_t sampleRate)
    : onSamples_(samplesFor(onMs, sampleRate)),
      offSamples_(samplesFor(offMs, sampleRate)),
      sampleRate_(sampleRate) {
    float lo = 0.0f;
    float hi = 0.0f;
    for (const char *p = digits; p && *p && count_ < kMaxDigits; ++p) {
        if (dtmfFreqs(*p, &lo, &hi)) {
            digits_[count_++] = *p;
        }
    }
    digits_[count_] = '\0';
}

size_t DtmfSource::fill(int16_t *out, size_t n) {
    size_t i = 0;
    while (i < n && index_ < count_) {
        if (pos_ == 0) {
            float lo = 0.0f;
            float hi = 0.0f;
            dtmfFreqs(digits_[index_], &lo, &hi);
            lo_.reset();
            hi_.reset();
            lo_.setFreq(lo, sampleRate_);
            hi_.setFreq(hi, sampleRate_);
        }
        out[i++] = pos_ < onSamples_ ? toSample(0.5f * (lo_.next() + hi_.next())) : 0;
        if (++pos_ >= onSamples_ + offSamples_) {
            pos_ = 0;
            ++index_;
        }
    }
    return i;
}

// --- PulseSource -----------------------------------------------------------------------

PulseSource::PulseSource(const char *digits, uint32_t sampleRate) : sampleRate_(sampleRate) {
    for (const char *p = digits; p && *p && count_ < kMaxDigits; ++p) {
        int c = pulseCount(*p);
        if (c > 0) {
            pulses_[count_++] = c;
        }
    }
}

size_t PulseSource::fill(int16_t *out, size_t n) {
    const uint32_t breakS = samplesFor(kPulseBreakMs, sampleRate_);
    const uint32_t pulseS = breakS + samplesFor(kPulseMakeMs, sampleRate_);
    const uint32_t gapS = samplesFor(kPulseGapMs, sampleRate_);
    const uint32_t clickS = samplesFor(kClickMs, sampleRate_);
    size_t i = 0;
    while (i < n && index_ < count_) {
        const uint32_t pulsesS = static_cast<uint32_t>(pulses_[index_]) * pulseS;
        float v = 0.0f;
        if (pos_ < pulsesS && clickS > 0) {
            uint32_t k = pos_ % pulseS;
            if (k < clickS) {  // line breaks: sharp click
                float t = 1.0f - static_cast<float>(k) / static_cast<float>(clickS);
                v = t * t;
            } else if (k >= breakS && k < breakS + clickS) {  // line makes: softer click
                float t = 1.0f - static_cast<float>(k - breakS) / static_cast<float>(clickS);
                v = -0.5f * t * t;
            }
        }
        out[i++] = toSample(v);
        if (++pos_ >= pulsesS + gapS) {
            pos_ = 0;
            ++index_;
        }
    }
    return i;
}

// --- SweepSource -----------------------------------------------------------------------

SweepSource::SweepSource(float f0, float f1, uint32_t durationMs, uint32_t sampleRate)
    : f0_(f0),
      ratio_(f0 > 0.0f ? f1 / f0 : 1.0f),
      total_(samplesFor(durationMs, sampleRate)),
      sampleRate_(sampleRate) {}

size_t SweepSource::fill(int16_t *out, size_t n) {
    size_t i = 0;
    for (; i < n && pos_ < total_; ++i, ++pos_) {
        if ((pos_ % 32) == 0) {  // update the frequency every 2 ms
            float t = static_cast<float>(pos_) / static_cast<float>(total_);
            osc_.setFreq(f0_ * std::pow(ratio_, t), sampleRate_);
        }
        out[i] = toSample(osc_.next());
    }
    return i;
}

}  // namespace tones
