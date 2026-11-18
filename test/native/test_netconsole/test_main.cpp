// Network console (lib/netconsole): the console output log, the setting and the log-in.
#include <unity.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#define FAKES_STORE
#define FAKES_SOCKET
#include "../support/fakes.h"
#include "console_log.h"
#include "net_console.h"

using namespace netconsole;
using calls::SocketState;

void setUp() {}
void tearDown() {}

namespace {

// A console client: it moves bytes only while connected, and reads everything at once.
class FakeSocket : public fakes::Socket {
public:
    FakeSocket() {
        openOnly = true;
        chunk = kUnlimited;
        peerAddress = "10.0.0.5";
    }
};

// Accepts into either socket, as hw::TcpListener does: whatever the socket had is closed.
class FakeListener : public Listener {
public:
    FakeSocket *sockets[2];
    unsigned pending = 0, starts = 0, stops = 0;
    bool fail = false, open = false;
    std::vector<unsigned> slots;  // where each connection went
    FakeListener(FakeSocket &a, FakeSocket &b) : sockets{&a, &b} {}
    bool begin(uint16_t port) override {
        TEST_ASSERT_EQUAL(2001, port);
        ++starts;
        open = !fail;
        return open;
    }
    bool accept(unsigned slot) override {
        TEST_ASSERT_TRUE(slot < 2);
        if (!open || !pending) {
            return false;
        }
        --pending;
        FakeSocket &socket = *sockets[slot];
        if (socket.status == SocketState::kOpen) {
            socket.close();
        }
        socket.status = SocketState::kOpen;
        socket.output.clear();
        slots.push_back(slot);
        return true;
    }
    void stop() override {
        ++stops;
        open = false;
        pending = 0;
    }
};

class FakeHost : public Host {
public:
    std::string password = "Secret123";
    unsigned checks = 0, logins = 0;
    std::string lastPassword;
    bool passwordMatches(const char *text) override {
        ++checks;
        lastPassword = text;
        return password == text;
    }
    std::vector<std::string> peers;
    void loggedIn(const char *peer) override {
        ++logins;
        peers.push_back(peer);
    }
    std::string model = "US Robotics Sportster 14.4k";
    std::string state = "Idle";
    const char *modelName() override { return model.c_str(); }
    const char *status() override { return state.c_str(); }
};

// What a new connection gets before log-in: the model, the status, the prompt.
const char kGreeting[] = "US Robotics Sportster 14.4k\r\nIdle\r\nPassword: ";

class CountingLock : public ConsoleLog::Lock {
public:
    int depth = 0, uses = 0;
    void lock() override {
        TEST_ASSERT_EQUAL(0, depth);
        ++depth;
        ++uses;
    }
    void unlock() override { --depth; }
};

constexpr uint32_t kIp = 0x0500000A;

struct Rig {
    uint8_t storage[256] = {};
    ConsoleLog log;
    FakeSocket socket, socket2;
    FakeListener listener{socket, socket2};
    FakeHost host;
    Console console{listener, socket, socket2, log, host};
    uint32_t now = 1000;
    Rig() { log.begin(storage, sizeof storage); }
    void write(const char *text) { log.append(text, strlen(text)); }
    void poll(bool enabled = true, uint32_t ip = kIp) { console.poll(enabled, ip, now); }
    // A client connects; returns what it got.
    std::string connect() {
        poll();
        ++listener.pending;
        poll();
        now += Console::kPromptDelayMs;
        poll();
        return socket.take();
    }
    std::string type(const char *text) {
        socket.input += text;
        poll();
        return socket.take();
    }
    std::string login() {
        connect();
        return type("Secret123\r\n");
    }
    // A second client connects while the first is there; returns what it got.
    std::string connectSecond() {
        ++listener.pending;
        poll();
        now += Console::kPromptDelayMs;
        poll();
        return socket2.take();
    }
    std::string typeSecond(const char *text) {
        socket2.input += text;
        poll();
        return socket2.take();
    }
    std::string typed() {
        uint8_t buf[64];
        size_t n = console.read(buf, sizeof buf);
        return std::string(reinterpret_cast<char *>(buf), n);
    }
};

bool has(const std::string &text, const char *part) {
    return text.find(part) != std::string::npos;
}

}  // namespace

// --- ConsoleLog ---------------------------------------------------------------------------

void test_log_keeps_the_newest_bytes_and_reports_what_was_skipped() {
    uint8_t storage[8];
    ConsoleLog log;
    log.begin(storage, sizeof storage);
    log.append("abcde", 5);
    uint8_t out[16];
    uint64_t from = 0, skipped = 99;
    size_t n = log.read(from, out, sizeof out, skipped);
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL(0, skipped);
    TEST_ASSERT_EQUAL_MEMORY("abcde", out, 5);
    TEST_ASSERT_EQUAL(5, from);
    log.append("fghij", 5);  // wraps: "cdefghij" held
    TEST_ASSERT_EQUAL(2, log.start());
    TEST_ASSERT_EQUAL(10, log.end());
    n = log.read(from, out, sizeof out, skipped);
    TEST_ASSERT_EQUAL(5, n);
    TEST_ASSERT_EQUAL_MEMORY("fghij", out, 5);
    from = 0;
    n = log.read(from, out, 3, skipped);  // too old: moves to the oldest byte held
    TEST_ASSERT_EQUAL(2, skipped);
    TEST_ASSERT_EQUAL(3, n);
    TEST_ASSERT_EQUAL_MEMORY("cde", out, 3);
    TEST_ASSERT_EQUAL(5, from);
    TEST_ASSERT_EQUAL(0, log.read(from = 10, out, sizeof out, skipped));
}

void test_log_append_longer_than_the_log_keeps_its_end() {
    uint8_t storage[4];
    ConsoleLog log;
    log.begin(storage, sizeof storage);
    log.append("x", 1);
    log.append("0123456789", 10);
    TEST_ASSERT_EQUAL(11, log.end());
    TEST_ASSERT_EQUAL(7, log.start());
    uint8_t out[8];
    uint64_t from = 0, skipped = 0;
    TEST_ASSERT_EQUAL(4, log.read(from, out, sizeof out, skipped));
    TEST_ASSERT_EQUAL_MEMORY("6789", out, 4);
    TEST_ASSERT_EQUAL(7, skipped);
}

void test_log_without_storage_counts_but_keeps_nothing() {
    ConsoleLog log;
    log.begin(nullptr, 100);
    log.append("abc", 3);
    uint8_t out[8];
    uint64_t from = 0, skipped = 0;
    TEST_ASSERT_EQUAL(0, log.read(from, out, sizeof out, skipped));
    TEST_ASSERT_EQUAL(3, from);
}

void test_log_takes_its_lock_for_every_call() {
    uint8_t storage[8];
    CountingLock lock;
    ConsoleLog log;
    log.begin(storage, sizeof storage, &lock);
    log.append("ab", 2);
    uint8_t out[4];
    uint64_t from = 0, skipped = 0;
    log.read(from, out, sizeof out, skipped);
    (void)log.start();
    (void)log.end();
    TEST_ASSERT_EQUAL(5, lock.uses);
    TEST_ASSERT_EQUAL(0, lock.depth);
}

// --- Settings -----------------------------------------------------------------------------

void test_setting_is_on_from_the_factory_and_saved() {
    fakes::MemoryStore store;
    Settings settings(store);
    TEST_ASSERT_TRUE(settings.begin());
    TEST_ASSERT_TRUE(settings.enabled());
    TEST_ASSERT_TRUE(settings.setEnabled(false));
    Settings again(store);
    TEST_ASSERT_TRUE(again.begin());
    TEST_ASSERT_FALSE(again.enabled());
    store.failSave = true;
    TEST_ASSERT_FALSE(again.setEnabled(true));
    TEST_ASSERT_FALSE(again.enabled());
}

void test_damaged_setting_loads_as_on() {
    fakes::MemoryStore store;
    Settings settings(store);
    TEST_ASSERT_TRUE(settings.setEnabled(false));
    store.bytes[7] ^= 1;  // CRC
    Settings again(store);
    TEST_ASSERT_FALSE(again.begin());
    TEST_ASSERT_TRUE(again.enabled());
    store.bytes = {'U', 'S', 'R', 'W', 1, 0, 0, 0, 0, 0};  // another record
    TEST_ASSERT_FALSE(again.begin());
    TEST_ASSERT_TRUE(again.enabled());
}

// --- Listening ----------------------------------------------------------------------------

void test_listens_only_when_on_and_wifi_up() {
    Rig r;
    r.poll(false);
    r.poll(true, 0);
    TEST_ASSERT_EQUAL(0, r.listener.starts);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kOff);
    r.poll();
    TEST_ASSERT_EQUAL(1, r.listener.starts);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kListening);
    r.poll(false);
    TEST_ASSERT_FALSE(r.listener.open);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kOff);
}

void test_failed_listen_is_retried_after_five_seconds() {
    Rig r;
    r.listener.fail = true;
    r.poll();
    r.now += Console::kRetryMs - 1;
    r.poll();
    TEST_ASSERT_EQUAL(1, r.listener.starts);
    r.listener.fail = false;
    r.now += 1;
    r.poll();
    TEST_ASSERT_EQUAL(2, r.listener.starts);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kListening);
}

void test_new_address_closes_the_client_and_listens_again() {
    Rig r;
    r.login();
    r.poll(true, kIp + 1);
    TEST_ASSERT_EQUAL(1, r.socket.closes);
    TEST_ASSERT_EQUAL(2, r.listener.starts);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kListening);
}

// --- Log-in -------------------------------------------------------------------------------

void test_nothing_but_the_prompt_before_log_in() {
    Rig r;
    r.write("Device password: Secret123\r\nboot messages\r\n");
    TEST_ASSERT_EQUAL_STRING(kGreeting, r.connect().c_str());
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kLogin);
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.console.waiting());
    r.write("more output\r\n");
    TEST_ASSERT_EQUAL_STRING("", r.type("Secr").c_str());  // no echo
    TEST_ASSERT_EQUAL_STRING("", r.typed().c_str());       // nothing reaches the console
}

void test_greeting_shows_the_current_model_and_status() {
    Rig r;
    r.host.model = "US Robotics Sportster 56k";
    r.host.state = "Connected";
    TEST_ASSERT_EQUAL_STRING("US Robotics Sportster 56k\r\nConnected\r\nPassword: ",
                             r.connect().c_str());
    r.host.state = "Idle";
    std::string out = r.type("wrong\r");  // a wrong password prompts without the greeting
    TEST_ASSERT_TRUE(has(out, "Wrong password.\r\nPassword: "));
    TEST_ASSERT_FALSE(has(out, "Idle"));
}

void test_prompt_waits_for_the_client_to_finish_opening() {
    Rig r;
    r.poll();
    ++r.listener.pending;
    r.poll();
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kLogin);
    r.now += Console::kPromptDelayMs - 1;
    r.poll();
    TEST_ASSERT_EQUAL_STRING("", r.socket.take().c_str());
    r.now += 1;
    r.poll();
    TEST_ASSERT_EQUAL_STRING(kGreeting, r.socket.take().c_str());
}

void test_right_password_replays_the_log_then_live_output() {
    Rig r;
    r.write("boot line\r\n");
    std::string out = r.login();
    TEST_ASSERT_TRUE(r.console.loggedIn());
    TEST_ASSERT_EQUAL(1, r.host.logins);
    TEST_ASSERT_TRUE(has(out, "Logged in."));
    TEST_ASSERT_TRUE(has(out, "boot line\r\n"));
    TEST_ASSERT_FALSE(has(out, "Secret123"));  // never echoed
    r.write("OK\r\n");
    r.poll();
    TEST_ASSERT_EQUAL_STRING("OK\r\n", r.socket.take().c_str());
}

void test_input_after_log_in_goes_to_the_console() {
    Rig r;
    r.connect();
    r.type("Secret123\r\nATI\r");  // the command typed straight after the password
    TEST_ASSERT_TRUE(r.console.loggedIn());
    TEST_ASSERT_EQUAL_STRING("ATI\r", r.typed().c_str());
    r.socket.input = "AT\r";
    TEST_ASSERT_EQUAL_STRING("AT\r", r.typed().c_str());
}

void test_line_endings_backspace_and_empty_lines() {
    Rig r;
    r.connect();
    TEST_ASSERT_EQUAL_STRING((std::string("\r\n") + kGreeting).c_str(),
                             r.type("\r\n").c_str());  // not a try
    r.type("Secret12X\b3\r");
    TEST_ASSERT_TRUE(r.console.loggedIn());
    TEST_ASSERT_EQUAL(1, r.host.checks);
    r.socket.input = "\nAT\r";  // the LF of the password's CR LF is dropped
    TEST_ASSERT_EQUAL_STRING("AT\r", r.typed().c_str());
    r.socket.input = std::string("\0", 1);  // after that, bytes pass as they are
    TEST_ASSERT_EQUAL(1, r.typed().size());
}

void test_cr_lf_is_one_line_end() {
    Rig r;
    r.connect();
    std::string out = r.type("wrong-one\r\n");
    TEST_ASSERT_EQUAL(1, r.host.checks);
    TEST_ASSERT_TRUE(has(out, "Wrong password."));
    TEST_ASSERT_FALSE(has(out, "Password: \r\nPassword: "));
}

void test_wrong_password_locks_out_for_a_second_and_three_close() {
    Rig r;
    r.write("secret console output\r\n");
    r.connect();
    std::string out = r.type("guess1\r");
    TEST_ASSERT_TRUE(has(out, "Wrong password.\r\nPassword: "));
    TEST_ASSERT_EQUAL(1, r.host.checks);
    r.now += 500;
    out = r.type("Secret123\r");  // in the lock-out: refused without checking
    TEST_ASSERT_EQUAL(1, r.host.checks);
    TEST_ASSERT_FALSE(r.console.loggedIn());
    TEST_ASSERT_TRUE(has(out, "Wrong password."));
    r.now += 500;  // the refused try did not extend the lock-out
    out = r.type("guess3\r");
    TEST_ASSERT_EQUAL(2, r.host.checks);
    TEST_ASSERT_TRUE(has(out, "closed"));
    TEST_ASSERT_FALSE(has(out, "secret console output"));
    TEST_ASSERT_EQUAL(1, r.socket.closes);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kListening);
    TEST_ASSERT_EQUAL(0, r.host.logins);
}

void test_lock_out_covers_the_next_connection() {
    Rig r;
    r.connect();
    r.type("guess\r");
    r.socket.close();  // the guesser reconnects at once
    r.poll();
    ++r.listener.pending;
    r.poll();
    r.now += Console::kPromptDelayMs;
    r.type("Secret123\r");
    TEST_ASSERT_FALSE(r.console.loggedIn());
    TEST_ASSERT_EQUAL(1, r.host.checks);
    r.now += Console::kLockOutMs;
    r.type("Secret123\r");
    TEST_ASSERT_TRUE(r.console.loggedIn());
}

void test_overlong_password_is_wrong_without_a_check() {
    Rig r;
    r.connect();
    std::string longText(Console::kPasswordMax + 10, 'a');
    std::string out = r.type((longText + "\r").c_str());
    TEST_ASSERT_TRUE(has(out, "Wrong password."));
    TEST_ASSERT_EQUAL(0, r.host.checks);
    r.now += Console::kLockOutMs;
    r.type("Secret123\r");
    TEST_ASSERT_TRUE(r.console.loggedIn());
}

void test_no_password_in_time_closes() {
    Rig r;
    r.connect();
    r.type("Secr");
    r.now += Console::kLoginMs;
    r.poll();
    TEST_ASSERT_EQUAL(1, r.socket.closes);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kListening);
}

void test_second_client_takes_over_when_it_logs_in() {
    Rig r;
    r.write("boot line\r\n");
    r.login();
    TEST_ASSERT_EQUAL_STRING(kGreeting, r.connectSecond().c_str());
    TEST_ASSERT_EQUAL(1, r.listener.slots.back());
    TEST_ASSERT_TRUE(r.console.loggedIn());  // the first still works meanwhile
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.console.waiting());
    r.socket.input = "AT\r";
    TEST_ASSERT_EQUAL_STRING("AT\r", r.typed().c_str());
    r.write("OK\r\n");
    std::string out = r.typeSecond("Secret123\r");
    TEST_ASSERT_TRUE(has(out, "Logged in."));
    TEST_ASSERT_TRUE(has(out, "boot line\r\n"));  // the replay
    std::string first = r.socket.take();
    TEST_ASSERT_TRUE(has(first, "OK\r\n"));  // what it had not had yet, then the notice
    TEST_ASSERT_TRUE(has(first, "logged in from another connection; closed."));
    TEST_ASSERT_EQUAL(1, r.socket.closes);
    TEST_ASSERT_TRUE(r.console.loggedIn());
    TEST_ASSERT_EQUAL_STRING("", r.console.waiting());
    r.socket2.input = "ATI\r";
    TEST_ASSERT_EQUAL_STRING("ATI\r", r.typed().c_str());
    r.write("live\r\n");
    r.poll();
    TEST_ASSERT_EQUAL_STRING("live\r\n", r.socket2.take().c_str());
    TEST_ASSERT_EQUAL(2, r.host.logins);  // a takeover is a login of its own
    TEST_ASSERT_EQUAL(2, r.host.peers.size());
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.host.peers[1].c_str());
}

void test_connection_without_the_password_never_throws_the_client_out() {
    Rig r;
    r.login();
    r.connectSecond();
    for (int i = 0; i < 3; ++i) {
        r.now += Console::kLockOutMs;
        r.typeSecond("guess\r");
    }
    TEST_ASSERT_EQUAL(1, r.socket2.closes);  // three wrong passwords
    TEST_ASSERT_EQUAL(0, r.socket.closes);
    TEST_ASSERT_TRUE(r.console.loggedIn());
    r.write("still here\r\n");
    r.poll();
    TEST_ASSERT_TRUE(has(r.socket.take(), "still here"));
}

void test_new_connection_replaces_the_one_at_the_prompt() {
    Rig r;
    r.login();
    r.connectSecond();
    r.typeSecond("Secr");  // half-typed, then hangs
    TEST_ASSERT_EQUAL_STRING(kGreeting, r.connectSecond().c_str());
    TEST_ASSERT_EQUAL(1, r.socket2.closes);  // the hung one
    TEST_ASSERT_EQUAL(0, r.socket.closes);
    TEST_ASSERT_TRUE(r.console.loggedIn());
    r.typeSecond("Secret123\r");  // the earlier half password is gone
    TEST_ASSERT_EQUAL_STRING("Secret123", r.host.lastPassword.c_str());
}

void test_hung_client_at_the_prompt_keeps_nobody_out() {
    Rig r;
    r.connect();  // never types
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kLogin);
    TEST_ASSERT_EQUAL_STRING(kGreeting, r.connectSecond().c_str());
    r.typeSecond("Secret123\r");
    TEST_ASSERT_TRUE(r.console.loggedIn());
    TEST_ASSERT_EQUAL_STRING("10.0.0.5", r.console.waiting());  // the hung one, until 60 s
    r.now += Console::kLoginMs;
    r.poll();
    TEST_ASSERT_EQUAL(1, r.socket.closes);
    TEST_ASSERT_EQUAL_STRING("", r.console.waiting());
    TEST_ASSERT_TRUE(r.console.loggedIn());
}

void test_client_that_leaves_frees_the_port() {
    Rig r;
    r.login();
    r.socket.status = SocketState::kClosed;
    r.poll();
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kListening);
    TEST_ASSERT_EQUAL_STRING("", r.console.peer());
    TEST_ASSERT_EQUAL_STRING(kGreeting, r.connect().c_str());
}

void test_turning_off_sends_what_it_can_then_closes() {
    Rig r;
    r.login();
    r.write("OK\r\n");
    r.poll(false);
    TEST_ASSERT_EQUAL_STRING("OK\r\n", r.socket.take().c_str());
    TEST_ASSERT_EQUAL(1, r.socket.closes);
    TEST_ASSERT_TRUE(r.console.state() == Console::State::kOff);
    TEST_ASSERT_EQUAL(0, r.typed().size());
}

// --- Output never waits ------------------------------------------------------------------

void test_client_that_stops_reading_never_holds_up_output() {
    Rig r;
    r.login();
    r.socket.room = 0;  // the client stopped reading
    for (int i = 0; i < 100; ++i) {
        r.write("0123456789");
        r.poll();  // returns at once
    }
    TEST_ASSERT_EQUAL_STRING("", r.socket.take().c_str());
    TEST_ASSERT_TRUE(r.console.loggedIn());
    r.write("END\r\n");
    r.socket.room = 1u << 20;
    r.poll();
    std::string out = r.socket.take();
    // 1005 bytes written, 256 held: the oldest are skipped, with a note first.
    TEST_ASSERT_TRUE(has(out, "[Network console: 749 bytes skipped]"));
    TEST_ASSERT_TRUE(out.size() > 5 && out.substr(out.size() - 5) == "END\r\n");
}

void test_partial_writes_resume_where_they_stopped() {
    Rig r;
    r.login();
    r.socket.room = 3;
    r.write("abcdefgh");
    r.poll();
    TEST_ASSERT_EQUAL_STRING("abc", r.socket.take().c_str());
    r.socket.room = 100;
    r.poll();
    TEST_ASSERT_EQUAL_STRING("defgh", r.socket.take().c_str());
}

void test_one_poll_sends_at_most_a_bounded_amount() {
    uint8_t big[16384];
    ConsoleLog log;
    log.begin(big, sizeof big);
    FakeSocket socket, spare;
    FakeListener listener(socket, spare);
    FakeHost host;
    Console console(listener, socket, spare, log, host);
    console.poll(true, kIp, 0);
    ++listener.pending;
    console.poll(true, kIp, 0);
    socket.input = "Secret123\r";  // typed before the prompt: read once it is out
    console.poll(true, kIp, Console::kPromptDelayMs);
    socket.take();
    std::string text(10000, 'x');
    log.append(text.data(), text.size());
    console.poll(true, kIp, Console::kPromptDelayMs);
    TEST_ASSERT_EQUAL(Console::kFlushMax, socket.take().size());
    console.flush();
    TEST_ASSERT_EQUAL(Console::kFlushMax, socket.take().size());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_log_keeps_the_newest_bytes_and_reports_what_was_skipped);
    RUN_TEST(test_log_append_longer_than_the_log_keeps_its_end);
    RUN_TEST(test_log_without_storage_counts_but_keeps_nothing);
    RUN_TEST(test_log_takes_its_lock_for_every_call);
    RUN_TEST(test_setting_is_on_from_the_factory_and_saved);
    RUN_TEST(test_damaged_setting_loads_as_on);
    RUN_TEST(test_listens_only_when_on_and_wifi_up);
    RUN_TEST(test_failed_listen_is_retried_after_five_seconds);
    RUN_TEST(test_new_address_closes_the_client_and_listens_again);
    RUN_TEST(test_nothing_but_the_prompt_before_log_in);
    RUN_TEST(test_greeting_shows_the_current_model_and_status);
    RUN_TEST(test_prompt_waits_for_the_client_to_finish_opening);
    RUN_TEST(test_right_password_replays_the_log_then_live_output);
    RUN_TEST(test_input_after_log_in_goes_to_the_console);
    RUN_TEST(test_line_endings_backspace_and_empty_lines);
    RUN_TEST(test_cr_lf_is_one_line_end);
    RUN_TEST(test_wrong_password_locks_out_for_a_second_and_three_close);
    RUN_TEST(test_lock_out_covers_the_next_connection);
    RUN_TEST(test_overlong_password_is_wrong_without_a_check);
    RUN_TEST(test_no_password_in_time_closes);
    RUN_TEST(test_second_client_takes_over_when_it_logs_in);
    RUN_TEST(test_connection_without_the_password_never_throws_the_client_out);
    RUN_TEST(test_new_connection_replaces_the_one_at_the_prompt);
    RUN_TEST(test_hung_client_at_the_prompt_keeps_nobody_out);
    RUN_TEST(test_client_that_leaves_frees_the_port);
    RUN_TEST(test_turning_off_sends_what_it_can_then_closes);
    RUN_TEST(test_client_that_stops_reading_never_holds_up_output);
    RUN_TEST(test_partial_writes_resume_where_they_stopped);
    RUN_TEST(test_one_poll_sends_at_most_a_bounded_amount);
    return UNITY_END();
}
