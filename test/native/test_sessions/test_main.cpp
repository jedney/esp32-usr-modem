// Browser login: form, cookie, password check, throttle, session table and login events.
#include <unity.h>
#include <cstring>
#include <string>
#include "session_policy.h"
using namespace webinterface;
void setUp() {}
void tearDown() {}

// SHA-256 from FIPS 180-4, written for this test (the firmware uses PSA).
std::string sha256hex(const std::string &text) {
    static const uint32_t k[64] = {
        0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4,
        0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe,
        0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f,
        0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
        0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc,
        0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
        0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116,
        0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
        0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7,
        0xc67178f2};
    uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                     0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
    std::string data = text;
    data += char(0x80);
    while (data.size() % 64 != 56) {
        data += char(0);
    }
    const uint64_t bits = uint64_t(text.size()) * 8;
    for (int i = 7; i >= 0; --i) {
        data += char((bits >> (8 * i)) & 0xff);
    }
    auto rotr = [](uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); };
    for (size_t block = 0; block < data.size(); block += 64) {
        uint32_t w[64];
        for (int i = 0; i < 16; ++i) {
            w[i] = 0;
            for (int j = 0; j < 4; ++j) {
                w[i] = (w[i] << 8) | uint8_t(data[block + i * 4 + j]);
            }
        }
        for (int i = 16; i < 64; ++i) {
            uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
            uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
        for (int i = 0; i < 64; ++i) {
            uint32_t t1 =
                hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
            uint32_t t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
            hh = g;
            g = f;
            f = e;
            e = d + t1;
            d = c;
            c = b;
            b = a;
            a = t1 + t2;
        }
        h[0] += a;
        h[1] += b;
        h[2] += c;
        h[3] += d;
        h[4] += e;
        h[5] += f;
        h[6] += g;
        h[7] += hh;
    }
    char out[65];
    for (int i = 0; i < 8; ++i) {
        snprintf(out + i * 8, 9, "%08x", unsigned(h[i]));
    }
    return out;
}
class TestCrypto : public Crypto {
public:
    uint32_t calls = 0;
    bool fail = false;
    // Every call gives different bytes: its number leads, so tokens never repeat.
    bool random(uint8_t *out, size_t len) override {
        ++calls;
        for (size_t i = 0; i < len; ++i) {
            out[i] = i < 4 ? uint8_t(calls >> (8 * i)) : uint8_t(i);
        }
        return !fail;
    }
    bool sha256(const uint8_t *data, size_t len, uint8_t out[32]) override {
        const std::string hex = sha256hex(std::string(reinterpret_cast<const char *>(data), len));
        for (int i = 0; i < 32; ++i) {
            out[i] = uint8_t(std::stoul(hex.substr(i * 2, 2), nullptr, 16));
        }
        return !fail;
    }
};

void test_sha256_reference_vectors() {
    TEST_ASSERT_EQUAL_STRING("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
                             sha256hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
                             sha256hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1",
        sha256hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq").c_str());
}

// --- Login form --------------------------------------------------------------------------
void test_login_form_decodes_both_fields() {
    char user[33], password[33];
    TEST_ASSERT_TRUE(
        parseLoginForm("username=admin&password=Correct%2BHorse+9%21", user, password));
    TEST_ASSERT_EQUAL_STRING("admin", user);
    TEST_ASSERT_EQUAL_STRING("Correct+Horse 9!", password);
    TEST_ASSERT_TRUE(parseLoginForm("password=x&username=", user, password));
    TEST_ASSERT_EQUAL_STRING("", user);
    TEST_ASSERT_EQUAL_STRING("x", password);
}
void test_login_form_refuses_duplicates_extras_controls_and_oversize() {
    char user[33], password[33];
    for (const char *bad :
         {"", "username=admin", "password=x", "username=a&password=b&password=c",
          "username=a&username=b&password=c", "username=a&password=b&remember=1",
          "username=a&password=b&", "username&password=b", "username=a%0d&password=b",
          "username=a&password=%00", "username=a&password=%7f", "username=a&password=%4",
          "username=a&password=%zz", "username=a&password=b\x01"}) {
        TEST_ASSERT_FALSE_MESSAGE(parseLoginForm(bad, user, password), bad);
        TEST_ASSERT_EQUAL_STRING("", user);
        TEST_ASSERT_EQUAL_STRING("", password);
    }
    std::string longest = "username=admin&password=" + std::string(32, 'p');
    TEST_ASSERT_TRUE(parseLoginForm(longest, user, password));
    TEST_ASSERT_FALSE(parseLoginForm(longest + "p", user, password));
    std::string body = "username=admin&password=" + std::string(kLoginFormBytes, '+');
    TEST_ASSERT_FALSE(parseLoginForm(body, user, password));
}

// --- Session cookie ----------------------------------------------------------------------
void test_session_cookie_is_found_among_others_and_must_be_unambiguous() {
    const std::string id = "0123456789abcdef0123456789abcdef";
    std::string token;
    TEST_ASSERT_TRUE(CookieResult::kNone == parseSessionCookie("", token));
    TEST_ASSERT_TRUE(CookieResult::kNone ==
                     parseSessionCookie("theme=dark; x_usr_session=1", token));
    TEST_ASSERT_TRUE(CookieResult::kOk == parseSessionCookie("usr_session=" + id, token));
    TEST_ASSERT_EQUAL_STRING(id.c_str(), token.c_str());
    TEST_ASSERT_TRUE(CookieResult::kOk ==
                     parseSessionCookie("theme=dark; usr_session=" + id + "; other=1", token));
    TEST_ASSERT_EQUAL_STRING(id.c_str(), token.c_str());
    for (const std::string &bad :
         {"usr_session=" + id + "; usr_session=" + id, "usr_session=" + id.substr(1),
          "usr_session=" + id + "0", std::string("usr_session=0123456789ABCDEF0123456789ABCDEF"),
          std::string("usr_session=")}) {
        TEST_ASSERT_TRUE_MESSAGE(CookieResult::kAmbiguous == parseSessionCookie(bad, token),
                                 bad.c_str());
        TEST_ASSERT_TRUE(token.empty());
    }
}

// --- Password check ----------------------------------------------------------------------
void test_password_is_checked_against_the_stored_sha256_hash() {
    TestCrypto crypto;
    const std::string hash = sha256hex("CorrectHorse9");
    TEST_ASSERT_TRUE(verifyPassword(crypto, "admin", "CorrectHorse9", hash.c_str()));
    TEST_ASSERT_FALSE(verifyPassword(crypto, "admin", "CorrectHorse8", hash.c_str()));
    TEST_ASSERT_FALSE(verifyPassword(crypto, "Admin", "CorrectHorse9", hash.c_str()));
    TEST_ASSERT_FALSE(verifyPassword(crypto, "", "CorrectHorse9", hash.c_str()));
    TEST_ASSERT_FALSE(verifyPassword(crypto, "admin", "", sha256hex("").c_str()));
    TEST_ASSERT_FALSE(verifyPassword(crypto, "admin", "CorrectHorse9", ""));
    TEST_ASSERT_FALSE(verifyPassword(crypto, "admin", "CorrectHorse9", hash.substr(1).c_str()));
    crypto.fail = true;
    TEST_ASSERT_FALSE(verifyPassword(crypto, "admin", "CorrectHorse9", hash.c_str()));
}

// --- Throttle ----------------------------------------------------------------------------
void test_throttle_allows_one_check_at_a_time_and_one_try_a_second_after_a_wrong_one() {
    LoginThrottle t;
    TEST_ASSERT_TRUE(t.begin(0));
    TEST_ASSERT_FALSE(t.begin(0));  // a second check while one is running
    t.finish(true, 10);
    TEST_ASSERT_TRUE(t.begin(11));  // a right password leaves no wait
    t.finish(false, 100);
    TEST_ASSERT_FALSE(t.begin(101));
    TEST_ASSERT_FALSE(t.begin(1099));  // refused tries do not extend the wait
    TEST_ASSERT_TRUE(t.begin(1100));
    t.finish(false, 0xfffffff0u);
    TEST_ASSERT_FALSE(t.begin(0x100));  // across millis() rollover
    TEST_ASSERT_TRUE(t.begin(0x400));
    t.finish(true, 0x400);
    t.finish(false, 0x500);
    t.reset();
    TEST_ASSERT_TRUE(t.begin(0x501));
}

// --- Sessions ----------------------------------------------------------------------------
void test_session_is_created_found_and_carries_a_separate_csrf_token() {
    TestCrypto crypto;
    Sessions sessions(crypto);
    std::string token;
    uint32_t serial, replaced;
    TEST_ASSERT_TRUE(Sessions::Result::kOk == sessions.create("", 5, token, serial, replaced));
    TEST_ASSERT_EQUAL(32, token.size());
    TEST_ASSERT_NOT_EQUAL(0, serial);
    TEST_ASSERT_EQUAL_UINT32(0, replaced);
    TEST_ASSERT_EQUAL_UINT32(serial, sessions.find(token, 6));
    const std::string csrf = sessions.csrf(serial);
    TEST_ASSERT_EQUAL(32, csrf.size());
    TEST_ASSERT_TRUE(csrf != token);
    TEST_ASSERT_TRUE(sessions.csrfMatches(serial, csrf, 6));
    TEST_ASSERT_FALSE(sessions.csrfMatches(serial, token, 6));
    TEST_ASSERT_FALSE(sessions.csrfMatches(serial, "", 6));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find("0123456789abcdef0123456789abcdef", 6));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find("", 6));
    crypto.fail = true;
    TEST_ASSERT_TRUE(Sessions::Result::kFailed == sessions.create("", 7, token, serial, replaced));
    TEST_ASSERT_TRUE(token.empty());
    TEST_ASSERT_EQUAL(1, sessions.count());
}
void test_session_ends_exactly_twelve_hours_after_login_whatever_happens() {
    TestCrypto crypto;
    Sessions sessions(crypto);
    std::string token;
    uint32_t serial, replaced;
    const uint64_t start = 0x123456789ull;  // beyond 32-bit milliseconds
    sessions.create("", start, token, serial, replaced);
    TEST_ASSERT_EQUAL_UINT32(43200, sessions.remainingSeconds(serial, start));
    TEST_ASSERT_EQUAL_UINT32(43200, sessions.remainingSeconds(serial, start + 1));
    TEST_ASSERT_EQUAL_UINT32(43199, sessions.remainingSeconds(serial, start + 1000));
    // Use never renews it.
    for (uint64_t t = start; t < start + kSessionLifetimeMs; t += 3600000) {
        TEST_ASSERT_EQUAL_UINT32(serial, sessions.find(token, t));
    }
    const uint64_t last = start + kSessionLifetimeMs - 1;
    TEST_ASSERT_TRUE(sessions.valid(serial, last));
    TEST_ASSERT_EQUAL_UINT32(1, sessions.remainingSeconds(serial, last));
    TEST_ASSERT_FALSE(sessions.valid(serial, last + 1));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find(token, last + 1));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.remainingSeconds(serial, last + 1));
    TEST_ASSERT_FALSE(sessions.csrfMatches(serial, sessions.csrf(serial), last + 1));
    // A clock that goes backwards never makes a session valid.
    TEST_ASSERT_FALSE(sessions.valid(serial, start - 1));
    uint32_t ended[kSessionSlots];
    TEST_ASSERT_EQUAL(0, sessions.expire(last, ended));
    TEST_ASSERT_EQUAL(1, sessions.expire(last + 1, ended));
    TEST_ASSERT_EQUAL_UINT32(serial, ended[0]);
    TEST_ASSERT_EQUAL(0, sessions.count());
    TEST_ASSERT_EQUAL(0, sessions.expire(last + 2, ended));
}
void test_login_replaces_the_browsers_own_session_only() {
    TestCrypto crypto;
    Sessions sessions(crypto);
    std::string first, other, second;
    uint32_t a, b, c, replaced;
    sessions.create("", 0, first, a, replaced);
    sessions.create("", 0, other, b, replaced);
    TEST_ASSERT_TRUE(Sessions::Result::kOk == sessions.create(first, 10, second, c, replaced));
    TEST_ASSERT_EQUAL_UINT32(a, replaced);
    TEST_ASSERT_NOT_EQUAL(a, c);
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find(first, 11));
    TEST_ASSERT_EQUAL_UINT32(b, sessions.find(other, 11));
    TEST_ASSERT_EQUAL_UINT32(c, sessions.find(second, 11));
    TEST_ASSERT_EQUAL(2, sessions.count());
    // The new session runs from its own login.
    TEST_ASSERT_TRUE(sessions.valid(c, kSessionLifetimeMs + 9));
    TEST_ASSERT_FALSE(sessions.valid(b, kSessionLifetimeMs));
}
void test_ninth_browser_is_refused_without_ending_anyone_until_a_slot_frees() {
    TestCrypto crypto;
    Sessions sessions(crypto);
    std::string tokens[kSessionSlots], extra;
    uint32_t serials[kSessionSlots], serial, replaced;
    for (unsigned i = 0; i < kSessionSlots; ++i) {
        TEST_ASSERT_TRUE(Sessions::Result::kOk ==
                         sessions.create("", i * 1000, tokens[i], serials[i], replaced));
    }
    TEST_ASSERT_TRUE(Sessions::Result::kFull == sessions.create("", 9000, extra, serial, replaced));
    TEST_ASSERT_TRUE(Sessions::Result::kFull == sessions.create("0123456789abcdef0123456789abcdef",
                                                                9000, extra, serial, replaced));
    for (unsigned i = 0; i < kSessionSlots; ++i) {
        TEST_ASSERT_EQUAL_UINT32(serials[i], sessions.find(tokens[i], 9000));
    }
    // A logged-in browser can still log in again in its own slot.
    TEST_ASSERT_TRUE(Sessions::Result::kOk ==
                     sessions.create(tokens[3], 9000, tokens[3], serials[3], replaced));
    // Expired sessions are pruned before a slot is chosen.
    TEST_ASSERT_TRUE(Sessions::Result::kOk ==
                     sessions.create("", kSessionLifetimeMs, extra, serial, replaced));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find(tokens[0], kSessionLifetimeMs));
    TEST_ASSERT_EQUAL_UINT32(serials[1], sessions.find(tokens[1], kSessionLifetimeMs));
    // Logging out frees a slot.
    TEST_ASSERT_TRUE(sessions.revoke(serials[5]));
    TEST_ASSERT_TRUE(Sessions::Result::kOk ==
                     sessions.create("", kSessionLifetimeMs, extra, serial, replaced));
}
void test_logout_and_password_change_end_sessions_and_serials_are_never_reused() {
    TestCrypto crypto;
    Sessions sessions(crypto);
    std::string a, b, c;
    uint32_t sa, sb, sc, replaced;
    sessions.create("", 0, a, sa, replaced);
    sessions.create("", 0, b, sb, replaced);
    TEST_ASSERT_TRUE(sessions.revoke(sa));
    TEST_ASSERT_FALSE(sessions.revoke(sa));  // logging out twice is harmless
    TEST_ASSERT_FALSE(sessions.revoke(0));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find(a, 1));
    TEST_ASSERT_TRUE(sessions.csrf(sa).empty());
    TEST_ASSERT_EQUAL_UINT32(sb, sessions.find(b, 1));
    sessions.create("", 2, c, sc, replaced);  // reuses the freed slot
    TEST_ASSERT_TRUE(sc != sa && sc != sb);
    TEST_ASSERT_FALSE(sessions.valid(sa, 3));
    sessions.revokeAll();
    TEST_ASSERT_EQUAL(0, sessions.count());
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find(b, 3));
    TEST_ASSERT_EQUAL_UINT32(0, sessions.find(c, 3));
}

// --- Login events ------------------------------------------------------------------------
void test_login_events_name_the_interface_and_peer_but_nothing_secret() {
    char line[80];
    formatLogin(LoginKind::kWeb, "10.0.0.5", line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("Login: web admin from 10.0.0.5", line);
    formatLogin(LoginKind::kNetConsole, "10.0.0.7", line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("Login: network console from 10.0.0.7", line);
    formatLogin(LoginKind::kOta, nullptr, line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("Login: OTA upload", line);
    formatLogin(LoginKind::kWeb, "", line, sizeof line);
    TEST_ASSERT_EQUAL_STRING("Login: web admin from unknown", line);
}
void test_peer_address_unwraps_ipv4_mapped_ipv6() {
    char out[16] = "x";
    const uint8_t v4[4] = {10, 0, 0, 208};
    formatPeer(v4, sizeof v4, out);
    TEST_ASSERT_EQUAL_STRING("10.0.0.208", out);
    // The web server's socket is IPv6 when lwIP has IPv6: IPv4 clients are ::ffff:a.b.c.d.
    const uint8_t mapped[16] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 192, 168, 255, 254};
    formatPeer(mapped, sizeof mapped, out);
    TEST_ASSERT_EQUAL_STRING("192.168.255.254", out);
    const uint8_t native6[16] = {0xfe, 0x80, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff, 10, 0, 0, 1};
    formatPeer(native6, sizeof native6, out);
    TEST_ASSERT_EQUAL_STRING("", out);
    formatPeer(v4, 3, out);
    TEST_ASSERT_EQUAL_STRING("", out);
    formatPeer(nullptr, 4, out);
    TEST_ASSERT_EQUAL_STRING("", out);
}
void test_login_events_queue_in_order_and_report_overflow() {
    LoginEvents events;
    char line[80];
    TEST_ASSERT_TRUE(events.empty());
    TEST_ASSERT_FALSE(events.take(line, sizeof line));
    events.push(LoginKind::kWeb, "10.0.0.1");
    events.push(LoginKind::kOta, "");
    TEST_ASSERT_TRUE(events.take(line, sizeof line));
    TEST_ASSERT_EQUAL_STRING("Login: web admin from 10.0.0.1", line);
    TEST_ASSERT_TRUE(events.take(line, sizeof line));
    TEST_ASSERT_EQUAL_STRING("Login: OTA upload", line);
    TEST_ASSERT_TRUE(events.empty());
    for (unsigned i = 0; i < LoginEvents::kCapacity + 2; ++i) {
        char peer[16];
        snprintf(peer, sizeof peer, "10.0.0.%u", i);
        events.push(LoginKind::kNetConsole, peer);
    }
    TEST_ASSERT_TRUE(events.take(line, sizeof line));
    TEST_ASSERT_EQUAL_STRING("Login: 2 earlier login events not shown", line);
    TEST_ASSERT_TRUE(events.take(line, sizeof line));
    TEST_ASSERT_EQUAL_STRING("Login: network console from 10.0.0.2", line);
    unsigned left = 0;
    while (events.take(line, sizeof line)) {
        ++left;
    }
    TEST_ASSERT_EQUAL(LoginEvents::kCapacity - 1, left);
    TEST_ASSERT_TRUE(events.empty());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sha256_reference_vectors);
    RUN_TEST(test_login_form_decodes_both_fields);
    RUN_TEST(test_login_form_refuses_duplicates_extras_controls_and_oversize);
    RUN_TEST(test_session_cookie_is_found_among_others_and_must_be_unambiguous);
    RUN_TEST(test_password_is_checked_against_the_stored_sha256_hash);
    RUN_TEST(test_throttle_allows_one_check_at_a_time_and_one_try_a_second_after_a_wrong_one);
    RUN_TEST(test_session_is_created_found_and_carries_a_separate_csrf_token);
    RUN_TEST(test_session_ends_exactly_twelve_hours_after_login_whatever_happens);
    RUN_TEST(test_login_replaces_the_browsers_own_session_only);
    RUN_TEST(test_ninth_browser_is_refused_without_ending_anyone_until_a_slot_frees);
    RUN_TEST(test_logout_and_password_change_end_sessions_and_serials_are_never_reused);
    RUN_TEST(test_login_events_name_the_interface_and_peer_but_nothing_secret);
    RUN_TEST(test_peer_address_unwraps_ipv4_mapped_ipv6);
    RUN_TEST(test_login_events_queue_in_order_and_report_overflow);
    return UNITY_END();
}
