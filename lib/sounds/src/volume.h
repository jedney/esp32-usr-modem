// Volume scaling (spec 2.3, 3.4).
//
// There is no firmware volume cap. The CLS0361 speaker (8 ohm, 2 W) can take everything
// the MAX98357A can give it: at its default 9 dB gain (GAIN pin unconnected) and 5 V,
// full scale is about 3.6 V RMS, 1.6 W into 8 ohm, and the amp clips at about 1.4-1.8 W
// (see docs/decisions-log.md). Fit a smaller speaker or raise the amp gain and this
// needs a cap again.
//
// No Arduino dependencies: this module is unit-tested on the PC.

#pragma once

#include <cstdint>

namespace volume {

// The quietest audible setting is this far below the loudest.
constexpr float kRangeDb = 40.0f;

// Levels below this (0..1) are silent, so the pot's end stop is a true mute.
constexpr float kMuteBelow = 0.02f;

float dbToGain(float db);

// Level 0..1 (pot position or percent / 100) to gain 0..1 on an audio (log) curve:
// silent below kMuteBelow, then -kRangeDb .. 0 dB.
float levelToGain(float level);

// Pot ADC reading to level 0..1, with a small dead zone at each end so the end stops
// reliably reach 0 and 1.
float potLevel(uint32_t raw, uint32_t maxRaw);
float speakerGain(float pot, unsigned level);  // ATL0/1 low, ATL2 medium, ATL3 high

// The gain applied to the samples: the user gain clamped to 0..1 (1 = full scale).
float outputGain(float userGain);

// Scales one sample, with rounding and clipping.
int16_t scale(int16_t sample, float gain);

}  // namespace volume
