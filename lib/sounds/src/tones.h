// Generated tones (project brief 6.4): UK dial, busy and ring tones, DTMF, UK pulse-dial
// clicks, plain test tones and a frequency sweep. Each is a Source that fills a buffer
// of 16-bit mono samples at full scale; the caller applies the volume (volume.h).
//
// No Arduino dependencies: this module is unit-tested on the PC.

#pragma once

#include <cstddef>
#include <cstdint>

namespace tones {

// Sample rate for generated sounds. 16 kHz covers the tones (all < 2 kHz) and the sweep.
constexpr uint32_t kSampleRate = 16000;

// Highest test-tone frequency (just under the 8 kHz Nyquist limit).
constexpr uint32_t kMaxToneHz = 7000;
constexpr uint32_t kMinToneHz = 20;

// On/off pattern: on ms, off ms, on ms, off ms ... repeating. count == 0 means
// continuously on.
struct Cadence {
    const uint16_t *ms;
    uint8_t count;
};

bool cadenceOn(const Cadence &c, uint32_t elapsedMs);

struct ToneSpec {
    float f1;
    float f2;  // 0 for a single frequency
    Cadence cadence;
};

// UK tones.
extern const ToneSpec kUkDialTone;  // 350 + 450 Hz, continuous
extern const ToneSpec kUkBusyTone;  // 400 Hz, 0.375 s on / 0.375 s off
extern const ToneSpec kUkRingTone;  // 400 + 450 Hz, 0.4 on, 0.2 off, 0.4 on, 2.0 off

// DTMF row and column frequencies for 0-9, *, #, A-D (case-insensitive).
bool dtmfFreqs(char digit, float *low, float *high);

// UK pulse dialling: digit n = n pulses, 0 = 10. Returns -1 for anything else.
int pulseCount(char digit);

// UK pulse timing: 10 pulses per second, 66 ms break / 34 ms make, 800 ms between digits.
constexpr uint32_t kPulseBreakMs = 66;
constexpr uint32_t kPulseMakeMs = 34;
constexpr uint32_t kPulseGapMs = 800;

// Phase-continuous sine oscillator. The frequency can change between samples without
// clicks (used by the sweep).
class Osc {
public:
    void setFreq(float hz, uint32_t sampleRate);
    float next();  // -1..1
    void reset() { phase_ = 0.0f; }

private:
    float phase_ = 0.0f;  // 0..1
    float step_ = 0.0f;
};

class Source {
public:
    virtual ~Source() = default;
    // Writes up to n samples. Returns how many were written; fewer than n means the
    // sound has finished. Sources with no end always return n.
    virtual size_t fill(int16_t *out, size_t n) = 0;
};

// A tone (one or two frequencies) with an optional cadence. durationMs == 0 plays
// until the caller stops it. Two-frequency tones use half amplitude for each, so the
// peak never exceeds full scale.
class ToneSource : public Source {
public:
    ToneSource(const ToneSpec &spec, uint32_t durationMs, uint32_t sampleRate = kSampleRate);
    ToneSource(float f1, float f2, uint32_t durationMs, uint32_t sampleRate = kSampleRate);
    size_t fill(int16_t *out, size_t n) override;

private:
    ToneSpec spec_;
    uint32_t sampleRate_;
    uint64_t total_;  // samples, 0 = endless
    uint64_t pos_ = 0;
    Osc a_, b_;
};

// DTMF for each digit in turn. Characters that aren't DTMF digits are skipped.
class DtmfSource : public Source {
public:
    explicit DtmfSource(const char *digits, uint32_t onMs = 100, uint32_t offMs = 100,
                        uint32_t sampleRate = kSampleRate);
    size_t fill(int16_t *out, size_t n) override;

private:
    static constexpr size_t kMaxDigits = 32;
    char digits_[kMaxDigits + 1];
    size_t count_ = 0;
    size_t index_ = 0;
    uint32_t onSamples_, offSamples_;
    uint32_t pos_ = 0;  // within the current digit
    uint32_t sampleRate_;
    Osc lo_, hi_;
};

// UK pulse-dial clicks for each digit (a click as the line breaks, a softer one as it
// makes), with the inter-digit gap after each digit.
class PulseSource : public Source {
public:
    explicit PulseSource(const char *digits, uint32_t sampleRate = kSampleRate);
    size_t fill(int16_t *out, size_t n) override;

private:
    static constexpr size_t kMaxDigits = 32;
    int pulses_[kMaxDigits];
    size_t count_ = 0;
    size_t index_ = 0;
    uint32_t pos_ = 0;  // within the current digit
    uint32_t sampleRate_;
};

// Logarithmic sweep from f0 to f1 over durationMs: finds speaker resonances and rattles.
class SweepSource : public Source {
public:
    SweepSource(float f0, float f1, uint32_t durationMs, uint32_t sampleRate = kSampleRate);
    size_t fill(int16_t *out, size_t n) override;

private:
    float f0_, ratio_;
    uint32_t total_, pos_ = 0, sampleRate_;
    Osc osc_;
};

// Samples per millisecond-length, rounded down.
constexpr uint32_t samplesFor(uint32_t ms, uint32_t sampleRate = kSampleRate) {
    return static_cast<uint32_t>((static_cast<uint64_t>(ms) * sampleRate) / 1000u);
}

}  // namespace tones
