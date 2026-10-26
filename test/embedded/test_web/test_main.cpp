#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>
#include "web_policy.h"
#include "web_service.h"
void setUp() {}
void tearDown() {}
void test_real_digest_md5_adapter_and_random_tickets() {
    hw::WebCrypto crypto;
    char hash[33];
    TEST_ASSERT_TRUE(crypto.md5("abc", hash));
    TEST_ASSERT_EQUAL_STRING("900150983cd24fb0d6963f7d28e17f72", hash);
    webinterface::Auth auth(crypto);
    webinterface::Tickets tickets(auth);
    std::string first = tickets.issue(millis()), second = tickets.issue(millis());
    TEST_ASSERT_EQUAL(32, first.size());
    TEST_ASSERT_NOT_EQUAL(0, first.compare(second));
    TEST_ASSERT_TRUE(tickets.consume(first, millis()));
    TEST_ASSERT_FALSE(tickets.consume(first, millis()));
}
void test_browser_utf8_box_drawing_on_board() {
    const uint8_t header[] = {0xda, 0xc4, 0xbf};
    auto text = webinterface::terminalUtf8(header, sizeof header, true);
    TEST_ASSERT_EQUAL(9, text.size());
    TEST_ASSERT_EQUAL_HEX8(0xe2, uint8_t(text[0]));
}
void setup() {
    delay(2000);
    WiFi.mode(WIFI_STA);
    UNITY_BEGIN();
    RUN_TEST(test_real_digest_md5_adapter_and_random_tickets);
    RUN_TEST(test_browser_utf8_box_drawing_on_board);
    UNITY_END();
}
void loop() {}
