// Unit tests for the dial-string rules (project brief 6.3). Run with: pio test -e native
#include <unity.h>

#include "dialstring.h"

using dialstring::classify;
using dialstring::Kind;
using dialstring::Result;
using dialstring::Special;

void setUp() {}
void tearDown() {}

static void assertIp(const Result &r, int a, int b, int c, int d, int port) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Kind::IpAddress), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_UINT8(a, r.ip[0]);
    TEST_ASSERT_EQUAL_UINT8(b, r.ip[1]);
    TEST_ASSERT_EQUAL_UINT8(c, r.ip[2]);
    TEST_ASSERT_EQUAL_UINT8(d, r.ip[3]);
    TEST_ASSERT_EQUAL_UINT16(port, r.port);
}

static void assertHost(const Result &r, const char *host, int port) {
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Kind::Host), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_STRING(host, r.host);
    TEST_ASSERT_EQUAL_UINT16(port, r.port);
}

static void assertInvalid(const char *input) {
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(Kind::Invalid),
                                  static_cast<int>(classify(input).kind), input);
}

// --- Numeric IP + port -----------------------------------------------------------------

void test_brief_example_ip_with_port() {
    assertIp(classify("001002003004789"), 1, 2, 3, 4, 789);
}

void test_twelve_digits_defaults_to_port_23() {
    Result r = classify("170064142064");
    assertIp(r, 170, 64, 142, 64, 23);
    TEST_ASSERT_EQUAL_STRING("170.64.142.64", r.host);
}

void test_seventeen_digits_max_port() {
    assertIp(classify("17006414206465535"), 170, 64, 142, 64, 65535);
}

void test_formatting_and_modifiers_are_stripped() {
    assertIp(classify("170-064-142-064 23"), 170, 64, 142, 64, 23);
    assertIp(classify("W,170 064 142 064,2323"), 170, 64, 142, 64, 2323);
    assertIp(classify("(170)064-142-064;"), 170, 64, 142, 64, 23);
}

void test_octet_over_255_is_invalid() {
    assertInvalid("256001001001");
}

void test_port_over_65535_is_invalid() {
    assertInvalid("00100200300465536");
}

void test_port_zero_is_invalid() {
    assertInvalid("0010020030040");
}

void test_more_than_17_digits_is_invalid() {
    assertInvalid("001002003004123456");
}

// --- Hostnames ---------------------------------------------------------------------------

void test_quoted_host_with_port() {
    assertHost(classify("\"bbs.example.com:2323\""), "bbs.example.com", 2323);
}

void test_quoted_host_without_closing_quote() {
    assertHost(classify("\"bbs.example.com"), "bbs.example.com", 23);
}

void test_bare_host_keeps_w_characters() {
    // 'W' is a dial modifier, but hostnames are never stripped.
    assertHost(classify("www.amigaretro.com"), "www.amigaretro.com", 23);
}

void test_bare_host_with_port() {
    assertHost(classify("www.amigaretro.com:23"), "www.amigaretro.com", 23);
}

void test_dotted_ip_is_treated_as_host() {
    assertHost(classify("170.64.142.64:23"), "170.64.142.64", 23);
}

void test_quoted_single_label_host() {
    assertHost(classify("\"localhost:6400\""), "localhost", 6400);
}

void test_host_with_bad_port_is_invalid() {
    assertInvalid("bbs.example.com:");
    assertInvalid("bbs.example.com:0");
    assertInvalid("bbs.example.com:70000");
    assertInvalid("bbs.example.com:23a");
}

void test_host_with_bad_characters_is_invalid() {
    assertInvalid("bbs example.com");
    assertInvalid("\"\"");
    assertInvalid(".example.com");
}

void test_host_too_long_is_invalid() {
    assertInvalid("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.com");
}

// --- Phonebook and reserved numbers --------------------------------------------------------

void test_short_number_is_phonebook() {
    Result r = classify("1");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Kind::Phonebook), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_STRING("1", r.number);
}

void test_eleven_digits_is_phonebook() {
    Result r = classify("555-123-4567 8");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Kind::Phonebook), static_cast<int>(r.kind));
    TEST_ASSERT_EQUAL_STRING("55512345678", r.number);
}

static void assertSpecial(const char *input, Special expected) {
    Result r = classify(input);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(Kind::Special), static_cast<int>(r.kind), input);
    TEST_ASSERT_EQUAL_INT_MESSAGE(static_cast<int>(expected), static_cast<int>(r.special), input);
}

void test_reserved_numbers() {
    assertSpecial("123", Special::ConfigMenu);
    assertSpecial("666", Special::Ppp);
    assertSpecial("667", Special::Slip);
    assertSpecial("5517546", Special::Ppp);
    assertSpecial("5517545", Special::Slip);
    assertSpecial("6-6-6", Special::Ppp);
}

// --- Junk ------------------------------------------------------------------------------------

void test_empty_and_null_are_invalid() {
    assertInvalid("");
    assertInvalid("   ");
    assertInvalid(",W;");
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Kind::Invalid),
                          static_cast<int>(classify(nullptr).kind));
}

void test_letters_without_dot_are_invalid() {
    assertInvalid("bbs");
}

void test_kind_names() {
    TEST_ASSERT_EQUAL_STRING("ip", dialstring::kindName(Kind::IpAddress));
    TEST_ASSERT_EQUAL_STRING("slip", dialstring::specialName(Special::Slip));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_brief_example_ip_with_port);
    RUN_TEST(test_twelve_digits_defaults_to_port_23);
    RUN_TEST(test_seventeen_digits_max_port);
    RUN_TEST(test_formatting_and_modifiers_are_stripped);
    RUN_TEST(test_octet_over_255_is_invalid);
    RUN_TEST(test_port_over_65535_is_invalid);
    RUN_TEST(test_port_zero_is_invalid);
    RUN_TEST(test_more_than_17_digits_is_invalid);
    RUN_TEST(test_quoted_host_with_port);
    RUN_TEST(test_quoted_host_without_closing_quote);
    RUN_TEST(test_bare_host_keeps_w_characters);
    RUN_TEST(test_bare_host_with_port);
    RUN_TEST(test_dotted_ip_is_treated_as_host);
    RUN_TEST(test_quoted_single_label_host);
    RUN_TEST(test_host_with_bad_port_is_invalid);
    RUN_TEST(test_host_with_bad_characters_is_invalid);
    RUN_TEST(test_host_too_long_is_invalid);
    RUN_TEST(test_short_number_is_phonebook);
    RUN_TEST(test_eleven_digits_is_phonebook);
    RUN_TEST(test_reserved_numbers);
    RUN_TEST(test_empty_and_null_are_invalid);
    RUN_TEST(test_letters_without_dot_are_invalid);
    RUN_TEST(test_kind_names);
    return UNITY_END();
}
