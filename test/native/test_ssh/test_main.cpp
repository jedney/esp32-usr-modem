// Unit tests for the basic SSH client (lib/ssh) against a scripted server on the PC.
#include <unity.h>
#include <memory>
#include <string>
#include <vector>
#include "../support/ssh_fakes.h"
#include "ssh_client.h"

using sshfake::FakeCrypto;
using sshfake::FakeServer;
using Bytes = std::vector<uint8_t>;

void setUp() {}
void tearDown() {}

namespace {
struct Link {
    FakeCrypto crypto;
    ssh::Client client{crypto};
    FakeServer server;
    std::string screen;
    bool drain = true;  // the terminal reads everything shown

    void start(const char *host = "bbs.example.com") {
        client.begin(host, 80, 24);
        server.start();
        pump();
    }
    void pump(int rounds = 40) {
        for (int i = 0; i < rounds; ++i) {
            if (client.outSize()) {
                server.feed(client.outData(), client.outSize());
                client.sent(client.outSize());
            }
            if (!server.out.empty()) {
                size_t n =
                    server.out.size() < client.inSpace() ? server.out.size() : client.inSpace();
                n = client.receive(server.out.data(), n);
                server.out.erase(server.out.begin(), server.out.begin() + n);
            }
            client.poll();
            if (drain) {
                read();
            }
        }
    }
    void read() {
        uint8_t buf[512];
        size_t n;
        while ((n = client.read(buf, sizeof buf)) != 0) {
            screen.append(reinterpret_cast<char *>(buf), n);
        }
    }
    void type(const std::string &text) {
        client.write(reinterpret_cast<const uint8_t *>(text.data()), text.size());
        pump();
    }
    std::string take() {
        std::string s = screen;
        screen.clear();
        return s;
    }
    Bytes nextMessage() {
        TEST_ASSERT_FALSE_MESSAGE(server.messages.empty(), "no message from the client");
        Bytes m = server.messages.front();
        server.messages.pop_front();
        return m;
    }
    // Through the login prompts to an open session on server channel 7.
    void online(uint32_t window = 1 << 20, uint32_t maxPacket = 32768) {
        start();
        type("jon\r");
        type("secret\r");
        server.messages.clear();
        server.send({ssh::kUserauthSuccess});
        pump();
        Bytes open = nextMessage();
        TEST_ASSERT_EQUAL_UINT8(ssh::kChannelOpen, open[0]);
        server.send(confirmation(window, maxPacket));
        pump();
        server.messages.clear();
        server.send({ssh::kChannelSuccess, 0, 0, 0, 0});
        pump();
        take();
    }
    static Bytes confirmation(uint32_t window, uint32_t maxPacket) {
        Bytes m(17);
        ssh::Writer(m.data(), m.size())
            .byte(ssh::kChannelOpenConfirmation)
            .u32(0)
            .u32(7)
            .u32(window)
            .u32(maxPacket);
        return m;
    }
};
std::unique_ptr<Link> link() {
    return std::make_unique<Link>();
}
Bytes message(uint8_t type, const std::string &text) {
    Bytes m(5 + text.size() + 8);
    ssh::Writer w(m.data(), m.size());
    w.byte(type).string(text.data(), text.size());
    m.resize(w.size());
    return m;
}
Bytes channelData(const std::string &text, uint32_t channel = 0) {
    Bytes m(9 + text.size());
    ssh::Writer(m.data(), m.size())
        .byte(ssh::kChannelData)
        .u32(channel)
        .string(text.data(), text.size());
    return m;
}
Bytes failure(const std::string &methods) {
    Bytes m(6 + methods.size());
    ssh::Writer(m.data(), m.size())
        .byte(ssh::kUserauthFailure)
        .string(methods.data(), methods.size())
        .boolean(false);
    return m;
}
std::string field(ssh::Reader &r) {
    const uint8_t *data;
    size_t size;
    TEST_ASSERT_TRUE(r.string(data, size));
    return std::string(reinterpret_cast<const char *>(data), size);
}
std::string dataOf(const Bytes &m) {
    TEST_ASSERT_EQUAL_UINT8(ssh::kChannelData, m[0]);
    ssh::Reader r(m.data() + 1, m.size() - 1);
    uint32_t channel = 0;
    r.u32(channel);
    TEST_ASSERT_EQUAL_UINT32(7, channel);
    return field(r);
}
bool contains(const void *object, size_t size, const std::string &needle) {
    const uint8_t *p = static_cast<const uint8_t *>(object);
    for (size_t i = 0; i + needle.size() <= size; ++i) {
        if (memcmp(p + i, needle.data(), needle.size()) == 0) {
            return true;
        }
    }
    return false;
}
}  // namespace

void test_reader_is_bounded() {
    const uint8_t data[] = {0, 0, 0, 5, 'a', 'b'};
    ssh::Reader r(data, sizeof data);
    const uint8_t *s;
    size_t n;
    TEST_ASSERT_FALSE(r.string(s, n));
    TEST_ASSERT_FALSE(r.ok());
    uint8_t b = 0;
    TEST_ASSERT_FALSE(r.byte(b));
    TEST_ASSERT_EQUAL_size_t(0, r.left());
    ssh::Reader ok(data, sizeof data);
    uint32_t v = 0;
    TEST_ASSERT_TRUE(ok.u32(v));
    TEST_ASSERT_EQUAL_UINT32(5, v);
    TEST_ASSERT_TRUE(ok.skip(2));
    TEST_ASSERT_FALSE(ok.skip(1));
}
void test_writer_overflow_and_mpint() {
    uint8_t buf[8];
    ssh::Writer w(buf, sizeof buf);
    w.u32(1).u32(2);
    TEST_ASSERT_TRUE(w.ok());
    w.byte(3);
    TEST_ASSERT_FALSE(w.ok());
    uint8_t out[16];
    const uint8_t high[] = {0, 0, 0x80, 1};
    ssh::Writer m(out, sizeof out);
    m.mpint(high, sizeof high);
    const uint8_t expectHigh[] = {0, 0, 0, 3, 0, 0x80, 1};
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expectHigh, out, sizeof expectHigh);
    const uint8_t zero[] = {0, 0};
    ssh::Writer z(out, sizeof out);
    z.mpint(zero, sizeof zero);
    TEST_ASSERT_EQUAL_size_t(4, z.size());
    TEST_ASSERT_EQUAL_UINT32(0, ssh::getU32(out));
}
void test_name_lists() {
    const std::string list = "aes128-ctr,hmac-sha2-256-etm@openssh.com";
    const auto *p = reinterpret_cast<const uint8_t *>(list.data());
    TEST_ASSERT_TRUE(ssh::listHas(p, list.size(), "aes128-ctr"));
    TEST_ASSERT_FALSE(ssh::listHas(p, list.size(), "aes128"));
    TEST_ASSERT_FALSE(ssh::listHas(p, list.size(), "hmac-sha2-256"));
    const char *const client[] = {"aes256-ctr", "aes128-ctr"};
    TEST_ASSERT_EQUAL_INT(1, ssh::choose(client, 2, p, list.size()));
    const char *const none[] = {"chacha20-poly1305@openssh.com"};
    TEST_ASSERT_EQUAL_INT(-1, ssh::choose(none, 1, p, list.size()));
}

void test_handshake_reaches_login_prompt() {
    auto l = link();
    l->start();
    TEST_ASSERT_TRUE(l->client.ready());
    TEST_ASSERT_FALSE(l->client.over());
    TEST_ASSERT_EQUAL_STRING("login as: ", l->take().c_str());
    TEST_ASSERT_EQUAL_UINT(1, l->server.kexDone);
    TEST_ASSERT_TRUE(l->server.strict);
    TEST_ASSERT_EQUAL_STRING("curve25519-sha256", l->server.chosenKex.c_str());
    TEST_ASSERT_EQUAL(0, l->server.clientVersion.rfind("SSH-2.0-USRModem_", 0));
    Bytes service = l->nextMessage();
    ssh::Reader r(service.data() + 1, service.size() - 1);
    TEST_ASSERT_EQUAL_UINT8(ssh::kServiceRequest, service[0]);
    TEST_ASSERT_EQUAL_STRING("ssh-userauth", field(r).c_str());
}
void test_lines_before_the_version_are_skipped() {
    auto l = link();
    l->server.preamble = "Welcome to the BBS\r\nPlease wait\n";
    l->start();
    TEST_ASSERT_TRUE(l->client.ready());
}
void test_old_or_missing_greeting_fails() {
    auto l = link();
    l->server.version = "SSH-1.5-Old";
    l->start();
    TEST_ASSERT_TRUE(l->client.over());
    TEST_ASSERT_FALSE(l->client.ready());
    TEST_ASSERT_EQUAL_STRING("SSH: unsupported SSH version", l->client.reason());
    auto t = link();
    t->client.begin("h", 80, 24);
    std::string junk(1100, 'x');
    t->server.out.assign(junk.begin(), junk.end());
    t->pump();
    TEST_ASSERT_EQUAL_STRING("SSH: no SSH greeting", t->client.reason());
}
void test_negotiation_failures_name_the_part() {
    struct Case {
        std::string FakeServer::*list;
        const char *value, *reason;
    } cases[] = {
        {&FakeServer::kexList, "diffie-hellman-group14-sha256", "SSH: no common key exchange"},
        {&FakeServer::hostKeyList, "ssh-dss", "SSH: no common host key type"},
        {&FakeServer::cipherList, "chacha20-poly1305@openssh.com", "SSH: no common cipher"},
        {&FakeServer::macList, "umac-64@openssh.com", "SSH: no common MAC"},
        {&FakeServer::compression, "zlib", "SSH: no common compression"},
    };
    for (const Case &c : cases) {
        auto l = link();
        l->server.*(c.list) = c.value;
        if (c.list == &FakeServer::macList) {
            l->server.cipherList = "aes128-ctr";  // GCM would need no MAC
        }
        l->start();
        TEST_ASSERT_TRUE_MESSAGE(l->client.over(), c.reason);
        TEST_ASSERT_FALSE(l->client.ready());
        TEST_ASSERT_EQUAL_STRING(c.reason, l->client.reason());
    }
}
void test_gcm_needs_no_mac() {
    auto l = link();
    l->server.cipherList = "aes256-gcm@openssh.com";
    l->server.macList = "umac-64@openssh.com";
    l->start();
    TEST_ASSERT_TRUE(l->client.ready());
}
void test_every_cipher_mac_and_exchange_carries_data() {
    const char *ciphers[] = {"aes128-ctr", "aes256-ctr", "aes128-gcm@openssh.com",
                             "aes256-gcm@openssh.com"};
    const char *macs[] = {"hmac-sha2-256", "hmac-sha2-256-etm@openssh.com"};
    const char *kexes[] = {"curve25519-sha256", "curve25519-sha256@libssh.org",
                           "ecdh-sha2-nistp256"};
    for (const char *cipher : ciphers) {
        for (const char *mac : macs) {
            for (const char *kex : kexes) {
                auto l = link();
                l->server.cipherList = cipher;
                l->server.macList = mac;
                l->server.kexList = kex;
                l->online();
                TEST_ASSERT_EQUAL_STRING(kex, l->server.chosenKex.c_str());
                l->type("ping");
                TEST_ASSERT_EQUAL_STRING("ping", dataOf(l->nextMessage()).c_str());
                l->server.send(channelData("pong"));
                l->pump();
                TEST_ASSERT_EQUAL_STRING("pong", l->take().c_str());
                TEST_ASSERT_FALSE(l->server.broken);
            }
        }
    }
}
void test_key_agreement_may_take_many_polls() {
    auto l = link();
    l->crypto.busyPolls = 7;
    l->start();
    TEST_ASSERT_TRUE(l->client.ready());
    auto f = link();
    f->crypto.failAgree = true;
    f->start();
    TEST_ASSERT_EQUAL_STRING("SSH: key exchange failed", f->client.reason());
}
void test_host_key_is_trusted_but_its_shape_is_checked() {
    auto l = link();
    l->server.hostKeyList = "ssh-rsa";  // any type works: the key is never checked
    l->start();
    TEST_ASSERT_TRUE(l->client.ready());
    auto t = link();
    t->server.truncateSignature = true;
    t->start();
    TEST_ASSERT_EQUAL_STRING("SSH: bad key exchange reply", t->client.reason());
}
void test_strict_key_exchange_resets_sequence_numbers() {
    auto l = link();
    l->start();
    TEST_ASSERT_EQUAL_UINT32(0, l->server.rxSeqAtMessage.at(0));
    auto plain = link();
    plain->server.kexList = "curve25519-sha256";
    plain->start();
    TEST_ASSERT_FALSE(plain->server.strict);
    // KEXINIT 0, ECDH_INIT 1, NEWKEYS 2, then the service request
    TEST_ASSERT_EQUAL_UINT32(3, plain->server.rxSeqAtMessage.at(0));
    TEST_ASSERT_TRUE(plain->client.ready());
}
void test_strict_refuses_a_message_before_kexinit() {
    auto l = link();
    l->server.ignoreFirst = true;
    l->start();
    TEST_ASSERT_EQUAL_STRING("SSH: unexpected message before key exchange", l->client.reason());
    auto plain = link();
    plain->server.ignoreFirst = true;
    plain->server.kexList = "curve25519-sha256";
    plain->start();
    TEST_ASSERT_TRUE(plain->client.ready());
}
void test_newkeys_after_client_newkeys() {
    auto l = link();
    l->server.sendNewKeysEarly = false;
    l->start();
    TEST_ASSERT_TRUE(l->client.ready());
}
void test_login_prompts_echo_name_and_hide_password() {
    auto l = link();
    l->start();
    l->take();
    l->type("jo\x7f");
    l->type("x\bon\r");
    TEST_ASSERT_EQUAL_STRING("jo\b \bx\b \bon\r\njon@bbs.example.com's password: ",
                             l->take().c_str());
    l->type("se\x7f");
    l->type("ecret\n\r");
    TEST_ASSERT_EQUAL_STRING("\r\n", l->take().c_str());
    l->nextMessage();  // service request
    Bytes auth = l->nextMessage();
    TEST_ASSERT_EQUAL_UINT8(ssh::kUserauthRequest, auth[0]);
    ssh::Reader r(auth.data() + 1, auth.size() - 1);
    TEST_ASSERT_EQUAL_STRING("jon", field(r).c_str());
    TEST_ASSERT_EQUAL_STRING("ssh-connection", field(r).c_str());
    TEST_ASSERT_EQUAL_STRING("password", field(r).c_str());
    bool change = true;
    r.boolean(change);
    TEST_ASSERT_FALSE(change);
    TEST_ASSERT_EQUAL_STRING("secret", field(r).c_str());
}
void test_empty_login_name_asks_again() {
    auto l = link();
    l->start();
    l->take();
    l->type("\r");
    TEST_ASSERT_EQUAL_STRING("\r\nlogin as: ", l->take().c_str());
}
void test_three_wrong_passwords_hang_up() {
    auto l = link();
    l->start();
    l->type("jon\r");
    for (int i = 0; i < 3; ++i) {
        l->take();
        l->type("wrong\r");
        l->server.send(failure("publickey,password"));
        l->pump();
    }
    const std::string screen = l->take();
    TEST_ASSERT_TRUE(screen.find("Too many failed logins.") != std::string::npos);
    TEST_ASSERT_TRUE(l->client.over());
    TEST_ASSERT_EQUAL_STRING("SSH: login failed", l->client.reason());
    auto once = link();
    once->start();
    once->type("jon\r");
    once->type("wrong\r");
    once->take();
    once->server.send(failure("password"));
    once->pump();
    TEST_ASSERT_EQUAL_STRING("Access denied\r\njon@bbs.example.com's password: ",
                             once->take().c_str());
}
void test_server_without_password_login() {
    auto l = link();
    l->start();
    l->type("jon\r");
    l->type("pw\r");
    l->take();
    l->server.send(failure("publickey,keyboard-interactive"));
    l->pump();
    TEST_ASSERT_EQUAL_STRING("This server does not accept password login.\r\n", l->take().c_str());
    TEST_ASSERT_EQUAL_STRING("SSH: password login refused", l->client.reason());
}
void test_banner_lines_get_crlf() {
    auto l = link();
    l->start();
    l->type("jon\r");
    l->type("pw\r");
    l->take();
    Bytes banner = message(ssh::kUserauthBanner, "Hi\nThere\r\n");
    banner.insert(banner.end(), {0, 0, 0, 0});
    l->server.send(banner);
    l->pump();
    TEST_ASSERT_EQUAL_STRING("Hi\r\nThere\r\n", l->take().c_str());
}
void test_session_requests_pty_and_shell() {
    auto l = link();
    l->start();
    l->type("jon\r");
    l->type("pw\r");
    l->server.messages.clear();
    l->server.send({ssh::kUserauthSuccess});
    l->pump();
    Bytes open = l->nextMessage();
    ssh::Reader o(open.data() + 1, open.size() - 1);
    TEST_ASSERT_EQUAL_STRING("session", field(o).c_str());
    uint32_t sender = 1, window = 0, max = 0;
    o.u32(sender);
    o.u32(window);
    o.u32(max);
    TEST_ASSERT_EQUAL_UINT32(0, sender);
    TEST_ASSERT_EQUAL_UINT32(ssh::Client::kScreenSize, window);
    TEST_ASSERT_EQUAL_UINT32(ssh::Client::kMaxChannelPacket, max);
    l->type("early");  // typed before the shell is up: sent once it is
    l->server.send(Link::confirmation(1000, 32768));
    l->pump();
    Bytes pty = l->nextMessage();
    ssh::Reader p(pty.data() + 1, pty.size() - 1);
    uint32_t channel = 0, columns = 0, rows = 0;
    bool want = true;
    p.u32(channel);
    TEST_ASSERT_EQUAL_UINT32(7, channel);
    TEST_ASSERT_EQUAL_STRING("pty-req", field(p).c_str());
    p.boolean(want);
    TEST_ASSERT_FALSE(want);
    TEST_ASSERT_EQUAL_STRING("ansi", field(p).c_str());
    p.u32(columns);
    p.u32(rows);
    TEST_ASSERT_EQUAL_UINT32(80, columns);
    TEST_ASSERT_EQUAL_UINT32(24, rows);
    Bytes shell = l->nextMessage();
    ssh::Reader s(shell.data() + 1, shell.size() - 1);
    s.u32(channel);
    TEST_ASSERT_EQUAL_STRING("shell", field(s).c_str());
    s.boolean(want);
    TEST_ASSERT_TRUE(want);
    TEST_ASSERT_TRUE(l->server.messages.empty());
    l->server.send({ssh::kChannelSuccess, 0, 0, 0, 0});
    l->pump();
    TEST_ASSERT_EQUAL_STRING("early", dataOf(l->nextMessage()).c_str());
}
void test_refused_session_or_shell_ends_the_call() {
    auto l = link();
    l->start();
    l->type("jon\r");
    l->type("pw\r");
    l->server.send({ssh::kUserauthSuccess});
    l->pump();
    Bytes refuse(17, 0);
    refuse[0] = ssh::kChannelOpenFailure;
    l->server.send(refuse);
    l->pump();
    TEST_ASSERT_EQUAL_STRING("SSH: session refused", l->client.reason());
    auto s = link();
    s->start();
    s->type("jon\r");
    s->type("pw\r");
    s->server.send({ssh::kUserauthSuccess});
    s->pump();
    s->server.send(Link::confirmation(100, 100));
    s->pump();
    s->server.send({ssh::kChannelFailure, 0, 0, 0, 0});
    s->pump();
    TEST_ASSERT_EQUAL_STRING("SSH: shell refused", s->client.reason());
}
void test_remote_window_and_packet_size_are_respected() {
    auto l = link();
    l->online(3, 2);
    l->type("hello");
    TEST_ASSERT_EQUAL_STRING("he", dataOf(l->nextMessage()).c_str());
    TEST_ASSERT_EQUAL_STRING("l", dataOf(l->nextMessage()).c_str());
    TEST_ASSERT_TRUE(l->server.messages.empty());
    Bytes adjust(9);
    ssh::Writer(adjust.data(), adjust.size()).byte(ssh::kChannelWindowAdjust).u32(0).u32(10);
    l->server.send(adjust);
    l->pump();
    TEST_ASSERT_EQUAL_STRING("lo", dataOf(l->nextMessage()).c_str());
}
void test_window_grows_as_the_terminal_reads() {
    auto l = link();
    l->online();
    l->server.messages.clear();
    l->drain = false;
    l->server.send(channelData(std::string(4000, 'a')));
    l->server.send(channelData(std::string(1000, 'b')));
    l->pump();
    TEST_ASSERT_TRUE(l->server.messages.empty());  // nothing read yet: no more window
    TEST_ASSERT_EQUAL_size_t(5000, l->client.waiting());
    l->read();
    l->pump();
    Bytes adjust = l->nextMessage();
    TEST_ASSERT_EQUAL_UINT8(ssh::kChannelWindowAdjust, adjust[0]);
    ssh::Reader r(adjust.data() + 1, adjust.size() - 1);
    uint32_t channel = 0, add = 0;
    r.u32(channel);
    r.u32(add);
    TEST_ASSERT_EQUAL_UINT32(7, channel);
    TEST_ASSERT_EQUAL_UINT32(5000, add);
}
void test_server_overrunning_the_window_ends_the_call() {
    auto l = link();
    l->online();
    l->drain = false;
    for (int i = 0; i < 3; ++i) {
        l->server.send(channelData(std::string(3000, 'x')));
    }
    l->pump();
    TEST_ASSERT_TRUE(l->client.over());
    TEST_ASSERT_EQUAL_STRING("SSH: the server sent too much", l->client.reason());
}
void test_resize_sends_window_change() {
    auto l = link();
    l->online();
    l->server.messages.clear();
    l->client.resize(132, 50);
    l->pump();
    Bytes change = l->nextMessage();
    ssh::Reader r(change.data() + 1, change.size() - 1);
    uint32_t channel = 0, columns = 0, rows = 0;
    bool want = true;
    r.u32(channel);
    TEST_ASSERT_EQUAL_STRING("window-change", field(r).c_str());
    r.boolean(want);
    r.u32(columns);
    r.u32(rows);
    TEST_ASSERT_FALSE(want);
    TEST_ASSERT_EQUAL_UINT32(132, columns);
    TEST_ASSERT_EQUAL_UINT32(50, rows);
    l->client.resize(132, 50);
    l->pump();
    TEST_ASSERT_TRUE(l->server.messages.empty());
}
void test_requests_wanting_replies_get_failures() {
    auto l = link();
    l->online();
    l->server.messages.clear();
    Bytes global = message(ssh::kGlobalRequest, "keepalive@openssh.com");
    global.push_back(1);
    l->server.send(global);
    Bytes quiet = message(ssh::kGlobalRequest, "hostkeys-00@openssh.com");
    quiet.push_back(0);
    l->server.send(quiet);
    Bytes channel(9 + 11 + 1);
    ssh::Writer(channel.data(), channel.size())
        .byte(ssh::kChannelRequest)
        .u32(0)
        .string("exit-status")
        .boolean(true);
    l->server.send(channel);
    l->server.send({ssh::kIgnore, 0, 0, 0, 0});
    l->server.send({ssh::kDebug, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0});
    l->pump();
    TEST_ASSERT_EQUAL_UINT8(ssh::kRequestFailure, l->nextMessage()[0]);
    Bytes reply = l->nextMessage();
    TEST_ASSERT_EQUAL_UINT8(ssh::kChannelFailure, reply[0]);
    TEST_ASSERT_EQUAL_UINT32(7, ssh::getU32(reply.data() + 1));
    TEST_ASSERT_TRUE(l->server.messages.empty());
    TEST_ASSERT_FALSE(l->client.over());
}
void test_channel_close_is_a_normal_end() {
    auto l = link();
    l->online();
    l->server.messages.clear();
    l->server.send({ssh::kChannelEof, 0, 0, 0, 0});
    l->server.send({ssh::kChannelClose, 0, 0, 0, 0});
    l->pump();
    TEST_ASSERT_TRUE(l->client.over());
    TEST_ASSERT_EQUAL_STRING("", l->client.reason());
    Bytes close = l->nextMessage();
    TEST_ASSERT_EQUAL_UINT8(ssh::kChannelClose, close[0]);
    TEST_ASSERT_EQUAL_UINT32(7, ssh::getU32(close.data() + 1));
}
void test_disconnect_is_shown() {
    auto l = link();
    l->online();
    Bytes bye(5 + 4 + 3 + 4);
    ssh::Writer(bye.data(), bye.size()).byte(ssh::kDisconnect).u32(11).string("bye").string("");
    l->server.send(bye);
    l->pump();
    TEST_ASSERT_EQUAL_STRING("\r\nSSH: disconnected by the server\r\n", l->take().c_str());
    TEST_ASSERT_EQUAL_STRING("SSH: disconnected by the server", l->client.reason());
}
void test_server_rekey_holds_typed_data() {
    auto l = link();
    l->online();
    l->server.messages.clear();
    l->server.rekey();
    // The client's KEXINIT goes out; data typed during the exchange waits for it.
    l->client.write(reinterpret_cast<const uint8_t *>("abc"), 3);
    l->pump();
    TEST_ASSERT_EQUAL_UINT(2, l->server.kexDone);
    TEST_ASSERT_EQUAL_STRING("abc", dataOf(l->nextMessage()).c_str());
    l->server.send(channelData("after"));
    l->pump();
    TEST_ASSERT_EQUAL_STRING("after", l->take().c_str());
    TEST_ASSERT_FALSE(l->server.broken);
}
void test_oversized_or_damaged_packets_fail() {
    auto l = link();
    l->client.begin("h", 80, 24);
    const std::string greeting = "SSH-2.0-Big\r\n";
    l->server.out.assign(greeting.begin(), greeting.end());
    l->server.out.insert(l->server.out.end(), {0, 0, 0x9c, 0x40, 4});
    l->pump();
    TEST_ASSERT_EQUAL_STRING("SSH: bad packet", l->client.reason());
    auto d = link();
    d->online();
    d->server.send(channelData("hello"));
    d->server.out[10] ^= 1;
    d->pump();
    TEST_ASSERT_EQUAL_STRING("SSH: damaged packet", d->client.reason());
}
void test_session_message_before_key_exchange_fails() {
    auto l = link();
    l->client.begin("h", 80, 24);
    const std::string greeting = "SSH-2.0-Odd\r\n";
    l->server.out.assign(greeting.begin(), greeting.end());
    // an unencrypted SERVICE_ACCEPT: length 12, padding 10
    l->server.out.insert(l->server.out.end(), {0, 0, 0, 12, 10, ssh::kServiceAccept});
    l->server.out.insert(l->server.out.end(), 10, 0);
    l->pump();
    TEST_ASSERT_EQUAL_STRING("SSH: unexpected message before key exchange", l->client.reason());
}
void test_password_is_wiped_after_sending() {
    auto l = link();
    l->start();
    l->type("jon\r");
    l->type("Sup3rS3cret\r");
    TEST_ASSERT_FALSE(l->server.messages.empty());
    TEST_ASSERT_FALSE(contains(&l->client, sizeof l->client, "Sup3rS3cret"));
    TEST_ASSERT_FALSE(l->screen.find("Sup3rS3cret") != std::string::npos);
    // typed but never sent: wiped by the hang-up
    auto h = link();
    h->start();
    h->type("jon\r");
    h->client.write(reinterpret_cast<const uint8_t *>("Unsent99"), 8);
    h->client.end();
    TEST_ASSERT_FALSE(contains(&h->client, sizeof h->client, "Unsent99"));
    TEST_ASSERT_TRUE(h->client.over());
    TEST_ASSERT_GREATER_OR_EQUAL_UINT(2, h->crypto.forgets);
}
void test_restart_after_a_call() {
    auto l = link();
    l->online();
    l->client.end();
    l->server = FakeServer();
    l->screen.clear();
    l->start();
    TEST_ASSERT_TRUE(l->client.ready());
    TEST_ASSERT_EQUAL_STRING("login as: ", l->take().c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_reader_is_bounded);
    RUN_TEST(test_writer_overflow_and_mpint);
    RUN_TEST(test_name_lists);
    RUN_TEST(test_handshake_reaches_login_prompt);
    RUN_TEST(test_lines_before_the_version_are_skipped);
    RUN_TEST(test_old_or_missing_greeting_fails);
    RUN_TEST(test_negotiation_failures_name_the_part);
    RUN_TEST(test_gcm_needs_no_mac);
    RUN_TEST(test_every_cipher_mac_and_exchange_carries_data);
    RUN_TEST(test_key_agreement_may_take_many_polls);
    RUN_TEST(test_host_key_is_trusted_but_its_shape_is_checked);
    RUN_TEST(test_strict_key_exchange_resets_sequence_numbers);
    RUN_TEST(test_strict_refuses_a_message_before_kexinit);
    RUN_TEST(test_newkeys_after_client_newkeys);
    RUN_TEST(test_login_prompts_echo_name_and_hide_password);
    RUN_TEST(test_empty_login_name_asks_again);
    RUN_TEST(test_three_wrong_passwords_hang_up);
    RUN_TEST(test_server_without_password_login);
    RUN_TEST(test_banner_lines_get_crlf);
    RUN_TEST(test_session_requests_pty_and_shell);
    RUN_TEST(test_refused_session_or_shell_ends_the_call);
    RUN_TEST(test_remote_window_and_packet_size_are_respected);
    RUN_TEST(test_window_grows_as_the_terminal_reads);
    RUN_TEST(test_server_overrunning_the_window_ends_the_call);
    RUN_TEST(test_resize_sends_window_change);
    RUN_TEST(test_requests_wanting_replies_get_failures);
    RUN_TEST(test_channel_close_is_a_normal_end);
    RUN_TEST(test_disconnect_is_shown);
    RUN_TEST(test_server_rekey_holds_typed_data);
    RUN_TEST(test_oversized_or_damaged_packets_fail);
    RUN_TEST(test_session_message_before_key_exchange_fails);
    RUN_TEST(test_password_is_wiped_after_sending);
    RUN_TEST(test_restart_after_a_call);
    return UNITY_END();
}
