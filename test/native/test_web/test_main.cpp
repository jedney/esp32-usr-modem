#include <unity.h>
#include <cstring>
#include <string>
#include <vector>
#include "image_check.h"
#include "upload_handshake.h"
#include "web_policy.h"
using namespace webinterface;
void setUp() {}
void tearDown() {}
class FakeCrypto : public Crypto {
public:
    uint8_t sequence = 0;
    bool fail = false;
    std::string last;
    bool random(uint8_t *out, size_t len) override {
        memset(out, ++sequence, len);
        return !fail;
    }
    bool md5(const std::string &text, char out[33]) override {
        last = text;
        strcpy(out, text == "GET:/api/session" ? "f156ea5001b17ee7478625ca4f937164"
                                               : "0123456789abcdef0123456789abcdef");
        return !fail;
    }
};
std::string digest(const std::string &nonce, const char *nc = "00000001") {
    return "Digest username=\"admin\", realm=\"usr-modem\", nonce=\"" + nonce +
           "\", uri=\"/api/session\", response=\"0123456789abcdef0123456789abcdef\", "
           "qop=auth, nc=" +
           nc + ", cnonce=\"browser\"";
}
void test_host_and_origin_do_not_accept_rebinding_authorities() {
    TEST_ASSERT_TRUE(allowedHost("USR-MODEM.local:80", "usr-modem", "10.0.0.221"));
    TEST_ASSERT_TRUE(allowedHost("10.0.0.221", "usr-modem", "10.0.0.221"));
    for (const char *bad :
         {"", "evil.local", "usr-modem.local.evil", "usr-modem.local:81", "usr-modem.local@evil",
          "usr-modem.local,evil", "usr-modem.local/", " usr-modem.local", "10.0.0.221:80:80"}) {
        TEST_ASSERT_FALSE(allowedHost(bad, "usr-modem", "10.0.0.221"));
    }
    TEST_ASSERT_TRUE(allowedOrigin("http://usr-modem.local", "usr-modem", "10.0.0.221"));
    TEST_ASSERT_FALSE(allowedOrigin("null", "usr-modem", "10.0.0.221"));
    TEST_ASSERT_FALSE(allowedOrigin("http://usr-modem.local/evil", "usr-modem", "10.0.0.221"));
}
void test_digest_checks_exact_uri_method_replay_and_expiry() {
    FakeCrypto crypto;
    Auth auth(crypto);
    auto challenge = auth.challenge(0xfffffff0u);
    auto nonce = challenge.substr(challenge.find("nonce=\"") + 7, 32);
    const char *ha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    TEST_ASSERT_TRUE(auth.verify(digest(nonce), "GET", "/api/session", ha1, 5));
    TEST_ASSERT_EQUAL_STRING(
        (std::string(ha1) + ':' + nonce + ":00000001:browser:auth:f156ea5001b17ee7478625ca4f937164")
            .c_str(),
        crypto.last.c_str());
    TEST_ASSERT_FALSE(auth.verify(digest(nonce), "GET", "/api/session", ha1, 6));
    TEST_ASSERT_FALSE(auth.verify(digest(nonce, "00000002"), "GET", "/elsewhere", ha1, 6));
    TEST_ASSERT_TRUE(auth.verify(digest(nonce, "00000002"), "GET", "/api/session", ha1, 7));
    TEST_ASSERT_FALSE(auth.verify(digest(nonce, "00000003"), "GET", "/api/session", ha1, 60000));
    auth.reset();
    TEST_ASSERT_FALSE(auth.verify(digest(nonce, "00000003"), "GET", "/api/session", ha1, 8));
}
void test_digest_reports_stale_nonce_and_wrong_password() {
    FakeCrypto crypto;
    Auth auth(crypto);
    const char *ha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    TEST_ASSERT_TRUE(Check::kMissing == auth.check("", "GET", "/api/session", ha1, 1));
    auto c = auth.challenge(0);
    TEST_ASSERT_EQUAL(std::string::npos, c.find("stale"));
    auto n = c.substr(c.find("nonce=\"") + 7, 32);
    TEST_ASSERT_TRUE(Check::kOk == auth.check(digest(n), "GET", "/api/session", ha1, 1));
    // Replay of the same count, an expired nonce and a nonce we never issued:
    // the password is right, so the browser should retry without asking again.
    TEST_ASSERT_TRUE(Check::kStale == auth.check(digest(n), "GET", "/api/session", ha1, 2));
    TEST_ASSERT_TRUE(Check::kStale ==
                     auth.check(digest(n, "00000002"), "GET", "/api/session", ha1, 60000));
    TEST_ASSERT_TRUE(Check::kStale == auth.check(digest("0123456789abcdef0123456789abcdef"), "GET",
                                                 "/api/session", ha1, 3));
    auto s = auth.challenge(4, true);
    TEST_ASSERT_NOT_EQUAL(std::string::npos, s.find(", stale=true"));
    // A wrong response is never stale, even with a fresh nonce.
    std::string wrong = digest(n, "00000003");
    wrong.replace(wrong.find("0123456789abcdef0123456789abcdef"), 32,
                  "ffffffffffffffffffffffffffffffff");
    TEST_ASSERT_TRUE(Check::kWrong == auth.check(wrong, "GET", "/api/session", ha1, 5));
    TEST_ASSERT_TRUE(Check::kOk ==
                     auth.check(digest(n, "00000003"), "GET", "/api/session", ha1, 1005));
}
// SEC-02: after a wrong response the next second is answered at once, without
// checking the password, instead of the server task sleeping for a second.
void test_wrong_response_locks_out_for_one_second_without_blocking() {
    FakeCrypto crypto;
    Auth auth(crypto);
    const char *ha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    auto c = auth.challenge(0xfffffe00u);
    auto n = c.substr(c.find("nonce=\"") + 7, 32);
    std::string wrong = digest(n);
    wrong.replace(wrong.find("0123456789abcdef0123456789abcdef"), 32,
                  "ffffffffffffffffffffffffffffffff");
    TEST_ASSERT_FALSE(auth.lockedOut(0xfffffe00u));
    TEST_ASSERT_TRUE(Check::kWrong == auth.check(wrong, "GET", "/api/session", ha1, 0xffffff00u));
    TEST_ASSERT_TRUE(auth.lockedOut(0xffffff00u));
    // Inside the second (across the 2^32 ms wrap) even the right password is refused,
    // and the password is not checked at all.
    crypto.last.clear();
    TEST_ASSERT_TRUE(Check::kWrong ==
                     auth.check(digest(n, "00000002"), "GET", "/api/session", ha1, 0x000002e7u));
    TEST_ASSERT_TRUE(crypto.last.empty());
    // A refused attempt does not extend the lock-out, so nobody can hold it for ever.
    TEST_ASSERT_TRUE(auth.lockedOut(0x000002e7u));
    TEST_ASSERT_FALSE(auth.lockedOut(0x000002e8u));
    TEST_ASSERT_TRUE(Check::kOk ==
                     auth.check(digest(n, "00000003"), "GET", "/api/session", ha1, 0x000002e8u));
    // No Authorization header is still just a challenge, locked out or not.
    TEST_ASSERT_TRUE(Check::kWrong == auth.check(wrong, "GET", "/api/session", ha1, 0x1000));
    TEST_ASSERT_TRUE(Check::kMissing == auth.check("", "GET", "/api/session", ha1, 0x1001));
    // A password change (reset) clears the lock-out.
    auth.reset();
    TEST_ASSERT_FALSE(auth.lockedOut(0x1002));
}
void test_digest_rejects_duplicate_fields_and_non_digest_schemes() {
    FakeCrypto crypto;
    Auth auth(crypto);
    auto c = auth.challenge(0);
    auto n = c.substr(c.find("nonce=\"") + 7, 32);
    const char *ha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    TEST_ASSERT_FALSE(
        auth.verify(digest(n) + ", username=\"admin\"", "GET", "/api/session", ha1, 1));
    TEST_ASSERT_FALSE(auth.verify("Basic YWRtaW46cGFzcw==", "GET", "/api/session", ha1, 1));
    TEST_ASSERT_FALSE(auth.verify(digest(n, "00000000"), "GET", "/api/session", ha1, 1));
    TEST_ASSERT_FALSE(auth.verify(digest(n, "garbage!"), "GET", "/api/session", ha1, 1));
    TEST_ASSERT_FALSE(auth.verify(digest(n), "GET", "/api/session", "", 1));
    crypto.fail = true;
    TEST_ASSERT_FALSE(auth.verify(digest(n), "GET", "/api/session", ha1, 1));
}
void test_tickets_are_single_use_bounded_and_expire_across_rollover() {
    FakeCrypto crypto;
    Auth auth(crypto);
    Tickets tickets(auth);
    auto a = tickets.issue(0xfffffff0u);
    TEST_ASSERT_TRUE(tickets.consume(a, 0));
    TEST_ASSERT_FALSE(tickets.consume(a, 1));
    a = tickets.issue(0xfffffff0u);
    TEST_ASSERT_FALSE(tickets.consume(a, 30000));
    a = tickets.issue(0);
    for (unsigned i = 0; i < 4; ++i) {
        tickets.issue(1);
    }
    TEST_ASSERT_FALSE(tickets.consume(a, 2));
    a = tickets.issue(3);
    tickets.reset();
    TEST_ASSERT_FALSE(tickets.consume(a, 4));
    crypto.fail = true;
    TEST_ASSERT_TRUE(tickets.issue(5).empty());
}
class Store : public modemcfg::Store {
public:
    std::vector<uint8_t> data;
    bool fail = false;
    size_t load(uint8_t *out, size_t cap) override {
        size_t n = data.size() < cap ? data.size() : cap;
        memcpy(out, data.data(), n);
        return n;
    }
    bool save(const uint8_t *in, size_t n) override {
        if (fail) {
            return false;
        }
        data.assign(in, in + n);
        return true;
    }
    bool erase() override {
        data.clear();
        return true;
    }
};
void test_settings_default_persist_failure_and_corruption_fail_closed() {
    Store store;
    Settings s(store);
    TEST_ASSERT_TRUE(s.begin());
    TEST_ASSERT_TRUE(s.enabled());
    TEST_ASSERT_TRUE(s.setEnabled(false));
    Settings reboot(store);
    TEST_ASSERT_TRUE(reboot.begin());
    TEST_ASSERT_FALSE(reboot.enabled());
    store.fail = true;
    TEST_ASSERT_FALSE(reboot.setEnabled(true));
    TEST_ASSERT_FALSE(reboot.enabled());
    store.data[6] ^= 1;
    Settings damaged(store);
    TEST_ASSERT_FALSE(damaged.begin());
    TEST_ASSERT_FALSE(damaged.enabled());
    store.erase();
    Settings factory(store);
    TEST_ASSERT_TRUE(factory.begin());
    TEST_ASSERT_TRUE(factory.enabled());
}
void test_queue_wrap_overflow_and_secret_cleanup() {
    Queue<4> queue;
    uint8_t input[] = {1, 2, 3, 4}, output[4] = {};
    TEST_ASSERT_TRUE(queue.push(input, 4));
    TEST_ASSERT_FALSE(queue.push(input, 1));
    TEST_ASSERT_EQUAL(2, queue.pop(output, 2));
    TEST_ASSERT_TRUE(queue.push(input, 2));
    TEST_ASSERT_EQUAL(4, queue.pop(output, 4));
    const uint8_t expected[] = {3, 4, 1, 2};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, 4);
    queue.push(input, 4);
    queue.clear();
    TEST_ASSERT_EQUAL(0, queue.pop(output, 4));
    TEST_ASSERT_EQUAL(4, queue.space());
}
void test_version_order_is_numeric_and_invalid_is_distinct() {
    TEST_ASSERT_EQUAL(1, compareVersions("0.11", "0.9"));
    TEST_ASSERT_EQUAL(-1, compareVersions("0.10", "0.11"));
    TEST_ASSERT_EQUAL(0, compareVersions("0.11", "0.11"));
    for (const char *bad : {"", "0", ".1", "1.", "0.11-dev", "0.1.2", "999999.1"}) {
        TEST_ASSERT_EQUAL(-2, compareVersions(bad, "0.11"));
    }
}
void test_image_identity_header_length_and_chunk_boundaries() {
    std::string image(24, '\0');
    image[0] = char(0xe9);
    image[12] = 9;
    image += "USRMODEM/1/FIRMWARE/0.11\n";
    ImageCheck check(false, "0.11", image.size());
    for (char c : image) {
        TEST_ASSERT_TRUE(check.write(reinterpret_cast<const uint8_t *>(&c), 1));
    }
    TEST_ASSERT_TRUE(check.complete());
    ImageCheck wrong(false, "0.10", image.size());
    wrong.write(reinterpret_cast<const uint8_t *>(image.data()), image.size());
    TEST_ASSERT_FALSE(wrong.complete());
    ImageCheck truncated(false, "0.11", image.size() + 1);
    truncated.write(reinterpret_cast<const uint8_t *>(image.data()), image.size());
    TEST_ASSERT_FALSE(truncated.complete());
    image[12] = 0;
    ImageCheck chip(false, "0.11", image.size());
    chip.write(reinterpret_cast<const uint8_t *>(image.data()), image.size());
    TEST_ASSERT_FALSE(chip.complete());
}
void test_json_escapes_untrusted_strings() {
    TEST_ASSERT_EQUAL_STRING("\"a\\\"\\\\\\u001b\\u000a\"", jsonString("a\"\\\x1b\n").c_str());
}
void test_browser_cp437_is_utf8_and_serial_source_bytes_are_unchanged() {
    const uint8_t bytes[] = {0xda, 0xc4, 0xbf, '\r', '\n'};
    TEST_ASSERT_EQUAL_STRING("\xe2\x94\x8c\xe2\x94\x80\xe2\x94\x90\r\n",
                             terminalUtf8(bytes, sizeof bytes, true).c_str());
    TEST_ASSERT_EQUAL_STRING("???\r\n", terminalUtf8(bytes, sizeof bytes, false).c_str());
    TEST_ASSERT_EQUAL_HEX8(0xda, bytes[0]);
}
void test_raw_headers_reject_duplicates_smuggling_and_pipelining() {
    const std::string good = "GET / HTTP/1.1\r\nHost: usr-modem.local\r\n\r\n";
    HeaderGate gate;
    for (char c : good) {
        TEST_ASSERT_TRUE(gate.accept(reinterpret_cast<const uint8_t *>(&c), 1));
    }
    TEST_ASSERT_FALSE(gate.accept(reinterpret_cast<const uint8_t *>(good.data()), good.size()));
    for (const char *extra :
         {"Host: evil.local\r\n", "hOsT: usr-modem.local\r\n", "Transfer-Encoding: chunked\r\n",
          "Content-Length: 1\r\nContent-Length: 1\r\n", " Content-Length: 1\r\n"}) {
        HeaderGate bad;
        std::string input =
            "POST / HTTP/1.1\r\nHost: usr-modem.local\r\n" + std::string(extra) + "\r\n";
        TEST_ASSERT_FALSE(
            bad.accept(reinterpret_cast<const uint8_t *>(input.data()), input.size()));
    }
    HeaderGate post;
    std::string body = "POST / HTTP/1.1\r\nHost: usr-modem.local\r\nContent-Length: 3\r\n\r\nabc";
    TEST_ASSERT_TRUE(post.accept(reinterpret_cast<const uint8_t *>(body.data()), body.size()));
    const uint8_t extra = 0;
    TEST_ASSERT_FALSE(post.accept(&extra, 1));
    HeaderGate upgrade;
    std::string request =
        "GET /ws?token=abc HTTP/1.1\r\nHost: usr-modem.local\r\n"
        "Origin: http://foreign.invalid\r\nUpgrade: websocket\r\n\r\n";
    TEST_ASSERT_TRUE(
        upgrade.accept(reinterpret_cast<const uint8_t *>(request.data()), request.size()));
    TEST_ASSERT_TRUE(upgrade.complete());
    TEST_ASSERT_TRUE(upgrade.websocket());
    TEST_ASSERT_EQUAL_STRING("http://foreign.invalid", upgrade.origin().c_str());
    TEST_ASSERT_EQUAL_STRING("/ws?token=abc", upgrade.target().c_str());
    HeaderGate browser;
    std::string large = "GET /api/session HTTP/1.1\r\nHost: usr-modem.local\r\nUser-Agent: " +
                        std::string(1500, 'a') + "\r\n\r\n";
    TEST_ASSERT_TRUE(browser.accept(reinterpret_cast<const uint8_t *>(large.data()), large.size()));
    TEST_ASSERT_EQUAL(large.size(), browser.headerLength());
    TEST_ASSERT_EQUAL(std::string::npos, browser.filtered().find("User-Agent"));
    TEST_ASSERT_LESS_THAN(1024, browser.filtered().size());
}
void test_terminal_geometry_is_bounded_and_cannot_be_terminal_input() {
    unsigned columns = 80, rows = 25;
    TEST_ASSERT_TRUE(terminalSize("{\"t\":\"size\",\"cols\":132,\"rows\":50}", columns, rows));
    TEST_ASSERT_EQUAL(132, columns);
    TEST_ASSERT_EQUAL(50, rows);
    for (const char *bad :
         {"{\"t\":\"size\",\"cols\":133,\"rows\":50}", "{\"t\":\"size\",\"cols\":80,\"rows\":0}",
          "{\"t\":\"size\",\"cols\":-1,\"rows\":25}", "{\"t\":\"size\",\"cols\":080,\"rows\":25}",
          "{\"t\":\"size\",\"cols\":80,\"rows\":25}AT*FACTORY",
          "{\"t\":\"size\",\"cols\":80,\"rows\":25,\"cols\":120}",
          "{\"t\":\"size\",\"cols\":999999999999999999999,\"rows\":25}"}) {
        TEST_ASSERT_FALSE(terminalSize(bad, columns, rows));
        TEST_ASSERT_EQUAL(132, columns);
        TEST_ASSERT_EQUAL(50, rows);
    }
}
// STA-07: each side acts only while the other is still waiting.
void test_upload_handshake_grant_and_refuse_on_time() {
    UploadHandshake h;
    uint32_t client = 0;
    TEST_ASSERT_FALSE(h.takeRequest(client));
    TEST_ASSERT_TRUE(h.ask(7));
    TEST_ASSERT_FALSE(h.ask(8));  // one request at a time
    TEST_ASSERT_FALSE(h.answered());
    TEST_ASSERT_FALSE(h.allow(true));  // not taken yet
    TEST_ASSERT_TRUE(h.takeRequest(client));
    TEST_ASSERT_EQUAL_UINT32(7, client);
    TEST_ASSERT_FALSE(h.takeRequest(client));  // taken once
    TEST_ASSERT_FALSE(h.allow(false));
    TEST_ASSERT_TRUE(h.answered());
    TEST_ASSERT_FALSE(h.granted());
    TEST_ASSERT_TRUE(h.idle());
    TEST_ASSERT_FALSE(h.uploading());
    TEST_ASSERT_TRUE(h.ask(7));
    TEST_ASSERT_TRUE(h.takeRequest(client));
    TEST_ASSERT_TRUE(h.allow(true));
    TEST_ASSERT_TRUE(h.answered() && h.granted() && h.uploading());
    TEST_ASSERT_FALSE(h.giveUp());  // answered: the server goes ahead
    TEST_ASSERT_FALSE(h.ask(8));    // busy until the loop collects the result
    bool ok = true, fs = false;
    TEST_ASSERT_FALSE(h.takeDone(ok, fs));
    h.finish(true, false);
    TEST_ASSERT_TRUE(h.uploading());
    TEST_ASSERT_TRUE(h.takeDone(ok, fs));
    TEST_ASSERT_TRUE(ok);
    TEST_ASSERT_FALSE(fs);
    TEST_ASSERT_TRUE(h.idle());
    TEST_ASSERT_FALSE(h.uploading());
}
void test_upload_handshake_late_answer_changes_nothing() {
    UploadHandshake h;
    uint32_t client = 0;
    // The loop takes the request, the server times out, then the loop answers yes.
    TEST_ASSERT_TRUE(h.ask(3));
    TEST_ASSERT_TRUE(h.takeRequest(client));
    TEST_ASSERT_TRUE(h.giveUp());
    TEST_ASSERT_FALSE(h.allow(true));  // the loop must release its claim
    TEST_ASSERT_FALSE(h.uploading());  // so loop() does not return early for ever
    TEST_ASSERT_TRUE(h.idle());
    // The server times out before the loop even sees the request.
    TEST_ASSERT_TRUE(h.ask(4));
    TEST_ASSERT_TRUE(h.giveUp());
    TEST_ASSERT_FALSE(h.takeRequest(client));
    TEST_ASSERT_FALSE(h.allow(true));
    TEST_ASSERT_TRUE(h.idle());
    // A late answer can't grant a newer request that the loop never checked.
    TEST_ASSERT_TRUE(h.ask(5));
    TEST_ASSERT_FALSE(h.allow(true));
    TEST_ASSERT_FALSE(h.granted());
}
void test_upload_handshake_late_unmount_is_ignored() {
    UploadHandshake h;
    uint32_t client = 0;
    bool ok = true, fs = false;
    // On time: asked, taken, acknowledged.
    TEST_ASSERT_FALSE(h.askUnmount());  // only while an upload is active
    TEST_ASSERT_TRUE(h.ask(1) && h.takeRequest(client) && h.allow(true));
    TEST_ASSERT_FALSE(h.takeUnmount());
    TEST_ASSERT_TRUE(h.askUnmount());
    TEST_ASSERT_TRUE(h.takeUnmount());
    TEST_ASSERT_FALSE(h.takeUnmount());
    TEST_ASSERT_FALSE(h.unmountReady());
    TEST_ASSERT_TRUE(h.unmounted());
    TEST_ASSERT_TRUE(h.unmountReady());
    h.finish(true, true);
    TEST_ASSERT_TRUE(h.takeDone(ok, fs));
    // The server gives up before the loop takes the request: the failed upload finishes
    // and the loop remounts; the old request must not unmount LittleFS again.
    TEST_ASSERT_TRUE(h.ask(1) && h.takeRequest(client) && h.allow(true));
    TEST_ASSERT_TRUE(h.askUnmount());
    h.giveUpUnmount();
    h.finish(false, true);
    TEST_ASSERT_FALSE(h.takeUnmount());
    TEST_ASSERT_TRUE(h.takeDone(ok, fs));
    TEST_ASSERT_FALSE(ok);
    TEST_ASSERT_TRUE(fs);  // the loop remounts LittleFS
    TEST_ASSERT_FALSE(h.takeUnmount());
    // Taken, then the server gives up before the acknowledgement: the late ack is refused
    // and the result still says "LittleFS, failed", so the loop remounts after unmounting.
    TEST_ASSERT_TRUE(h.ask(1) && h.takeRequest(client) && h.allow(true));
    TEST_ASSERT_TRUE(h.askUnmount());
    TEST_ASSERT_TRUE(h.takeUnmount());
    h.giveUpUnmount();
    TEST_ASSERT_FALSE(h.unmounted());
    TEST_ASSERT_FALSE(h.unmountReady());
    h.finish(false, true);
    TEST_ASSERT_TRUE(h.takeDone(ok, fs));
    TEST_ASSERT_TRUE(!ok && fs);
    // An acknowledgement that won the race is kept by giveUpUnmount().
    TEST_ASSERT_TRUE(h.ask(1) && h.takeRequest(client) && h.allow(true));
    TEST_ASSERT_TRUE(h.askUnmount() && h.takeUnmount() && h.unmounted());
    h.giveUpUnmount();
    TEST_ASSERT_TRUE(h.unmountReady());
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_terminal_geometry_is_bounded_and_cannot_be_terminal_input);
    RUN_TEST(test_raw_headers_reject_duplicates_smuggling_and_pipelining);
    RUN_TEST(test_browser_cp437_is_utf8_and_serial_source_bytes_are_unchanged);
    RUN_TEST(test_host_and_origin_do_not_accept_rebinding_authorities);
    RUN_TEST(test_digest_checks_exact_uri_method_replay_and_expiry);
    RUN_TEST(test_digest_reports_stale_nonce_and_wrong_password);
    RUN_TEST(test_wrong_response_locks_out_for_one_second_without_blocking);
    RUN_TEST(test_upload_handshake_grant_and_refuse_on_time);
    RUN_TEST(test_upload_handshake_late_answer_changes_nothing);
    RUN_TEST(test_upload_handshake_late_unmount_is_ignored);
    RUN_TEST(test_digest_rejects_duplicate_fields_and_non_digest_schemes);
    RUN_TEST(test_tickets_are_single_use_bounded_and_expire_across_rollover);
    RUN_TEST(test_settings_default_persist_failure_and_corruption_fail_closed);
    RUN_TEST(test_queue_wrap_overflow_and_secret_cleanup);
    RUN_TEST(test_version_order_is_numeric_and_invalid_is_distinct);
    RUN_TEST(test_image_identity_header_length_and_chunk_boundaries);
    RUN_TEST(test_json_escapes_untrusted_strings);
    return UNITY_END();
}
