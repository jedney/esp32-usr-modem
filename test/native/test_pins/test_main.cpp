// Checks the GPIO pin map (lib/hw/src/pins.h): it is the map Electronics confirmed (Q76,
// schematic rev A), and it avoids the S3 board's reserved pins (project brief 5.2), so a
// pin change, by accident or one that would break the board, fails before upload.
// Run with: pio test -e native
#include <unity.h>

#include <cstddef>

// lib/hw is built for the ESP32 only, but pins.h itself is plain C++.
#include "../../../lib/hw/src/pins.h"

void setUp() {}
void tearDown() {}

// The confirmed map (Q76, 24 Oct 2026, schematic rev A). If Electronics changes the
// board, change pins.h and these numbers together.
void test_pin_map_is_the_confirmed_one() {
    TEST_ASSERT_EQUAL_INT(4, pins::kRxd);
    TEST_ASSERT_EQUAL_INT(5, pins::kTxd);
    TEST_ASSERT_EQUAL_INT(6, pins::kCts);
    TEST_ASSERT_EQUAL_INT(7, pins::kRts);
    TEST_ASSERT_EQUAL_INT(15, pins::kDtr);
    TEST_ASSERT_EQUAL_INT(16, pins::kDcd);
    TEST_ASSERT_EQUAL_INT(17, pins::kRi);
    TEST_ASSERT_EQUAL_INT(11, pins::kI2sLrc);
    TEST_ASSERT_EQUAL_INT(12, pins::kI2sBclk);
    TEST_ASSERT_EQUAL_INT(13, pins::kI2sDin);
    TEST_ASSERT_EQUAL_INT(14, pins::kAmpSd);
    TEST_ASSERT_EQUAL_INT(10, pins::kPot);
    // AA, CD, RD, SD, TR, CS, ARQ
    const int leds[7] = {1, 2, 42, 41, 40, 39, 38};
    TEST_ASSERT_EQUAL_INT_ARRAY(leds, pins::kLeds, 7);
    TEST_ASSERT_TRUE(pins::kLedActiveHigh);
    TEST_ASSERT_EQUAL_INT(18, pins::kTestUartTx);
    TEST_ASSERT_EQUAL_INT(8, pins::kTestUartRx);
}

void test_pin_map_avoids_reserved_pins() {
    // Strapping 0/3/45/46, PSRAM 35-37, USB 19/20, console 43/44, RGB LED 47 (brief 5.2)
    const int reserved[] = {0, 3, 45, 46, 35, 36, 37, 19, 20, 43, 44, 47};
    int used[] = {pins::kRxd,     pins::kTxd,     pins::kCts,     pins::kRts,     pins::kDtr,
                  pins::kDcd,     pins::kRi,      pins::kI2sBclk, pins::kI2sLrc,  pins::kI2sDin,
                  pins::kAmpSd,   pins::kPot,     pins::kLeds[0], pins::kLeds[1], pins::kLeds[2],
                  pins::kLeds[3], pins::kLeds[4], pins::kLeds[5], pins::kLeds[6]};
    for (int u : used) {
        for (int r : reserved) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(r, u, "pin map uses a reserved GPIO");
        }
    }
    for (size_t i = 0; i < sizeof used / sizeof used[0]; ++i) {
        for (size_t j = i + 1; j < sizeof used / sizeof used[0]; ++j) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(used[i], used[j], "GPIO used twice");
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(pins::kPot >= 1 && pins::kPot <= 10, "pot must be on ADC1");
    // The on-board RS232 test's UART2 pins must be spare ones.
    const int testPins[] = {pins::kTestUartTx, pins::kTestUartRx};
    for (int t : testPins) {
        for (int u : used) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(u, t, "test UART pin is a used GPIO");
        }
        for (int r : reserved) {
            TEST_ASSERT_NOT_EQUAL_MESSAGE(r, t, "test UART pin is a reserved GPIO");
        }
    }
    for (int u : used) {
        TEST_ASSERT_TRUE_MESSAGE(u >= 0 && u <= 48, "not an ESP32-S3 GPIO");
        TEST_ASSERT_FALSE_MESSAGE(u >= 22 && u <= 34,
                                  "GPIO22-25 do not exist; 26-34 are used by the flash and PSRAM");
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_pin_map_is_the_confirmed_one);
    RUN_TEST(test_pin_map_avoids_reserved_pins);
    return UNITY_END();
}
