#include <unity.h>
#include <algorithm>
#include <cmath>
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
// After a wrong response the next second is answered at once, without
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
// A network named "Caf\u00e9" reaches the page as the letters, not as mojibake. Bytes
// that are not valid UTF-8 are still escaped one by one so they cannot break the JSON.
void test_json_passes_valid_utf8_and_escapes_invalid_bytes() {
    TEST_ASSERT_EQUAL_STRING("\"caf\xc3\xa9\"", jsonString("caf\xc3\xa9").c_str());    // 2 bytes
    TEST_ASSERT_EQUAL_STRING("\"\xe2\x9c\x93\"", jsonString("\xe2\x9c\x93").c_str());  // 3 bytes
    TEST_ASSERT_EQUAL_STRING("\"\xf0\x9f\x93\xb6\"",
                             jsonString("\xf0\x9f\x93\xb6").c_str());  // 4 bytes
    // A lone lead byte, a lone continuation byte, a truncated sequence at the end.
    TEST_ASSERT_EQUAL_STRING("\"a\\u00c3b\"", jsonString("a\xc3"
                                                         "b")
                                                  .c_str());
    TEST_ASSERT_EQUAL_STRING("\"a\\u00a9b\"", jsonString("a\xa9"
                                                         "b")
                                                  .c_str());
    TEST_ASSERT_EQUAL_STRING("\"a\\u00e2\\u009c\"", jsonString("a\xe2\x9c").c_str());
    // Overlong forms, surrogates, and code points above U+10FFFF are not valid.
    TEST_ASSERT_EQUAL_STRING("\"\\u00c0\\u0080\"", jsonString("\xc0\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\"\\u00e0\\u0080\\u0080\"", jsonString("\xe0\x80\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\"\\u00ed\\u00a0\\u0080\"", jsonString("\xed\xa0\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\"\\u00f4\\u0090\\u0080\\u0080\"",
                             jsonString("\xf4\x90\x80\x80").c_str());
    TEST_ASSERT_EQUAL_STRING("\"\\u00ff\"", jsonString("\xff").c_str());
    // Control bytes and DEL are still escaped, quotes and backslashes still escaped.
    TEST_ASSERT_EQUAL_STRING("\"\\u007f\\u0001\\\"\"", jsonString("\x7f\x01\"").c_str());
    // C1 control characters (U+0080-U+009F) are valid UTF-8 but are controls: escaped.
    TEST_ASSERT_EQUAL_STRING("\"\\u0085\"", jsonString("\xc2\x85").c_str());
    TEST_ASSERT_EQUAL_STRING("\"\xc2\xa0\"", jsonString("\xc2\xa0").c_str());  // U+00A0 passes
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
    // After a WebSocket upgrade header, the frames pass without limit.
    HeaderGate upgrade;
    std::string request =
        "GET /ws?token=abc HTTP/1.1\r\nHost: usr-modem.local\r\n"
        "Origin: http://foreign.invalid\r\nUpgrade: websocket\r\n\r\n";
    TEST_ASSERT_TRUE(
        upgrade.accept(reinterpret_cast<const uint8_t *>(request.data()), request.size()));
    TEST_ASSERT_TRUE(upgrade.complete());
    TEST_ASSERT_TRUE(upgrade.websocket());
    const std::string frames(5000, '\x82');
    TEST_ASSERT_TRUE(
        upgrade.accept(reinterpret_cast<const uint8_t *>(frames.data()), frames.size()));
    // Browser-sized headers pass whole (the HTTP server's own limit is kMaxHeader too).
    HeaderGate browser;
    std::string large = "GET /api/session HTTP/1.1\r\nHost: usr-modem.local\r\nUser-Agent: " +
                        std::string(1500, 'a') + "\r\n\r\n";
    TEST_ASSERT_TRUE(browser.accept(reinterpret_cast<const uint8_t *>(large.data()), large.size()));
    TEST_ASSERT_TRUE(browser.complete());
    TEST_ASSERT_FALSE(browser.websocket());
    // A header longer than kMaxHeader is refused before it ends.
    HeaderGate huge;
    std::string tooLong =
        "GET / HTTP/1.1\r\nHost: usr-modem.local\r\nUser-Agent: " + std::string(kMaxHeader, 'a');
    TEST_ASSERT_FALSE(
        huge.accept(reinterpret_cast<const uint8_t *>(tooLong.data()), tooLong.size()));
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
// Each side acts only while the other is still waiting.
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
// --- Authorization parser limits, nonce table, real MD5, filesystem images -----------
std::string nonceOf(const std::string &challenge) {
    return challenge.substr(challenge.find("nonce=\"") + 7, 32);
}
void test_authorization_parser_refuses_unterminated_quote_and_too_many_parameters() {
    FakeCrypto crypto;
    Auth auth(crypto);
    const char *ha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    auto freshNonce = [&]() {
        auth.reset();
        return nonceOf(auth.challenge(0));
    };
    auto n = freshNonce();
    TEST_ASSERT_TRUE(Check::kOk == auth.check(digest(n), "GET", "/api/session", ha1, 1));
    // Closing quote missing at the end; a quote opened in an extra field; a quote that ends
    // in a backslash.
    n = freshNonce();
    std::string open = digest(n);
    open.pop_back();
    TEST_ASSERT_TRUE(Check::kMissing == auth.check(open, "GET", "/api/session", ha1, 1));
    n = freshNonce();
    TEST_ASSERT_TRUE(Check::kMissing == auth.check(digest(n) + ", extra=\"never closed", "GET",
                                                   "/api/session", ha1, 1));
    n = freshNonce();
    TEST_ASSERT_TRUE(Check::kMissing == auth.check(digest(n) + ", extra=\"ends in escape\\", "GET",
                                                   "/api/session", ha1, 1));
    // 12 parameters are accepted; the 13th is refused. digest() already has 8.
    n = freshNonce();
    TEST_ASSERT_TRUE(Check::kOk == auth.check(digest(n) + ", a1=1, a2=2, a3=3, a4=4", "GET",
                                              "/api/session", ha1, 1));
    n = freshNonce();
    TEST_ASSERT_TRUE(Check::kMissing == auth.check(digest(n) + ", a1=1, a2=2, a3=3, a4=4, a5=5",
                                                   "GET", "/api/session", ha1, 1));
}
void test_authorization_header_is_limited_to_900_bytes() {
    FakeCrypto crypto;
    Auth auth(crypto);
    const char *ha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    auto n = nonceOf(auth.challenge(0));
    const std::string head = digest(n) + ", pad=";
    std::string exact = head + std::string(900 - head.size(), 'x');
    TEST_ASSERT_EQUAL(900, exact.size());
    TEST_ASSERT_TRUE(Check::kOk == auth.check(exact, "GET", "/api/session", ha1, 1));
    auth.reset();
    n = nonceOf(auth.challenge(0));
    const std::string head2 = digest(n) + ", pad=";
    std::string over = head2 + std::string(901 - head2.size(), 'x');
    TEST_ASSERT_EQUAL(901, over.size());
    TEST_ASSERT_TRUE(Check::kMissing == auth.check(over, "GET", "/api/session", ha1, 1));
    // A value over the limit of its own field.
    auth.reset();
    n = nonceOf(auth.challenge(0));
    std::string longCnonce = digest(n);
    longCnonce.replace(longCnonce.find("browser"), 7, std::string(129, 'c'));
    TEST_ASSERT_TRUE(Check::kMissing == auth.check(longCnonce, "GET", "/api/session", ha1, 1));
}
void test_ninth_challenge_evicts_the_oldest_nonce_and_a_falling_count_is_stale() {
    FakeCrypto crypto;
    Auth auth(crypto);
    const char *ha1 = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa";
    std::vector<std::string> nonces;
    for (unsigned i = 0; i < 9; ++i) {
        nonces.push_back(nonceOf(auth.challenge(10 + i)));
    }
    // The ninth challenge took the place of the first (the oldest) nonce.
    TEST_ASSERT_TRUE(Check::kStale ==
                     auth.check(digest(nonces[0]), "GET", "/api/session", ha1, 100));
    // The other eight still work. Attempts are 2 s apart so the lock-out never applies.
    for (unsigned i = 1; i < 9; ++i) {
        TEST_ASSERT_TRUE(Check::kOk ==
                         auth.check(digest(nonces[i]), "GET", "/api/session", ha1, 100 + i * 2000));
    }
    // A count that goes backwards is a replay: right password, so stale, never logged in.
    auth.reset();
    auto n = nonceOf(auth.challenge(0));
    TEST_ASSERT_TRUE(Check::kOk ==
                     auth.check(digest(n, "00000005"), "GET", "/api/session", ha1, 1));
    TEST_ASSERT_TRUE(Check::kStale ==
                     auth.check(digest(n, "00000003"), "GET", "/api/session", ha1, 2000));
    TEST_ASSERT_TRUE(Check::kStale ==
                     auth.check(digest(n, "00000005"), "GET", "/api/session", ha1, 4000));
    TEST_ASSERT_TRUE(Check::kOk ==
                     auth.check(digest(n, "00000006"), "GET", "/api/session", ha1, 6000));
}
// MD5 from RFC 1321, written for this test (the firmware uses the core's routines).
std::string md5hex(const std::string &text) {
    static const unsigned shifts[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22,
                                        5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 5, 9,  14, 20,
                                        4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                        6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t k[64];
    for (int i = 0; i < 64; ++i) {
        k[i] = uint32_t(std::floor(std::fabs(std::sin(double(i + 1))) * 4294967296.0));
    }
    std::string data = text;
    data += char(0x80);
    while (data.size() % 64 != 56) {
        data += char(0);
    }
    const uint64_t bits = uint64_t(text.size()) * 8;
    for (int i = 0; i < 8; ++i) {
        data += char((bits >> (8 * i)) & 0xff);
    }
    uint32_t a0 = 0x67452301, b0 = 0xefcdab89, c0 = 0x98badcfe, d0 = 0x10325476;
    for (size_t block = 0; block < data.size(); block += 64) {
        uint32_t m[16];
        for (int i = 0; i < 16; ++i) {
            m[i] = 0;
            for (int j = 3; j >= 0; --j) {
                m[i] = (m[i] << 8) | uint8_t(data[block + i * 4 + j]);
            }
        }
        uint32_t a = a0, b = b0, c = c0, d = d0;
        for (int i = 0; i < 64; ++i) {
            uint32_t f;
            int g;
            if (i < 16) {
                f = (b & c) | (~b & d);
                g = i;
            } else if (i < 32) {
                f = (d & b) | (~d & c);
                g = (5 * i + 1) % 16;
            } else if (i < 48) {
                f = b ^ c ^ d;
                g = (3 * i + 5) % 16;
            } else {
                f = c ^ (b | ~d);
                g = (7 * i) % 16;
            }
            f = f + a + k[i] + m[g];
            a = d;
            d = c;
            c = b;
            b = b + ((f << shifts[i]) | (f >> (32 - shifts[i])));
        }
        a0 += a;
        b0 += b;
        c0 += c;
        d0 += d;
    }
    char out[33];
    const uint32_t words[4] = {a0, b0, c0, d0};
    for (int w = 0; w < 4; ++w) {
        for (int i = 0; i < 4; ++i) {
            snprintf(out + w * 8 + i * 2, 3, "%02x", unsigned((words[w] >> (8 * i)) & 0xff));
        }
    }
    return out;
}
class RealCrypto : public FakeCrypto {
public:
    bool md5(const std::string &text, char out[33]) override {
        strcpy(out, md5hex(text).c_str());
        return !fail;
    }
};
void test_md5_vectors_and_the_rfc2617_digest_formula() {
    TEST_ASSERT_EQUAL_STRING("d41d8cd98f00b204e9800998ecf8427e", md5hex("").c_str());
    TEST_ASSERT_EQUAL_STRING("900150983cd24fb0d6963f7d28e17f72", md5hex("abc").c_str());
    TEST_ASSERT_EQUAL_STRING(
        "57edf4a22be3c955ac49da2e2107b67a",
        md5hex("12345678901234567890123456789012345678901234567890123456789012345678901234567890")
            .c_str());
    // RFC 2617 section 3.5 example (qop=auth): the response a browser would send.
    const std::string ha1 = md5hex("Mufasa:testrealm@host.com:Circle Of Life");
    const std::string ha2 = md5hex("GET:/dir/index.html");
    TEST_ASSERT_EQUAL_STRING("939e7578ed9e3c518a452acee763bce9", ha1.c_str());
    TEST_ASSERT_EQUAL_STRING(
        "6629fae49393a05397450978507c4ef1",
        md5hex(ha1 + ":dcd98b7102dd2f0e8b11d0f600bfb0c093:00000001:0a4f113b:auth:" + ha2).c_str());
}
void test_digest_login_works_end_to_end_with_real_md5() {
    RealCrypto crypto;
    Auth auth(crypto);
    const std::string ha1 = md5hex("admin:usr-modem:CorrectHorse9");
    auto browser = [&](const std::string &nonce, const char *nc, const std::string &password,
                       const char *method) {
        const std::string key = md5hex("admin:usr-modem:" + password);
        const std::string response = md5hex(key + ':' + nonce + ':' + nc + ":browser:auth:" +
                                            md5hex(std::string(method) + ":/api/session"));
        return "Digest username=\"admin\", realm=\"usr-modem\", nonce=\"" + nonce +
               "\", uri=\"/api/session\", response=\"" + response + "\", qop=auth, nc=" + nc +
               ", cnonce=\"browser\"";
    };
    auto n = nonceOf(auth.challenge(0));
    TEST_ASSERT_TRUE(Check::kWrong == auth.check(browser(n, "00000001", "WrongPassword1", "GET"),
                                                 "GET", "/api/session", ha1.c_str(), 5));
    // Inside the lock-out second the right answer is refused; after it, it works.
    TEST_ASSERT_TRUE(Check::kWrong == auth.check(browser(n, "00000001", "CorrectHorse9", "GET"),
                                                 "GET", "/api/session", ha1.c_str(), 500));
    TEST_ASSERT_TRUE(Check::kOk == auth.check(browser(n, "00000001", "CorrectHorse9", "GET"), "GET",
                                              "/api/session", ha1.c_str(), 1005));
    // The same response again is a replay; a response made for GET fails for POST.
    TEST_ASSERT_TRUE(Check::kStale == auth.check(browser(n, "00000001", "CorrectHorse9", "GET"),
                                                 "GET", "/api/session", ha1.c_str(), 2000));
    TEST_ASSERT_TRUE(Check::kWrong == auth.check(browser(n, "00000002", "CorrectHorse9", "GET"),
                                                 "POST", "/api/session", ha1.c_str(), 3000));
    TEST_ASSERT_TRUE(Check::kOk == auth.check(browser(n, "00000003", "CorrectHorse9", "GET"), "GET",
                                              "/api/session", ha1.c_str(), 5000));
}
// A filesystem image has one exact size and the LITTLEFS marker; a firmware marker is not
// enough, and neither is the right marker in the wrong-sized image.
std::string fsImage(size_t length, const std::string &marker, size_t at) {
    std::string image(length, '\xff');
    image.replace(at, marker.size(), marker);
    return image;
}
bool feedImage(ImageCheck &check, const std::string &image, size_t chunk) {
    bool ok = true;
    for (size_t i = 0; i < image.size() && ok; i += chunk) {
        const size_t n = std::min(chunk, image.size() - i);
        ok = check.write(reinterpret_cast<const uint8_t *>(image.data() + i), n);
    }
    return ok;
}
void test_filesystem_image_needs_exact_size_and_littlefs_marker() {
    const size_t size = 0x7e0000;
    const std::string marker = "USRMODEM/1/LITTLEFS/0.11\n";
    {
        ImageCheck ok(true, "0.11", size);
        // The marker straddles a 4096-byte chunk boundary.
        TEST_ASSERT_TRUE(feedImage(ok, fsImage(size, marker, 4096 - 7), 4096));
        TEST_ASSERT_TRUE(ok.complete());
    }
    {
        ImageCheck shortImage(true, "0.11", size - 1);
        TEST_ASSERT_TRUE(feedImage(shortImage, fsImage(size - 1, marker, 100), 4096));
        TEST_ASSERT_FALSE(shortImage.complete());
    }
    {
        ImageCheck longImage(true, "0.11", size + 1);  // refused before any data
        TEST_ASSERT_FALSE(longImage.write(reinterpret_cast<const uint8_t *>(marker.data()), 1));
        TEST_ASSERT_FALSE(longImage.complete());
    }
    {
        ImageCheck noMarker(true, "0.11", size);
        TEST_ASSERT_TRUE(feedImage(noMarker, std::string(size, '\xff'), 65536));
        TEST_ASSERT_FALSE(noMarker.complete());
    }
    {
        ImageCheck firmwareMarker(true, "0.11", size);
        TEST_ASSERT_TRUE(
            feedImage(firmwareMarker, fsImage(size, "USRMODEM/1/FIRMWARE/0.11\n", 100), 65536));
        TEST_ASSERT_FALSE(firmwareMarker.complete());
    }
    {
        ImageCheck wrongVersion(true, "0.11", size);
        TEST_ASSERT_TRUE(
            feedImage(wrongVersion, fsImage(size, "USRMODEM/1/LITTLEFS/0.10\n", 100), 65536));
        TEST_ASSERT_FALSE(wrongVersion.complete());
    }
    {
        ImageCheck extra(true, "0.11", size);
        TEST_ASSERT_TRUE(feedImage(extra, fsImage(size, marker, 100), 65536));
        TEST_ASSERT_FALSE(extra.write(reinterpret_cast<const uint8_t *>(marker.data()), 1));
        TEST_ASSERT_FALSE(extra.complete());
    }
    ImageCheck badVersion(true, "0.11-dev", size);
    TEST_ASSERT_FALSE(badVersion.write(reinterpret_cast<const uint8_t *>(marker.data()), 1));
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
    RUN_TEST(test_json_passes_valid_utf8_and_escapes_invalid_bytes);
    RUN_TEST(test_authorization_parser_refuses_unterminated_quote_and_too_many_parameters);
    RUN_TEST(test_authorization_header_is_limited_to_900_bytes);
    RUN_TEST(test_ninth_challenge_evicts_the_oldest_nonce_and_a_falling_count_is_stale);
    RUN_TEST(test_md5_vectors_and_the_rfc2617_digest_formula);
    RUN_TEST(test_digest_login_works_end_to_end_with_real_md5);
    RUN_TEST(test_filesystem_image_needs_exact_size_and_littlefs_marker);
    return UNITY_END();
}
