// On-board checks of the hardware layer (spec 3.13). These pass with nothing
// wired to the S3, so they check the drivers, not the electronics; AT*HWTEST checks the
// electronics. Run with the board connected: pio test -e s3
#include <Arduino.h>
#include <unity.h>

#include "hw.h"
#include "pins.h"
#include "tones.h"

void setUp() {}
void tearDown() {}

namespace {
bool stopNow() {
    return true;
}
float half() {
    return 0.5f;
}
}  // namespace

void test_i2s_starts_and_plays() {
    tones::ToneSource src(1000.0f, 0.0f, 50);
    uint32_t start = millis();
    TEST_ASSERT_TRUE(hw::audioPlay(src, half, nullptr));
    TEST_ASSERT_TRUE(hw::audioReady());
    TEST_ASSERT_TRUE(millis() - start >= 50);  // really waited for the DMA
}

void test_audio_stop_is_honoured() {
    tones::ToneSource src(1000.0f, 0.0f, 0);  // endless
    TEST_ASSERT_FALSE(hw::audioPlay(src, half, stopNow));
}

void test_amp_auto_leaves_amp_off() {
    hw::ampSetMode(hw::AmpMode::kAuto);
    tones::ToneSource src(500.0f, 0.0f, 20);
    hw::audioPlay(src, half, nullptr);
    TEST_ASSERT_FALSE(hw::ampIsOn());
}

void test_pot_reads_in_range() {
    TEST_ASSERT_TRUE(hw::potRaw() <= hw::kPotMax);
    TEST_ASSERT_TRUE(hw::potMillivolts() <= 3400);
}

void test_lines_start_deasserted() {
    hw::linesBegin();
    TEST_ASSERT_FALSE(hw::lineIsAsserted(hwtest::OutSignal::kDCD));
    TEST_ASSERT_FALSE(hw::lineIsAsserted(hwtest::OutSignal::kRI));
    TEST_ASSERT_FALSE(hw::lineIsAsserted(hwtest::OutSignal::kCTS));
    TEST_ASSERT_EQUAL_INT(HIGH, digitalRead(pins::kDcd));  // HIGH = off on the TTL side
}

void test_leds_mask_round_trip() {
    hw::ledsSet(0x55);
    TEST_ASSERT_EQUAL_HEX8(0x55, hw::ledsMask());
    hw::ledsSet(0xFF);
    TEST_ASSERT_EQUAL_HEX8(0x7F, hw::ledsMask());  // only 7 LEDs
    hw::ledsSet(0);
}

void setup() {
    delay(2000);  // give the USB-serial monitor time to attach
    UNITY_BEGIN();
    RUN_TEST(test_i2s_starts_and_plays);
    RUN_TEST(test_audio_stop_is_honoured);
    RUN_TEST(test_amp_auto_leaves_amp_off);
    RUN_TEST(test_pot_reads_in_range);
    RUN_TEST(test_lines_start_deasserted);
    RUN_TEST(test_leds_mask_round_trip);
    UNITY_END();
}

void loop() {}
