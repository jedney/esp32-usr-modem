#include <Arduino.h>
#include <WiFi.h>
#include <unity.h>
#include "session_policy.h"
#include "web_policy.h"
#include "web_service.h"
void setUp() {}
void tearDown() {}
void test_real_sha256_adapter_checks_the_stored_password_hash() {
    hw::WebCrypto crypto;
    uint8_t digest[32];
    TEST_ASSERT_TRUE(crypto.sha256(reinterpret_cast<const uint8_t *>("abc"), 3, digest));
    TEST_ASSERT_EQUAL_HEX8(0xba, digest[0]);
    TEST_ASSERT_EQUAL_HEX8(0xad, digest[31]);
    // SHA-256("password"), as the device record stores it.
    const char *hash = "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8";
    TEST_ASSERT_TRUE(webinterface::verifyPassword(crypto, "admin", "password", hash));
    TEST_ASSERT_FALSE(webinterface::verifyPassword(crypto, "admin", "Password", hash));
}
void test_random_tickets_and_sessions_with_an_injected_clock() {
    hw::WebCrypto crypto;
    webinterface::Tickets tickets(crypto);
    std::string first = tickets.issue(1, millis()), second = tickets.issue(1, millis());
    TEST_ASSERT_EQUAL(32, first.size());
    TEST_ASSERT_NOT_EQUAL(0, first.compare(second));
    TEST_ASSERT_EQUAL_UINT32(1, tickets.consume(first, millis()));
    TEST_ASSERT_EQUAL_UINT32(0, tickets.consume(first, millis()));
    webinterface::Sessions sessions(crypto);
    std::string a, b;
    uint32_t sa, sb, replaced;
    const uint64_t start = 5000;
    TEST_ASSERT_TRUE(webinterface::Sessions::Result::kOk ==
                     sessions.create("", start, a, sa, replaced));
    TEST_ASSERT_TRUE(webinterface::Sessions::Result::kOk ==
                     sessions.create("", start, b, sb, replaced));
    TEST_ASSERT_NOT_EQUAL(0, a.compare(b));
    TEST_ASSERT_NOT_EQUAL(0, sessions.csrf(sa).compare(a));
    const uint64_t end = start + webinterface::kSessionLifetimeMs;
    TEST_ASSERT_EQUAL_UINT32(sa, sessions.find(a, end - 1));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find(a, end));
    uint32_t ended[webinterface::kSessionSlots];
    TEST_ASSERT_EQUAL(2, sessions.expire(end, ended));
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
    RUN_TEST(test_real_sha256_adapter_checks_the_stored_password_hash);
    RUN_TEST(test_random_tickets_and_sessions_with_an_injected_clock);
    RUN_TEST(test_browser_utf8_box_drawing_on_board);
    UNITY_END();
}
void loop() {}
