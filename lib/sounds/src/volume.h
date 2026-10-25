// Volume scaling and the hard speaker cap (project brief 5.3, 6.4).
//
// Every sound goes through outputGain(), which multiplies by the cap last, so no pot,
// ATL or test setting can drive the speaker past its rating.
//
// No Arduino dependencies: this module is unit-tested on the PC.

#pragma once

#include <cstdint>

namespace volume {

// BZ0512, 8 ohm 0.25 W, MAX98357A at 3 dB gain from 5 V: about -7 dB below full scale
// (brief 5.3). Confirm by ear on the bench (no distortion at maximum) and change here.
constexpr float kCapDb = -7.0f;

// The quietest audible setting is this far below the loudest.
constexpr float kRangeDb = 40.0f;

// Levels below this (0..1) are silent, so the pot's end stop is a true mute.
constexpr float kMuteBelow = 0.02f;

float dbToGain(float db);

// Gain of the cap, about 0.447 for -7 dB.
float capGain();

// Level 0..1 (pot position or percent / 100) to gain 0..1 on an audio (log) curve:
// silent below kMuteBelow, then -kRangeDb .. 0 dB.
float levelToGain(float level);

// Pot ADC reading to level 0..1, with a small dead zone at each end so the end stops
// reliably reach 0 and 1.
float potLevel(uint32_t raw, uint32_t maxRaw);
float speakerGain(float pot, unsigned level);  // ATL0/1 low, ATL2 medium, ATL3 high

// The gain actually applied to the samples: user gain (clamped to 0..1) times the cap.
float outputGain(float userGain);

// Scales one sample, with rounding and clipping.
int16_t scale(int16_t sample, float gain);

}  // namespace volume
