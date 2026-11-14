// Unit tests for the hardware-test helpers, generated tones and volume scaling
// (project brief 5.3, 6.4, 6.14). Run with: pio test -e native
#include <unity.h>

#include <cmath>
#include <cstdlib>
#include <vector>

#include "hwtest.h"
#include "tones.h"
#include "volume.h"

void setUp() {}
void tearDown() {}

namespace {

std::vector<int16_t> render(tones::Source &src, size_t maxSamples) {
    std::vector<int16_t> out(maxSamples);
    size_t got = 0;
    while (got < maxSamples) {
        size_t chunk = maxSamples - got < 256 ? maxSamples - got : 256;
        size_t n = src.fill(out.data() + got, chunk);
        got += n;
        if (n < chunk) {
            break;
        }
    }
    out.resize(got);
    return out;
}

// Frequency of a single sine from its upward zero crossings.
float estimateHz(const std::vector<int16_t> &s, uint32_t sampleRate) {
    int crossings = 0;
    for (size_t i = 1; i < s.size(); ++i) {
        if (s[i - 1] < 0 && s[i] >= 0) {
            ++crossings;
        }
    }
    return static_cast<float>(crossings) * static_cast<float>(sampleRate) /
           static_cast<float>(s.size());
}

int peak(const std::vector<int16_t> &s) {
    int p = 0;
    for (int16_t v : s) {
        p = std::abs(static_cast<int>(v)) > p ? std::abs(static_cast<int>(v)) : p;
    }
    return p;
}

// Goertzel power at one frequency, normalised so a full-scale sine reads about 1.
float toneLevel(const int16_t *s, size_t n, float hz, uint32_t sampleRate) {
    float k = 2.0f * std::cos(6.283185307f * hz / static_cast<float>(sampleRate));
    float a = 0.0f;
    float b = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float c = static_cast<float>(s[i]) / 32767.0f + k * a - b;
        b = a;
        a = c;
    }
    float power = a * a + b * b - k * a * b;
    return 2.0f * std::sqrt(power) / static_cast<float>(n);
}

}  // namespace

// --- LEDs and arguments ----------------------------------------------------------------

void test_led_names_match_web_protocol_bits() {
    TEST_ASSERT_EQUAL_STRING("AA", hwtest::ledName(0));
    TEST_ASSERT_EQUAL_STRING("CD", hwtest::ledName(1));
    TEST_ASSERT_EQUAL_STRING("RD", hwtest::ledName(2));
    TEST_ASSERT_EQUAL_STRING("SD", hwtest::ledName(3));
    TEST_ASSERT_EQUAL_STRING("TR", hwtest::ledName(4));
    TEST_ASSERT_EQUAL_STRING("CS", hwtest::ledName(5));
    TEST_ASSERT_EQUAL_STRING("ARQ", hwtest::ledName(6));
    TEST_ASSERT_EQUAL_STRING("?", hwtest::ledName(7));
}

void test_parse_led() {
    TEST_ASSERT_EQUAL_INT(hwtest::kCD, hwtest::parseLed("cd"));
    TEST_ASSERT_EQUAL_INT(hwtest::kARQ, hwtest::parseLed("ARQ"));
    TEST_ASSERT_EQUAL_INT(hwtest::kARQ, hwtest::parseLed("fax"));
    TEST_ASSERT_EQUAL_INT(hwtest::kAA, hwtest::parseLed("AA,1"));
    TEST_ASSERT_EQUAL_INT(-1, hwtest::parseLed("AAX"));
    TEST_ASSERT_EQUAL_INT(-1, hwtest::parseLed(""));
    TEST_ASSERT_EQUAL_INT(-1, hwtest::parseLed("ALL"));
}

void test_chase_sweeps_there_and_back() {
    const uint8_t expected[] = {0x01, 0x02, 0x04, 0x08, 0x10, 0x20, 0x40,
                                0x20, 0x10, 0x08, 0x04, 0x02, 0x01, 0x02};
    for (uint32_t i = 0; i < sizeof expected; ++i) {
        TEST_ASSERT_EQUAL_HEX8(expected[i], hwtest::chaseMask(i));
    }
}

void test_arg_start_accepts_equals_or_space() {
    TEST_ASSERT_EQUAL_STRING("CD", hwtest::argStart("=CD"));
    TEST_ASSERT_EQUAL_STRING("CD", hwtest::argStart(" CD"));
    TEST_ASSERT_EQUAL_STRING("CD", hwtest::argStart(" = CD"));
    TEST_ASSERT_EQUAL_STRING("", hwtest::argStart(""));
}

void test_word_is_and_rest() {
    const char *rest = nullptr;
    TEST_ASSERT_TRUE(hwtest::wordIs("dcd, on", "DCD", &rest));
    TEST_ASSERT_EQUAL_STRING("on", rest);
    TEST_ASSERT_TRUE(hwtest::wordIs("CHASE", "CHASE", &rest));
    TEST_ASSERT_EQUAL_STRING("", rest);
    TEST_ASSERT_FALSE(hwtest::wordIs("CHASER", "CHASE"));
    TEST_ASSERT_FALSE(hwtest::wordIs("CH", "CHASE"));
}

void test_parse_uint_ranges() {
    uint32_t v = 0;
    const char *rest = nullptr;
    TEST_ASSERT_TRUE(hwtest::parseUint("1000,250", 20, 7000, &v, &rest));
    TEST_ASSERT_EQUAL_UINT32(1000, v);
    TEST_ASSERT_EQUAL_STRING("250", rest);
    TEST_ASSERT_TRUE(hwtest::parseUint("0", 0, 100, &v));
    TEST_ASSERT_FALSE(hwtest::parseUint("101", 0, 100, &v));
    TEST_ASSERT_FALSE(hwtest::parseUint("19", 20, 7000, &v));
    TEST_ASSERT_FALSE(hwtest::parseUint("12x", 0, 100, &v));
    TEST_ASSERT_FALSE(hwtest::parseUint("", 0, 100, &v));
    TEST_ASSERT_FALSE(hwtest::parseUint("99999999999999999999", 0, 4000000000u, &v));
}

void test_parse_on_off_and_signals() {
    bool on = false;
    TEST_ASSERT_TRUE(hwtest::parseOnOff("on", &on));
    TEST_ASSERT_TRUE(on);
    TEST_ASSERT_TRUE(hwtest::parseOnOff("0", &on));
    TEST_ASSERT_FALSE(on);
    TEST_ASSERT_FALSE(hwtest::parseOnOff("maybe", &on));

    hwtest::OutSignal s = hwtest::OutSignal::kDCD;
    TEST_ASSERT_TRUE(hwtest::parseOutSignal("ri,on", &s));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(hwtest::OutSignal::kRI), static_cast<int>(s));
    TEST_ASSERT_TRUE(hwtest::parseOutSignal("CTS", &s));
    TEST_ASSERT_EQUAL_STRING("CTS", hwtest::outSignalName(s));
    TEST_ASSERT_FALSE(hwtest::parseOutSignal("DSR", &s));  // hard-wired, not testable
}

void test_loopback_pattern_covers_every_byte() {
    bool seen[256] = {};
    for (size_t i = 0; i < 256; ++i) {
        seen[hwtest::loopbackByte(i)] = true;
    }
    for (bool b : seen) {
        TEST_ASSERT_TRUE(b);
    }
    TEST_ASSERT_EQUAL_HEX8(hwtest::loopbackByte(3), hwtest::loopbackByte(259));
}

// --- Tones -----------------------------------------------------------------------------

void test_single_tone_frequency_length_and_peak() {
    tones::ToneSource src(1000.0f, 0.0f, 500);
    std::vector<int16_t> s = render(src, 20000);
    TEST_ASSERT_EQUAL_UINT32(8000, s.size());  // 500 ms at 16 kHz, then it ends
    TEST_ASSERT_FLOAT_WITHIN(4.0f, 1000.0f, estimateHz(s, tones::kSampleRate));
    TEST_ASSERT_INT_WITHIN(50, 32767, peak(s));
}

void test_endless_tone_keeps_filling() {
    tones::ToneSource src(440.0f, 0.0f, 0);
    int16_t buf[256];
    for (int i = 0; i < 200; ++i) {
        TEST_ASSERT_EQUAL_UINT32(256, src.fill(buf, 256));
    }
}

void test_uk_dial_tone_has_both_frequencies_and_no_clipping() {
    tones::ToneSource src(tones::kUkDialTone, 1000);
    std::vector<int16_t> s = render(src, 16000);
    // int16 samples can't exceed full scale, so test for clipping: no run of full-scale samples.
    int clipped = 0;
    for (int16_t v : s) {
        clipped += (v >= 32767 || v <= -32768) ? 1 : 0;
    }
    TEST_ASSERT_TRUE(clipped < 3);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, toneLevel(s.data(), s.size(), 350.0f, 16000));
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.5f, toneLevel(s.data(), s.size(), 450.0f, 16000));
    TEST_ASSERT_TRUE(toneLevel(s.data(), s.size(), 400.0f, 16000) < 0.05f);
}

void test_uk_busy_cadence() {
    const tones::Cadence &c = tones::kUkBusyTone.cadence;
    TEST_ASSERT_TRUE(tones::cadenceOn(c, 0));
    TEST_ASSERT_TRUE(tones::cadenceOn(c, 374));
    TEST_ASSERT_FALSE(tones::cadenceOn(c, 375));
    TEST_ASSERT_FALSE(tones::cadenceOn(c, 749));
    TEST_ASSERT_TRUE(tones::cadenceOn(c, 750));
}

void test_uk_ring_cadence() {
    const tones::Cadence &c = tones::kUkRingTone.cadence;
    TEST_ASSERT_TRUE(tones::cadenceOn(c, 100));    // first burst
    TEST_ASSERT_FALSE(tones::cadenceOn(c, 500));   // 0.2 s gap
    TEST_ASSERT_TRUE(tones::cadenceOn(c, 700));    // second burst
    TEST_ASSERT_FALSE(tones::cadenceOn(c, 1500));  // 2 s pause
    TEST_ASSERT_FALSE(tones::cadenceOn(c, 2999));
    TEST_ASSERT_TRUE(tones::cadenceOn(c, 3000));  // period 3 s
}

void test_busy_tone_is_silent_in_the_gaps() {
    tones::ToneSource src(tones::kUkBusyTone, 750);
    std::vector<int16_t> s = render(src, 16000);
    TEST_ASSERT_EQUAL_UINT32(12000, s.size());
    std::vector<int16_t> on(s.begin(), s.begin() + 6000);
    std::vector<int16_t> off(s.begin() + 6000, s.end());
    TEST_ASSERT_TRUE(peak(on) > 30000);
    TEST_ASSERT_EQUAL_INT(0, peak(off));
}

void test_dtmf_table() {
    float lo = 0.0f;
    float hi = 0.0f;
    TEST_ASSERT_TRUE(tones::dtmfFreqs('1', &lo, &hi));
    TEST_ASSERT_EQUAL_FLOAT(697.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(1209.0f, hi);
    TEST_ASSERT_TRUE(tones::dtmfFreqs('0', &lo, &hi));
    TEST_ASSERT_EQUAL_FLOAT(941.0f, lo);
    TEST_ASSERT_EQUAL_FLOAT(1336.0f, hi);
    TEST_ASSERT_TRUE(tones::dtmfFreqs('#', &lo, &hi));
    TEST_ASSERT_EQUAL_FLOAT(1477.0f, hi);
    TEST_ASSERT_TRUE(tones::dtmfFreqs('d', &lo, &hi));
    TEST_ASSERT_EQUAL_FLOAT(1633.0f, hi);
    TEST_ASSERT_FALSE(tones::dtmfFreqs('E', &lo, &hi));
}

void test_dtmf_sequence_length_and_content() {
    tones::DtmfSource src("1-2,3", 100, 100);  // separators skipped: 3 digits
    std::vector<int16_t> s = render(src, 100000);
    TEST_ASSERT_EQUAL_UINT32(3 * 3200, s.size());
    // Digit 2 = 697 + 1336 Hz, in the second 200 ms slot
    const int16_t *d2 = s.data() + 3200;
    TEST_ASSERT_FLOAT_WITHIN(0.06f, 0.5f, toneLevel(d2, 1600, 697.0f, 16000));
    TEST_ASSERT_FLOAT_WITHIN(0.06f, 0.5f, toneLevel(d2, 1600, 1336.0f, 16000));
    TEST_ASSERT_TRUE(toneLevel(d2, 1600, 1209.0f, 16000) < 0.06f);
}

void test_pulse_counts_uk() {
    TEST_ASSERT_EQUAL_INT(1, tones::pulseCount('1'));
    TEST_ASSERT_EQUAL_INT(9, tones::pulseCount('9'));
    TEST_ASSERT_EQUAL_INT(10, tones::pulseCount('0'));
    TEST_ASSERT_EQUAL_INT(-1, tones::pulseCount('*'));
}

void test_pulse_sequence_timing() {
    tones::PulseSource src("20");  // 2 pulses + gap, 10 pulses + gap
    std::vector<int16_t> s = render(src, 100000);
    const uint32_t pulse = tones::samplesFor(100);
    const uint32_t gap = tones::samplesFor(tones::kPulseGapMs);
    TEST_ASSERT_EQUAL_UINT32(12 * pulse + 2 * gap, s.size());
    // A sharp positive click starts each break
    TEST_ASSERT_TRUE(s[0] > 30000);
    TEST_ASSERT_TRUE(s[pulse] > 30000);
    // ...and silence in the inter-digit gap
    int gapPeak = 0;
    for (uint32_t i = 2 * pulse + 100; i < 2 * pulse + gap; ++i) {
        gapPeak = std::abs(s[i]) > gapPeak ? std::abs(s[i]) : gapPeak;
    }
    TEST_ASSERT_EQUAL_INT(0, gapPeak);
}

void test_sweep_rises_in_frequency() {
    tones::SweepSource src(200.0f, 4000.0f, 2000);
    std::vector<int16_t> s = render(src, 100000);
    TEST_ASSERT_EQUAL_UINT32(32000, s.size());
    std::vector<int16_t> start(s.begin(), s.begin() + 1600);
    std::vector<int16_t> end(s.end() - 1600, s.end());
    float f0 = estimateHz(start, 16000);
    float f1 = estimateHz(end, 16000);
    TEST_ASSERT_TRUE(f0 > 150.0f && f0 < 300.0f);
    TEST_ASSERT_TRUE(f1 > 3000.0f && f1 < 4100.0f);
}

// --- Volume ----------------------------------------------------------------------------

void test_speaker_levels_and_pot_stay_within_full_scale() {
    TEST_ASSERT_EQUAL_FLOAT(0.25f, volume::speakerGain(1, 0));
    TEST_ASSERT_EQUAL_FLOAT(0.25f, volume::speakerGain(1, 1));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, volume::speakerGain(1, 2));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, volume::speakerGain(1, 3));
    for (unsigned level = 0; level < 4; ++level) {
        TEST_ASSERT_EQUAL_FLOAT(0, volume::speakerGain(0, level));
        for (unsigned pot = 0; pot <= 4095; pot += 31) {
            float gain = volume::speakerGain(volume::potLevel(pot, 4095), level);
            TEST_ASSERT_TRUE(gain >= 0.0f && gain <= 1.0f);  // before outputGain() clamps it
        }
    }
}

// No volume cap: full volume is full scale (CLS0361 2 W speaker; see docs/decisions-log.md).
void test_output_gain_is_clamped_with_no_cap() {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, volume::outputGain(1.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, volume::outputGain(5.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.5f, volume::outputGain(0.5f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, volume::outputGain(-1.0f));
    TEST_ASSERT_EQUAL_INT16(32767, volume::scale(32767, volume::outputGain(1.0f)));
    TEST_ASSERT_EQUAL_INT16(-32768, volume::scale(-32768, volume::outputGain(1.0f)));
}

void test_level_curve_mute_monotonic_and_top() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, volume::levelToGain(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, volume::levelToGain(0.019f));
    TEST_ASSERT_FLOAT_WITHIN(0.002f, 0.01f, volume::levelToGain(0.02f));  // -40 dB
    TEST_ASSERT_EQUAL_FLOAT(1.0f, volume::levelToGain(1.0f));
    float last = 0.0f;
    for (int i = 2; i <= 100; ++i) {
        float g = volume::levelToGain(static_cast<float>(i) / 100.0f);
        TEST_ASSERT_TRUE(g > last);
        last = g;
    }
}

void test_pot_level_dead_zones() {
    TEST_ASSERT_EQUAL_FLOAT(0.0f, volume::potLevel(0, 4095));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, volume::potLevel(30, 4095));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, volume::potLevel(4095, 4095));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, volume::potLevel(4060, 4095));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.5f, volume::potLevel(2048, 4095));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, volume::potLevel(100, 0));
}

void test_scale_rounds_and_clips() {
    TEST_ASSERT_EQUAL_INT16(16384, volume::scale(32767, 0.5f));
    TEST_ASSERT_EQUAL_INT16(-16384, volume::scale(-32768, 0.5f));
    TEST_ASSERT_EQUAL_INT16(32767, volume::scale(30000, 2.0f));
    TEST_ASSERT_EQUAL_INT16(-32768, volume::scale(-30000, 2.0f));
    TEST_ASSERT_EQUAL_INT16(0, volume::scale(12345, 0.0f));
}

void test_idle_timeout_is_five_minutes_and_survives_clock_wrap() {
    TEST_ASSERT_EQUAL_UINT32(300000u, hwtest::kIdleTimeoutMs);
    TEST_ASSERT_FALSE(hwtest::idleExpired(1000, 1000));
    TEST_ASSERT_FALSE(hwtest::idleExpired(1000, 1000 + hwtest::kIdleTimeoutMs - 1));
    TEST_ASSERT_TRUE(hwtest::idleExpired(1000, 1000 + hwtest::kIdleTimeoutMs));
    // millis() wraps at 2^32: 10 s before the wrap to 10 s after is 20 s, not 49 days.
    TEST_ASSERT_FALSE(hwtest::idleExpired(0xFFFFFFFFu - 9999u, 10000u));
    TEST_ASSERT_TRUE(hwtest::idleExpired(0xFFFFFFFFu - 9999u, 10000u, 15000u));
    TEST_ASSERT_TRUE(hwtest::idleExpired(0xFFFFFFFFu - 299999u, 1u));
    TEST_ASSERT_TRUE(hwtest::idleExpired(0, 5000, 5000));
}

void test_only_single_led_forms_are_allowed_during_a_call() {
    TEST_ASSERT_TRUE(hwtest::allowedDuringCall("LED=CD"));
    TEST_ASSERT_TRUE(hwtest::allowedDuringCall("LED CD"));
    TEST_ASSERT_TRUE(hwtest::allowedDuringCall("LED=FAX"));
    TEST_ASSERT_TRUE(hwtest::allowedDuringCall("LED=ALL"));
    TEST_ASSERT_TRUE(hwtest::allowedDuringCall("LED=OFF"));
    TEST_ASSERT_TRUE(hwtest::allowedDuringCall("LED=NONE"));
    TEST_ASSERT_TRUE(hwtest::allowedDuringCall("LED=AUTO"));
    // The patterns run until a key, so they would freeze a call.
    TEST_ASSERT_FALSE(hwtest::allowedDuringCall("LED"));
    TEST_ASSERT_FALSE(hwtest::allowedDuringCall("LED="));
    TEST_ASSERT_FALSE(hwtest::allowedDuringCall("LED=CHASE"));
    TEST_ASSERT_FALSE(hwtest::allowedDuringCall("LED=WALK"));
    TEST_ASSERT_FALSE(hwtest::allowedDuringCall("LED CHASE"));
    TEST_ASSERT_FALSE(hwtest::allowedDuringCall("LEDS"));
    TEST_ASSERT_FALSE(hwtest::allowedDuringCall("LED=BOGUS"));
}

void test_other_hardware_commands_during_a_call_are_unchanged() {
    const char *const ok[] = {"POT?",      "PINS",  "INFO", "?",        "",       "SIG",
                              "SIG=CD,ON", "LINES", "LOOP", "LOOP=ALL", "BRIDGE", "BRIDGE=9600"};
    for (const char *c : ok) {
        TEST_ASSERT_TRUE_MESSAGE(hwtest::allowedDuringCall(c), c);
    }
    const char *const no[] = {"TONE=DIAL", "DTMF=123", "PULSE=5", "AMP=ON", "VOL=50",
                              "POT",       "POT=TONE", "TEST",    "FILES"};
    for (const char *c : no) {
        TEST_ASSERT_FALSE_MESSAGE(hwtest::allowedDuringCall(c), c);
    }
}

void test_hold_is_released_only_by_its_owner() {
    hwtest::Hold h;
    TEST_ASSERT_FALSE(h.active());
    TEST_ASSERT_FALSE(h.releaseForBrowser());  // nothing to release

    h.take(true);  // set from the browser
    TEST_ASSERT_TRUE(h.active());
    TEST_ASSERT_TRUE(h.releaseForBrowser());
    TEST_ASSERT_FALSE(h.active());

    h.take(false);  // set from the console, USB or RS232
    TEST_ASSERT_TRUE(h.active());
    TEST_ASSERT_FALSE(h.releaseForBrowser());  // the browser leaving must not cancel it
    TEST_ASSERT_TRUE(h.active());
    h.release();
    TEST_ASSERT_FALSE(h.active());
}

void test_hold_changes_owner_when_taken_again() {
    hwtest::Hold h;
    h.take(false);
    h.take(true);
    TEST_ASSERT_TRUE(h.releaseForBrowser());
    h.take(true);
    h.take(false);  // another port now holds it
    TEST_ASSERT_FALSE(h.releaseForBrowser());
    TEST_ASSERT_TRUE(h.active());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_led_names_match_web_protocol_bits);
    RUN_TEST(test_parse_led);
    RUN_TEST(test_chase_sweeps_there_and_back);
    RUN_TEST(test_arg_start_accepts_equals_or_space);
    RUN_TEST(test_word_is_and_rest);
    RUN_TEST(test_parse_uint_ranges);
    RUN_TEST(test_parse_on_off_and_signals);
    RUN_TEST(test_loopback_pattern_covers_every_byte);
    RUN_TEST(test_single_tone_frequency_length_and_peak);
    RUN_TEST(test_endless_tone_keeps_filling);
    RUN_TEST(test_uk_dial_tone_has_both_frequencies_and_no_clipping);
    RUN_TEST(test_uk_busy_cadence);
    RUN_TEST(test_uk_ring_cadence);
    RUN_TEST(test_busy_tone_is_silent_in_the_gaps);
    RUN_TEST(test_dtmf_table);
    RUN_TEST(test_dtmf_sequence_length_and_content);
    RUN_TEST(test_pulse_counts_uk);
    RUN_TEST(test_pulse_sequence_timing);
    RUN_TEST(test_sweep_rises_in_frequency);
    RUN_TEST(test_speaker_levels_and_pot_stay_within_full_scale);
    RUN_TEST(test_output_gain_is_clamped_with_no_cap);
    RUN_TEST(test_level_curve_mute_monotonic_and_top);
    RUN_TEST(test_pot_level_dead_zones);
    RUN_TEST(test_scale_rounds_and_clips);
    RUN_TEST(test_idle_timeout_is_five_minutes_and_survives_clock_wrap);
    RUN_TEST(test_only_single_led_forms_are_allowed_during_a_call);
    RUN_TEST(test_other_hardware_commands_during_a_call_are_unchanged);
    RUN_TEST(test_hold_is_released_only_by_its_owner);
    RUN_TEST(test_hold_changes_owner_when_taken_again);
    return UNITY_END();
}
