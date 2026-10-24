// Checks the GPIO pin map (lib/hw/src/pins.h) against the S3 board's reserved pins
// (project brief 5.2), so a pin change that would break the board fails before upload.
// Run with: pio test -e native
#include <unity.h>

#include <cstddef>

// lib/hw is built for the ESP32 only, but pins.h itself is plain C++.
#include "../../../lib/hw/src/pins.h"

void setUp() {}
void tearDown() {}

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
    RUN_TEST(test_pin_map_avoids_reserved_pins);
    return UNITY_END();
}
