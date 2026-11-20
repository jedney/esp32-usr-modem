#include <unity.h>
#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#define FAKES_STORE
#include "../support/fakes.h"
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
    bool random(uint8_t *out, size_t len) override {
        memset(out, ++sequence, len);
        return !fail;
    }
    bool sha256(const uint8_t *, size_t, uint8_t out[32]) override {
        memset(out, 0, 32);
        return !fail;
    }
};
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
void test_tickets_are_single_use_bounded_and_expire_across_rollover() {
    FakeCrypto crypto;
    Tickets tickets(crypto);
    auto a = tickets.issue(7, 0xfffffff0u);
    TEST_ASSERT_EQUAL_UINT32(7, tickets.consume(a, 0));
    TEST_ASSERT_EQUAL_UINT32(0, tickets.consume(a, 1));
    a = tickets.issue(7, 0xfffffff0u);
    TEST_ASSERT_EQUAL_UINT32(0, tickets.consume(a, 30000));
    a = tickets.issue(7, 0);
    for (unsigned i = 0; i < 4; ++i) {
        tickets.issue(7, 1);
    }
    TEST_ASSERT_EQUAL_UINT32(0, tickets.consume(a, 2));
    a = tickets.issue(7, 3);
    tickets.reset();
    TEST_ASSERT_EQUAL_UINT32(0, tickets.consume(a, 4));
    TEST_ASSERT_TRUE(tickets.issue(0, 5).empty());  // no session, no ticket
    crypto.fail = true;
    TEST_ASSERT_TRUE(tickets.issue(7, 5).empty());
}
void test_tickets_are_bound_to_their_session_and_die_with_it() {
    FakeCrypto crypto;
    Tickets tickets(crypto);
    auto mine = tickets.issue(3, 0), theirs = tickets.issue(4, 0), later = tickets.issue(3, 0);
    tickets.revoke(3);
    TEST_ASSERT_EQUAL_UINT32(0, tickets.consume(mine, 1));
    TEST_ASSERT_EQUAL_UINT32(0, tickets.consume(later, 1));
    TEST_ASSERT_EQUAL_UINT32(4, tickets.consume(theirs, 1));
}
void test_tokens_are_32_lower_case_hex_from_16_random_bytes() {
    FakeCrypto crypto;
    char token[33];
    TEST_ASSERT_TRUE(makeToken(crypto, token));
    TEST_ASSERT_EQUAL_STRING("01010101010101010101010101010101", token);
    crypto.fail = true;
    TEST_ASSERT_FALSE(makeToken(crypto, token));
    TEST_ASSERT_EQUAL_STRING("", token);
}
void test_settings_default_persist_failure_and_corruption_fail_closed() {
    fakes::MemoryStore store;
    Settings s(store);
    TEST_ASSERT_TRUE(s.begin());
    TEST_ASSERT_TRUE(s.enabled());
    TEST_ASSERT_TRUE(s.setEnabled(false));
    Settings reboot(store);
    TEST_ASSERT_TRUE(reboot.begin());
    TEST_ASSERT_FALSE(reboot.enabled());
    store.failSave = true;
    TEST_ASSERT_FALSE(reboot.setEnabled(true));
    TEST_ASSERT_FALSE(reboot.enabled());
    store.bytes[6] ^= 1;
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
    const uint8_t input[] = {1, 2, 3, 4};
    uint8_t output[4] = {};
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
    TEST_ASSERT_EQUAL(4, queue.append(input, 4));
    TEST_ASSERT_EQUAL(0, queue.append(input, 2));
    TEST_ASSERT_EQUAL(2, queue.pop(output, 2));
    TEST_ASSERT_EQUAL(2, queue.append(input, 4));
    TEST_ASSERT_EQUAL(4, queue.pop(output, 4));
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, output, 4);
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
    const uint8_t controls[] = {0, 1, 7, 8, 9, 10, 12, 13, 27, 31};
    TEST_ASSERT_EQUAL_STRING("\xe2\x90\x80\xe2\x98\xba\a\b\t\n\xe2\x99\x80\r\x1b\xe2\x96\xbc",
                             terminalUtf8(controls, sizeof controls, true).c_str());
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
          "Content-Length: 1\r\nContent-Length: 1\r\n", " Content-Length: 1\r\n",
          "Cookie: usr_session=a\r\ncookie: usr_session=b\r\n"}) {
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
    TEST_ASSERT_TRUE(terminalSize("{\"t\":\"size\",\"cols\":65535,\"rows\":65535}", columns, rows));
    TEST_ASSERT_EQUAL(65535, columns);
    TEST_ASSERT_EQUAL(65535, rows);
    columns = 132;
    rows = 50;
    for (const char *bad :
         {"{\"t\":\"size\",\"cols\":65536,\"rows\":50}", "{\"t\":\"size\",\"cols\":80,\"rows\":0}",
          "{\"t\":\"size\",\"cols\":79,\"rows\":25}", "{\"t\":\"size\",\"cols\":80,\"rows\":24}",
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
void test_terminal_owner_requires_explicit_open() {
    TerminalOwner owner;
    TEST_ASSERT_FALSE(owner.owns(1));
    TEST_ASSERT_FALSE(owner.open(0, true));
    TEST_ASSERT_FALSE(owner.open(1, false));
    TEST_ASSERT_TRUE(owner.open(1, true));
    TEST_ASSERT_TRUE(owner.owns(1));
    TEST_ASSERT_FALSE(owner.open(2, true));
    owner.close(2);
    TEST_ASSERT_TRUE(owner.owns(1));
    owner.close(1);
    TEST_ASSERT_TRUE(owner.open(2, true));
}
void test_terminal_queue_limits_retain_the_remainder() {
    Queue<kTerminalInputBytes> input;
    Queue<kTerminalOutputBytes> output;
    std::string paste(kTerminalOutputBytes + 1000, 'p');
    auto *bytes = reinterpret_cast<const uint8_t *>(paste.data());
    TEST_ASSERT_EQUAL(2048, input.append(bytes, paste.size()));
    TEST_ASSERT_EQUAL(0, input.append(bytes, 1));
    TEST_ASSERT_EQUAL(16384, output.append(bytes, paste.size()));
    TEST_ASSERT_FALSE(output.push(bytes, 1));
    uint8_t chunk[512];
    TEST_ASSERT_EQUAL(512, output.pop(chunk, sizeof chunk));
    TEST_ASSERT_TRUE(output.push(bytes, 512));
    TEST_ASSERT_EQUAL(16384, output.size());
    TEST_ASSERT_EQUAL(1024 * 1024, kTerminalFrameBytes);
}
void test_only_terminal_owner_can_request_hangup_and_ownership_survives() {
    TerminalOwner owner;
    TEST_ASSERT_FALSE(owner.requestHangup(1));
    TEST_ASSERT_TRUE(owner.open(1, true));
    TEST_ASSERT_FALSE(owner.requestHangup(2));
    TEST_ASSERT_FALSE(owner.takeHangup(1));
    TEST_ASSERT_TRUE(owner.requestHangup(1));
    TEST_ASSERT_FALSE(owner.takeHangup(2));
    TEST_ASSERT_TRUE(owner.takeHangup(1));
    TEST_ASSERT_TRUE(owner.owns(1));
    TEST_ASSERT_FALSE(owner.takeHangup(1));
    TEST_ASSERT_TRUE(owner.requestHangup(1));
    owner.close(1);
    TEST_ASSERT_TRUE(owner.open(2, true));
    TEST_ASSERT_FALSE(owner.takeHangup(2));
}
int main() {
    UNITY_BEGIN();
    RUN_TEST(test_only_terminal_owner_can_request_hangup_and_ownership_survives);
    RUN_TEST(test_terminal_owner_requires_explicit_open);
    RUN_TEST(test_terminal_queue_limits_retain_the_remainder);
    RUN_TEST(test_terminal_geometry_is_bounded_and_cannot_be_terminal_input);
    RUN_TEST(test_raw_headers_reject_duplicates_smuggling_and_pipelining);
    RUN_TEST(test_browser_cp437_is_utf8_and_serial_source_bytes_are_unchanged);
    RUN_TEST(test_host_and_origin_do_not_accept_rebinding_authorities);
    RUN_TEST(test_upload_handshake_grant_and_refuse_on_time);
    RUN_TEST(test_upload_handshake_late_answer_changes_nothing);
    RUN_TEST(test_upload_handshake_late_unmount_is_ignored);
    RUN_TEST(test_tickets_are_single_use_bounded_and_expire_across_rollover);
    RUN_TEST(test_tickets_are_bound_to_their_session_and_die_with_it);
    RUN_TEST(test_tokens_are_32_lower_case_hex_from_16_random_bytes);
    RUN_TEST(test_settings_default_persist_failure_and_corruption_fail_closed);
    RUN_TEST(test_queue_wrap_overflow_and_secret_cleanup);
    RUN_TEST(test_version_order_is_numeric_and_invalid_is_distinct);
    RUN_TEST(test_image_identity_header_length_and_chunk_boundaries);
    RUN_TEST(test_json_escapes_untrusted_strings);
    RUN_TEST(test_json_passes_valid_utf8_and_escapes_invalid_bytes);
    RUN_TEST(test_filesystem_image_needs_exact_size_and_littlefs_marker);
    return UNITY_END();
}
