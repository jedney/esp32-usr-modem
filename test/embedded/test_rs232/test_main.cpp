// On-board checks of the RS232 host port (spec 3.2) with NO wires: UART2 plays
// the computer, connected inside the chip through the GPIO matrix:
//   UART2 TX  -> TXD-in pin (UART1 RX and the RMT speed capture)
//   UART1 TX  -> UART2 RX
//   our CTS   -> UART2 CTS (so the "computer" obeys our flow control)
// RTS-in and DTR-in are driven as GPIO outputs on their own pins.
//
// These drive the port's input pins, so they need a board without the MAX3232s fitted;
// with them fitted the tests are skipped (the end-to-end tests cover a wired port).
// Run with: pio test -e s3 -f embedded/test_rs232   (then upload the firmware again)
#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/uart.h>
#include <esp_rom_gpio.h>
#include <soc/gpio_sig_map.h>
#include <unity.h>

#include <cstring>
#include <string>

#include "autobaud.h"
#include "hw.h"
#include "pins.h"
#include "wifi_radio.h"

namespace {

bool fitted = false;
autobaud::Run runs[2048];
uint32_t current = 38400;

// Does something else drive the pin (a MAX3232 receiver)? With the internal pull-down
// on, an undriven pin reads LOW.
bool driven(int pin) {
    pinMode(pin, INPUT_PULLDOWN);
    delay(2);
    bool high = digitalRead(pin) == HIGH;
    pinMode(pin, INPUT_PULLUP);
    return high;
}

// Drives an input pin from the S3 itself, keeping the pin's input routing (e.g. into
// UART1's CTS input).
void drive(int pin, bool high) {
    esp_rom_gpio_connect_out_signal(pin, SIG_GPIO_OUT_IDX, false, false);
    gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT_OUTPUT);
    gpio_set_level(static_cast<gpio_num_t>(pin), high ? 1 : 0);
}

void release(int pin) {
    gpio_set_direction(static_cast<gpio_num_t>(pin), GPIO_MODE_INPUT);
    gpio_set_pull_mode(static_cast<gpio_num_t>(pin), GPIO_PULLUP_ONLY);
}

void rigBaud(uint32_t b) {
    Serial2.updateBaudRate(b);
}

void portBaud(uint32_t b) {
    hw::hostSetBaud(b);
    current = b;
}

void flushAll() {
    delay(5);
    while (Serial1.available() > 0) {
        Serial1.read();
    }
    while (Serial2.available() > 0) {
        Serial2.read();
    }
    while (hw::captureRead(runs, sizeof runs / sizeof runs[0]) > 0) {
    }
}

std::string readFor(HardwareSerial &s, uint32_t ms) {
    std::string got;
    uint32_t start = millis();
    while (millis() - start < ms) {
        while (s.available() > 0) {
            got += static_cast<char>(s.read());
        }
        delay(1);
    }
    return got;
}

// Sends `text` from the "computer" and returns the decoded capture burst.
autobaud::Burst sendAndCapture(const char *text, uint32_t rate, uint8_t *out, size_t cap) {
    flushAll();
    Serial2.print(text);
    Serial2.flush();
    uint32_t chars = strlen(text);
    delay(hw::kCaptureIdleUs / 1000 + chars * 10 * 1000 / rate + 30);
    size_t n = hw::captureRead(runs, sizeof runs / sizeof runs[0]);
    TEST_ASSERT_TRUE_MESSAGE(n > 0, "nothing captured");
    return autobaud::decodeBurst(runs, n, current, out, cap);
}

}  // namespace

void setUp() {
    if (fitted) {
        TEST_IGNORE_MESSAGE("RS232 receivers fitted: these tests need a board without them");
    }
}
void tearDown() {}

void test_speed_detected_from_at_at_every_rate() {
    // Up from 300, then back down, so every rate is reached from another one.
    static const uint32_t order[] = {300,   600,   1200,   2400,  4800, 9600, 19200,
                                     38400, 57600, 115200, 19200, 1200, 300,  38400};
    for (uint32_t rate : order) {
        rigBaud(rate);
        uint8_t out[16];
        autobaud::Burst b = sendAndCapture("AT\r", rate, out, sizeof out);
        char msg[48];
        snprintf(msg, sizeof msg, "at %u baud", static_cast<unsigned>(rate));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(rate, b.baud, msg);
        TEST_ASSERT_EQUAL_MESSAGE(rate != current, b.changed, msg);
        TEST_ASSERT_EQUAL_MESSAGE(3, b.len, msg);
        TEST_ASSERT_EQUAL_STRING_LEN_MESSAGE("AT\r", reinterpret_cast<char *>(out), 3, msg);
        portBaud(b.baud);

        // The UART follows: the next line arrives intact through UART1 as well.
        flushAll();
        Serial2.print("ATI3\r");
        Serial2.flush();
        std::string got = readFor(Serial1, 5 * 10 * 1000 / rate + 30);
        TEST_ASSERT_EQUAL_STRING_MESSAGE("ATI3\r", got.c_str(), msg);
    }
}

void test_long_line_is_captured_whole() {
    rigBaud(115200);
    portBaud(115200);
    std::string line = "AT*";
    while (line.size() < 140) {
        line += "0123456789abcdefghijklmnopqrstuvwxyz";
    }
    line.resize(140);
    line += "\r";
    uint8_t out[256];
    autobaud::Burst b = sendAndCapture(line.c_str(), 115200, out, sizeof out);
    TEST_ASSERT_EQUAL(line.size(), b.len);
    TEST_ASSERT_EQUAL_STRING_LEN(line.c_str(), reinterpret_cast<char *>(out), line.size());
}

void test_data_to_the_computer() {
    rigBaud(57600);
    portBaud(57600);
    flushAll();
    Serial1.print("CONNECT 33600\r\nhello");
    Serial1.flush();
    TEST_ASSERT_EQUAL_STRING("CONNECT 33600\r\nhello", readFor(Serial2, 20).c_str());
}

void test_cts_drops_when_the_buffer_is_full_and_nothing_is_lost() {
    rigBaud(115200);
    portBaud(115200);
    hw::hostSetFlow(true, false);  // &H1&R1
    flushAll();
    hw::captureEnd();  // online, as in the firmware: data comes through the UART only
    TEST_ASSERT_TRUE(hw::ctsOutAsserted());
    // The "computer" obeys our CTS.
    gpio_set_direction(static_cast<gpio_num_t>(pins::kCts), GPIO_MODE_INPUT);
    esp_rom_gpio_connect_in_signal(pins::kCts, U2CTS_IN_IDX, false);
    uart_set_hw_flow_ctrl(UART_NUM_2, UART_HW_FLOWCTRL_CTS, 0);

    const size_t total = hw::kHostRxBuffer + 2048;
    uint8_t chunk[64];
    size_t sent = 0;
    bool dropped = false;
    const uint32_t start = millis();
    while (sent < total && millis() - start < 3000) {  // modem reads nothing meanwhile
        if (Serial2.availableForWrite() >= static_cast<int>(sizeof chunk)) {
            for (size_t i = 0; i < sizeof chunk; ++i) {
                chunk[i] = static_cast<uint8_t>(sent + i);
            }
            Serial2.write(chunk, sizeof chunk);
            sent += sizeof chunk;
        }
        dropped = dropped || !hw::ctsOutAsserted();
        delay(1);
    }
    delay(50);
    TEST_ASSERT_TRUE_MESSAGE(dropped || !hw::ctsOutAsserted(), "CTS never dropped");
    TEST_ASSERT_FALSE_MESSAGE(hw::ctsOutAsserted(), "CTS on with a full buffer");

    // Now read everything: CTS comes back and every byte arrives, in order.
    size_t got = 0;
    size_t wrong = 0;
    uint32_t last = millis();
    while (millis() - last < 300) {
        while (Serial1.available() > 0) {
            if (static_cast<uint8_t>(Serial1.read()) != static_cast<uint8_t>(got)) {
                ++wrong;
            }
            ++got;
            last = millis();
        }
        delay(1);
    }
    TEST_ASSERT_TRUE(hw::ctsOutAsserted());
    TEST_ASSERT_EQUAL(sent, got);
    TEST_ASSERT_EQUAL(0, wrong);

    uart_set_hw_flow_ctrl(UART_NUM_2, UART_HW_FLOWCTRL_DISABLE, 0);
    hw::captureBegin();
}

// A long stream in command mode (a file sent before the call is up, or noise) must not
// upset the capture: afterwards "AT" is still understood.
void test_long_stream_in_command_mode_is_survived() {
    rigBaud(115200);
    portBaud(115200);
    flushAll();
    for (int i = 0; i < 100; ++i) {  // 10 KB, about 0.9 s without a pause
        Serial2.print(
            "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ"
            "0123456789abcdefghijklmnopqrstuvwxyz\r\n");
    }
    Serial2.flush();
    delay(100);
    uint8_t out[16];
    autobaud::Burst b = sendAndCapture("AT\r", 115200, out, sizeof out);
    TEST_ASSERT_EQUAL(3, b.len);
    TEST_ASSERT_EQUAL_STRING_LEN("AT\r", reinterpret_cast<char *>(out), 3);
}

void test_cts_held_on_without_hardware_flow_control() {
    hw::hostSetFlow(false, false);  // &H0
    TEST_ASSERT_TRUE(hw::ctsOutAsserted());
}

void test_sending_stops_while_rts_is_off() {
    rigBaud(115200);
    portBaud(115200);
    hw::hostSetFlow(false, true);  // &R2
    flushAll();
    drive(pins::kRts, true);  // HIGH = RTS off at the GPIO
    Serial1.print("held until RTS comes on");
    std::string early = readFor(Serial2, 30);
    TEST_ASSERT_TRUE_MESSAGE(early.size() <= 1, early.c_str());  // at most one in flight
    drive(pins::kRts, false);
    std::string rest = early + readFor(Serial2, 30);
    TEST_ASSERT_EQUAL_STRING("held until RTS comes on", rest.c_str());
    release(pins::kRts);
    hw::hostSetFlow(false, false);
}

void test_dtr_input() {
    drive(pins::kDtr, false);
    TEST_ASSERT_TRUE(hw::dtrAsserted());
    drive(pins::kDtr, true);
    TEST_ASSERT_FALSE(hw::dtrAsserted());
    release(pins::kDtr);
    TEST_ASSERT_FALSE(hw::dtrAsserted());  // pull-up: nothing connected = off
}
void test_idle_shutdown_and_drain_are_bounded_with_rts_off() {
    rigBaud(115200);
    portBaud(115200);
    hw::hostSetFlow(false, true);
    drive(pins::kRts, true);
    Serial1.print("queued while RTS is off");
    delay(10);
    TEST_ASSERT_FALSE(hw::hostDrain(20));
    uint32_t before = millis();
    hw::hostEnd();
    TEST_ASSERT_LESS_THAN_UINT32(500, millis() - before);
    TEST_ASSERT_FALSE(hw::hostActive());
    release(pins::kRts);
}

void setup() {
    delay(2000);
    fitted = driven(pins::kTxd) || driven(pins::kRts) || driven(pins::kDtr);
    if (!fitted) {
        hw::hostBegin(current);
        hw::captureBegin();  // first: setting up the RMT makes TXD-in an input-only pin
        Serial2.begin(current, SERIAL_8N1, pins::kTestUartRx, pins::kTestUartTx);
        uart_internal_loopback(2, pins::kTxd);         // "computer" -> TXD in
        uart_internal_loopback(1, pins::kTestUartRx);  // RXD out -> "computer"
    }
    UNITY_BEGIN();
    RUN_TEST(test_speed_detected_from_at_at_every_rate);
    RUN_TEST(test_long_line_is_captured_whole);
    RUN_TEST(test_data_to_the_computer);
    RUN_TEST(test_cts_drops_when_the_buffer_is_full_and_nothing_is_lost);
    RUN_TEST(test_cts_held_on_without_hardware_flow_control);
    RUN_TEST(test_sending_stops_while_rts_is_off);
    RUN_TEST(test_dtr_input);
    RUN_TEST(test_long_stream_in_command_mode_is_survived);
    RUN_TEST(test_idle_shutdown_and_drain_are_bounded_with_rts_off);
    UNITY_END();
}

void loop() {}
