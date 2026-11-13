#include "net_console.h"

#include <cstdio>
#include <cstring>

#include "devicecfg.h"

namespace netconsole {

// --- Settings ---------------------------------------------------------------------------
// "USRN", layout 1, the flag, then a CRC-32 of the first six bytes.

bool Settings::begin() {
    uint8_t data[10] = {};
    size_t n = store_.load(data, sizeof data);
    enabled_ = true;
    if (n == 0) {
        return true;
    }
    if (n != sizeof data || memcmp(data, "USRN\1", 5) || data[5] > 1) {
        return false;
    }
    uint32_t crc = modemcfg::crc32(data, 6);
    for (unsigned i = 0; i < 4; ++i) {
        if (data[6 + i] != uint8_t(crc >> (8 * i))) {
            return false;
        }
    }
    enabled_ = data[5] == 1;
    return true;
}

bool Settings::setEnabled(bool on) {
    uint8_t data[10] = {'U', 'S', 'R', 'N', 1, uint8_t(on), 0, 0, 0, 0};
    uint32_t crc = modemcfg::crc32(data, 6);
    for (unsigned i = 0; i < 4; ++i) {
        data[6 + i] = uint8_t(crc >> (8 * i));
    }
    if (!store_.save(data, sizeof data)) {
        return false;
    }
    enabled_ = on;
    return true;
}

// --- Console ----------------------------------------------------------------------------

Console::~Console() {
    clearPassword();
}

const char *Console::peer() const {
    return state_ == State::kLogin || state_ == State::kReady ? socket_.peer() : "";
}

void Console::poll(bool enabled, uint32_t ip, uint32_t now) {
    if (!enabled || ip == 0 || ip != ip_) {
        if (state_ == State::kReady) {
            flush();  // the OK of AT*NETCONSOLE=OFF, if the socket takes it
        }
        stop();
        ip_ = ip;
        if (!enabled || ip == 0) {
            return;
        }
    }
    if (state_ == State::kOff) {
        if (tried_ && now - triedAt_ < kRetryMs) {
            return;
        }
        tried_ = true;
        triedAt_ = now;
        if (!listener_.begin(kPort)) {
            return;
        }
        state_ = State::kListening;
    }
    if (state_ != State::kListening && socket_.state() != calls::SocketState::kOpen) {
        drop();  // the client went away
    }
    if (state_ == State::kListening) {
        accept(now);
    } else {
        listener_.refuse("Network console in use.\r\n");
    }
    if (state_ == State::kLogin) {
        login(now);
    }
    if (state_ == State::kReady) {
        flush();
    }
}

void Console::stop() {
    if (state_ == State::kLogin || state_ == State::kReady) {
        drop();
    }
    if (state_ != State::kOff) {
        listener_.stop();
    }
    state_ = State::kOff;
    tried_ = false;
}

void Console::accept(uint32_t now) {
    if (!listener_.accept()) {
        return;
    }
    state_ = State::kLogin;
    since_ = now;
    tries_ = 0;
    afterCr_ = false;
    prompted_ = false;
    clearPassword();
}

void Console::drop() {
    socket_.close();
    clearPassword();
    noteAt_ = noteLength_ = 0;
    skipped_ = 0;
    state_ = State::kListening;
}

void Console::send(const char *text) {
    socket_.write(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

void Console::clearPassword() {
    devicecfg::wipe(password_, sizeof password_);
    length_ = 0;
    overflow_ = false;
}

// One byte at a time, so that what follows the password stays in the socket for read().
void Console::login(uint32_t now) {
    if (now - since_ >= kLoginMs) {
        send("\r\nNo password: closed.\r\n");
        drop();
        return;
    }
    if (!prompted_) {
        if (now - since_ < kPromptDelayMs) {
            return;
        }
        prompted_ = true;
        send("Password: ");
    }
    uint8_t c = 0;
    for (size_t i = 0; i < kLoginBytes && state_ == State::kLogin; ++i) {
        if (!socket_.read(&c, 1)) {
            break;
        }
        bool cr = afterCr_;
        afterCr_ = c == '\r';
        if ((c == '\n' || c == '\0') && cr) {
            continue;  // CR LF or CR NUL ends one line
        }
        if (c == '\r' || c == '\n') {
            submit(now);
        } else if (c == 8 || c == 127) {
            if (length_ && !overflow_) {
                password_[--length_] = '\0';
            }
        } else if (length_ < kPasswordMax) {
            password_[length_++] = static_cast<char>(c);
        } else {
            overflow_ = true;
        }
    }
    c = 0;
}

void Console::submit(uint32_t now) {
    if (length_ == 0 && !overflow_) {
        send("\r\nPassword: ");
        return;
    }
    bool locked = wrong_ && now - wrongAt_ < kLockOutMs;
    bool ok = !locked && !overflow_ && host_.passwordMatches(password_);
    clearPassword();
    if (ok) {
        wrong_ = false;
        state_ = State::kReady;
        cursor_ = log_.start();  // all the console output still held
        host_.loggedIn();
        send("\r\nLogged in. Recent console output follows.\r\n");
        return;
    }
    if (!locked) {  // a refused attempt during the lock-out does not extend it
        wrong_ = true;
        wrongAt_ = now;
    }
    if (++tries_ >= kTries) {
        send("\r\nWrong password: closed.\r\n");
        drop();
        return;
    }
    send("\r\nWrong password.\r\nPassword: ");
}

void Console::flush() {
    if (state_ != State::kReady) {
        return;
    }
    uint8_t chunk[512];
    size_t sent = 0;
    while (sent < kFlushMax && socket_.state() == calls::SocketState::kOpen) {
        uint64_t from = cursor_, skipped = 0;
        size_t n = log_.read(from, chunk, sizeof chunk, skipped);
        cursor_ = from - n;  // moved to the oldest byte held if the client fell behind
        skipped_ += skipped;
        if (!sendNote() || n == 0) {
            break;
        }
        size_t w = socket_.write(chunk, n);
        cursor_ += w;
        sent += w;
        if (w < n) {
            break;
        }
    }
}

// The "skipped" note goes before the output that follows the gap. It is written only
// once the socket takes its first byte, so it counts every byte skipped until then.
// Returns true when no note is left to send.
bool Console::sendNote() {
    if (noteLength_ == 0 && skipped_ != 0) {
        int len = snprintf(note_, sizeof note_, "\r\n[Network console: %llu bytes skipped]\r\n",
                           static_cast<unsigned long long>(skipped_));
        noteLength_ = len > 0 ? static_cast<size_t>(len) : 0;
        noteAt_ = 0;
    }
    if (noteLength_ == 0) {
        return true;
    }
    size_t w =
        socket_.write(reinterpret_cast<const uint8_t *>(note_) + noteAt_, noteLength_ - noteAt_);
    if (noteAt_ == 0) {
        if (w == 0) {
            noteLength_ = 0;  // not started: written again later with the new count
            return false;
        }
        skipped_ = 0;
    }
    noteAt_ += w;
    if (noteAt_ < noteLength_) {
        return false;
    }
    noteAt_ = noteLength_ = 0;
    return true;
}

size_t Console::read(uint8_t *out, size_t cap) {
    if (state_ != State::kReady) {
        return 0;
    }
    size_t n = socket_.read(out, cap);
    if (n && afterCr_) {  // the LF or NUL of the password's line end
        afterCr_ = false;
        if (out[0] == '\n' || out[0] == '\0') {
            memmove(out, out + 1, --n);
        }
    }
    return n;
}

}  // namespace netconsole
