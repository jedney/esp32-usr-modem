#include "volume.h"

#include <cmath>

namespace volume {
float speakerGain(float pot, unsigned level) {
    const float levels[] = {0.25f, 0.25f, 0.5f, 1.0f};
    return levelToGain(pot) * levels[level < 4 ? level : 3];
}

namespace {
constexpr float kDeadZone = 0.01f;  // of the pot's travel, at each end
}

float dbToGain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

float capGain() {
    return dbToGain(kCapDb);
}

float levelToGain(float level) {
    if (level < kMuteBelow) {
        return 0.0f;
    }
    if (level >= 1.0f) {
        return 1.0f;
    }
    // kMuteBelow -> -kRangeDb, 1 -> 0 dB
    float t = (level - kMuteBelow) / (1.0f - kMuteBelow);
    return dbToGain((t - 1.0f) * kRangeDb);
}

float potLevel(uint32_t raw, uint32_t maxRaw) {
    if (maxRaw == 0) {
        return 0.0f;
    }
    float x = static_cast<float>(raw) / static_cast<float>(maxRaw);
    x = (x - kDeadZone) / (1.0f - 2.0f * kDeadZone);
    if (x < 0.0f) {
        return 0.0f;
    }
    return x > 1.0f ? 1.0f : x;
}

float outputGain(float userGain) {
    if (userGain < 0.0f) {
        userGain = 0.0f;
    } else if (userGain > 1.0f) {
        userGain = 1.0f;
    }
    return userGain * capGain();
}

int16_t scale(int16_t sample, float gain) {
    float v = std::round(static_cast<float>(sample) * gain);
    if (v > 32767.0f) {
        return 32767;
    }
    if (v < -32768.0f) {
        return -32768;
    }
    return static_cast<int16_t>(v);
}

}  // namespace volume
