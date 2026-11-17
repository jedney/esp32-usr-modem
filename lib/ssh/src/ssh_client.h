// Basic SSH client for a modem call: the transport, password login with prompts typed in
// the terminal (PuTTY style), and one terminal session (pty "ansi" and shell).
//
// Hobby use only, for BBSes that offer SSH. Not for production or secure use: the
// server's host key is trusted without any check, the cipher set is limited, and there is
// no public-key authentication. The login name and password are never stored; the
// password is never echoed and is wiped once it has been encrypted.
//
// No Arduino dependencies: unit-tested on the PC.
#pragma once
#include "ssh_transport.h"

namespace ssh {
template <size_t N>
class Ring {
public:
    size_t size() const { return count_; }
    size_t space() const { return N - count_; }
    void clear() { head_ = count_ = 0; }
    bool put(uint8_t b) {
        if (!space()) {
            return false;
        }
        data_[(head_ + count_++) % N] = b;
        return true;
    }
    size_t peek(uint8_t *out, size_t cap) const {
        size_t n = cap < count_ ? cap : count_;
        for (size_t i = 0; i < n; ++i) {
            out[i] = data_[(head_ + i) % N];
        }
        return n;
    }
    void drop(size_t n) {
        head_ = (head_ + n) % N;
        count_ -= n;
    }
    size_t read(uint8_t *out, size_t cap) {
        size_t n = peek(out, cap);
        drop(n);
        return n;
    }

private:
    uint8_t data_[N] = {};
    size_t head_ = 0, count_ = 0;
};

class Client {
public:
    static constexpr size_t kScreenSize = 8192;  // bytes waiting for the terminal
    static constexpr size_t kTypedSize = 2048;   // bytes from the terminal waiting to go
    static constexpr size_t kMaxChannelPacket = 4096;
    static constexpr size_t kMaxUser = 64, kMaxPassword = 127;
    static constexpr unsigned kMaxTries = 3;

    explicit Client(Crypto &crypto) : transport_(crypto) {}
    // host is shown in the password prompt.
    void begin(const char *host, unsigned columns, unsigned rows);
    // Hang-up: keys and anything typed at the prompts are wiped.
    void end();

    // Network side.
    size_t inSpace() const { return transport_.inSpace(); }
    size_t receive(const uint8_t *data, size_t size) { return transport_.receive(data, size); }
    const uint8_t *outData() const { return transport_.outData(); }
    size_t outSize() const { return transport_.outSize(); }
    void sent(size_t size) { transport_.sent(size); }

    // Terminal side.
    size_t space() const;
    size_t write(const uint8_t *data, size_t size);
    size_t read(uint8_t *out, size_t capacity) { return screen_.read(out, capacity); }
    size_t waiting() const { return screen_.size(); }
    void resize(unsigned columns, unsigned rows);

    void poll();
    // The key exchange is done: the call can be answered (CONNECT).
    bool ready() const { return transport_.established(); }
    // The session is over (the server hung up, the shell ended, or the login failed).
    bool over() const { return stage_ == Stage::kOver; }
    // Why it ended, or "" for a normal end.
    const char *reason() const { return reason_; }

private:
    enum class Stage : uint8_t {
        kHandshake,
        kService,
        kUser,
        kPassword,
        kAuth,
        kOpening,
        kShell,
        kOnline,
        kOver
    };
    void handle(const uint8_t *payload, size_t size);
    void send();
    void show(const char *text);
    void showText(const uint8_t *text, size_t size);  // LF becomes CRLF
    void prompt();
    void type(uint8_t b);
    void finish(const char *message, const char *reason);
    bool sendAuth();
    bool sendData();

    Transport transport_;
    Ring<kScreenSize> screen_;
    Ring<kTypedSize> typed_;
    char host_[64] = {};
    char user_[kMaxUser + 1] = {};
    char line_[kMaxPassword + 1] = {};
    size_t lineSize_ = 0;
    uint8_t scratch_[kMaxChannelPacket + 64] = {};
    Stage stage_ = Stage::kHandshake;
    unsigned tries_ = 0;
    unsigned columns_ = 80, rows_ = 24;
    uint32_t remote_ = 0, remoteWindow_ = 0, remoteMax_ = 0, localWindow_ = 0;
    unsigned failures_ = 0, globalFailures_ = 0;
    bool serviceWanted_ = false, authWanted_ = false, openWanted_ = false, ptyWanted_ = false;
    bool shellWanted_ = false, sizeWanted_ = false;
    const char *reason_ = "";
};
}  // namespace ssh
