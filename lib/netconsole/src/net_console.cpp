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

Console::Console(Listener &listener, calls::Socket &first, calls::Socket &second,
                 const ConsoleLog &log, Host &host)
    : listener_(listener), log_(log), host_(host) {
    clients_[0].socket = &first;
    clients_[1].socket = &second;
}

Console::~Console() {
    for (auto &client : clients_) {
        clearPassword(client);
    }
}

const Console::Client *Console::ready() const {
    for (const auto &client : clients_) {
        if (client.stage == Stage::kReady) {
            return &client;
        }
    }
    return nullptr;
}

Console::Client *Console::ready() {
    for (auto &client : clients_) {
        if (client.stage == Stage::kReady) {
            return &client;
        }
    }
    return nullptr;
}

Console::State Console::state() const {
    if (!listening_) {
        return State::kOff;
    }
    if (ready()) {
        return State::kReady;
    }
    for (const auto &client : clients_) {
        if (client.stage == Stage::kLogin) {
            return State::kLogin;
        }
    }
    return State::kListening;
}

const char *Console::peer() const {
    const Client *client = ready();
    return client ? client->socket->peer() : "";
}

const char *Console::waiting() const {
    for (const auto &client : clients_) {
        if (client.stage == Stage::kLogin) {
            return client.socket->peer();
        }
    }
    return "";
}

void Console::poll(bool enabled, uint32_t ip, uint32_t now) {
    if (!enabled || ip == 0 || ip != ip_) {
        flush();  // the OK of AT*NETCONSOLE=OFF, if the socket takes it
        stop();
        ip_ = ip;
        if (!enabled || ip == 0) {
            return;
        }
    }
    if (!listening_) {
        if (tried_ && now - triedAt_ < kRetryMs) {
            return;
        }
        tried_ = true;
        triedAt_ = now;
        listening_ = listener_.begin(kPort);
        if (!listening_) {
            return;
        }
    }
    for (auto &client : clients_) {
        if (client.stage != Stage::kFree && client.socket->state() != calls::SocketState::kOpen) {
            drop(client);  // the client went away
        }
    }
    accept(now);
    for (auto &client : clients_) {
        if (client.stage == Stage::kLogin) {
            login(client, now);
        }
    }
    flush();
}

void Console::stop() {
    for (auto &client : clients_) {
        if (client.stage != Stage::kFree) {
            drop(client);
        }
    }
    if (listening_) {
        listener_.stop();
    }
    listening_ = false;
    tried_ = false;
}

// A new connection takes a free socket, or the one of the client longest at the password
// prompt. It never replaces the logged-in client: that one is closed only when another
// client logs in (submit()), so a connection without the password can't throw it out.
void Console::accept(uint32_t now) {
    Client *slot = nullptr;
    for (auto &client : clients_) {
        if (client.stage == Stage::kFree) {
            slot = &client;
            break;
        }
    }
    for (auto &client : clients_) {
        if (!slot && client.stage == Stage::kLogin) {
            slot = &client;
        } else if (slot && slot->stage == Stage::kLogin && client.stage == Stage::kLogin &&
                   now - client.since > now - slot->since) {
            slot = &client;
        }
    }
    if (!slot || !listener_.accept(static_cast<unsigned>(slot - clients_))) {
        return;
    }
    clearPassword(*slot);
    slot->stage = Stage::kLogin;
    slot->since = now;
    slot->tries = 0;
    slot->afterCr = false;
    slot->prompted = false;
}

void Console::drop(Client &client) {
    client.socket->close();
    clearPassword(client);
    if (client.stage == Stage::kReady) {
        noteAt_ = noteLength_ = 0;
        skipped_ = 0;
    }
    client.stage = Stage::kFree;
}

void Console::send(Client &client, const char *text) {
    client.socket->write(reinterpret_cast<const uint8_t *>(text), strlen(text));
}

void Console::clearPassword(Client &client) {
    devicecfg::wipe(client.password, sizeof client.password);
    client.length = 0;
    client.overflow = false;
}

// One byte at a time, so that what follows the password stays in the socket for read().
void Console::login(Client &client, uint32_t now) {
    if (now - client.since >= kLoginMs) {
        send(client, "\r\nNo password: closed.\r\n");
        drop(client);
        return;
    }
    if (!client.prompted) {
        if (now - client.since < kPromptDelayMs) {
            return;
        }
        client.prompted = true;
        prompt(client);
    }
    uint8_t c = 0;
    for (size_t i = 0; i < kLoginBytes && client.stage == Stage::kLogin; ++i) {
        if (!client.socket->read(&c, 1)) {
            break;
        }
        bool cr = client.afterCr;
        client.afterCr = c == '\r';
        if ((c == '\n' || c == '\0') && cr) {
            continue;  // CR LF or CR NUL ends one line
        }
        if (c == '\r' || c == '\n') {
            submit(client, now);
        } else if (c == 8 || c == 127) {
            if (client.length && !client.overflow) {
                client.password[--client.length] = '\0';
            }
        } else if (client.length < kPasswordMax) {
            client.password[client.length++] = static_cast<char>(c);
        } else {
            client.overflow = true;
        }
    }
    c = 0;
}

void Console::prompt(Client &client) {
    send(client, host_.modelName());
    send(client, "\r\n");
    send(client, host_.status());
    send(client, "\r\nPassword: ");
}

void Console::submit(Client &client, uint32_t now) {
    if (client.length == 0 && !client.overflow) {
        send(client, "\r\n");
        prompt(client);
        return;
    }
    bool locked = wrong_ && now - wrongAt_ < kLockOutMs;
    bool ok = !locked && !client.overflow && host_.passwordMatches(client.password);
    clearPassword(client);
    if (ok) {
        wrong_ = false;
        if (Client *previous = ready()) {  // one logged-in client: the newest
            flush();
            send(*previous, "\r\nNetwork console: logged in from another connection; closed.\r\n");
            drop(*previous);
        }
        client.stage = Stage::kReady;
        cursor_ = log_.start();  // all the console output still held
        noteAt_ = noteLength_ = 0;
        skipped_ = 0;
        host_.loggedIn(client.socket->peer());
        send(client, "\r\nLogged in. Recent console output follows.\r\n");
        return;
    }
    if (!locked) {  // a refused attempt during the lock-out does not extend it
        wrong_ = true;
        wrongAt_ = now;
    }
    if (++client.tries >= kTries) {
        send(client, "\r\nWrong password: closed.\r\n");
        drop(client);
        return;
    }
    send(client, "\r\nWrong password.\r\nPassword: ");
}

void Console::flush() {
    Client *client = ready();
    if (!client) {
        return;
    }
    uint8_t chunk[512];
    size_t sent = 0;
    while (sent < kFlushMax && client->socket->state() == calls::SocketState::kOpen) {
        uint64_t from = cursor_, skipped = 0;
        size_t n = log_.read(from, chunk, sizeof chunk, skipped);
        cursor_ = from - n;  // moved to the oldest byte held if the client fell behind
        skipped_ += skipped;
        if (!sendNote(*client) || n == 0) {
            break;
        }
        size_t w = client->socket->write(chunk, n);
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
bool Console::sendNote(Client &client) {
    if (noteLength_ == 0 && skipped_ != 0) {
        int len = snprintf(note_, sizeof note_, "\r\n[Network console: %llu bytes skipped]\r\n",
                           static_cast<unsigned long long>(skipped_));
        noteLength_ = len > 0 ? static_cast<size_t>(len) : 0;
        noteAt_ = 0;
    }
    if (noteLength_ == 0) {
        return true;
    }
    size_t w = client.socket->write(reinterpret_cast<const uint8_t *>(note_) + noteAt_,
                                    noteLength_ - noteAt_);
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
    Client *client = ready();
    if (!client) {
        return 0;
    }
    size_t n = client->socket->read(out, cap);
    if (n && client->afterCr) {  // the LF or NUL of the password's line end
        client->afterCr = false;
        if (out[0] == '\n' || out[0] == '\0') {
            memmove(out, out + 1, --n);
        }
    }
    return n;
}

}  // namespace netconsole
