// On-board checks for the USB modem port driver (v2.0, lib/hw/src/usb_port.h):
//   pio test -e s3 -f embedded/test_usb
// Results come back over the UART console port. Plug a PC into the rear USB-C socket (or
// the S3's own native USB-C) with nothing opening the new COM port; if a program opens it
// within the first seconds, the "closed" checks are skipped.
#include <Arduino.h>
#include <unity.h>

#include "usb_port.h"

void setUp() {}
void tearDown() {}

void test_driver_starts_once() {
    TEST_ASSERT_TRUE(hw::usbBegin());
    TEST_ASSERT_TRUE(hw::usbStarted());
    TEST_ASSERT_TRUE(hw::usbBegin());  // a second call is harmless
}

void test_pc_enumerates_the_port() {
    const uint32_t start = millis();
    while (!hw::usbMounted() && millis() - start < 5000) {
        delay(10);
    }
    if (!hw::usbMounted()) {
        TEST_IGNORE_MESSAGE("no PC on the native USB port");
    }
}

void test_closed_port_takes_no_output_and_counts_as_drained() {
    if (!hw::usbMounted() || hw::usbDtr()) {
        TEST_IGNORE_MESSAGE("needs a PC with the port closed");
    }
    const uint8_t text[] = "never sent";
    TEST_ASSERT_EQUAL_UINT32(0, hw::usbWriteSpace());
    TEST_ASSERT_EQUAL_UINT32(0, hw::usbWrite(text, sizeof text));
    TEST_ASSERT_TRUE(hw::usbTxDrained());
    TEST_ASSERT_FALSE(hw::usbRts());
}

void test_reads_without_data_return_nothing() {
    hw::usbDiscardInput();
    uint8_t byte = 0;
    TEST_ASSERT_EQUAL_UINT32(0, hw::usbRead(&byte, 0));
    if (!hw::usbDtr()) {
        TEST_ASSERT_EQUAL_UINT32(0, hw::usbAvailable());
        TEST_ASSERT_EQUAL_UINT32(0, hw::usbRead(&byte, 1));
    }
}

void setup() {
    delay(2000);  // give the USB-serial monitor time to attach
    UNITY_BEGIN();
    RUN_TEST(test_driver_starts_once);
    RUN_TEST(test_pc_enumerates_the_port);
    RUN_TEST(test_closed_port_takes_no_output_and_counts_as_drained);
    RUN_TEST(test_reads_without_data_return_nothing);
    UNITY_END();
}

void loop() {}
