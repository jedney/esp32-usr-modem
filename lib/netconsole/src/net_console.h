// The network console: the maintenance console on a raw TCP port (like ser2net), so the
// modem can be used and debugged over Wi-Fi without the console cable. No Telnet
// negotiation; bytes pass as they are.
//
//   - One logged-in client at a time. A new connection gets its own password prompt; when
//     it logs in, the earlier client is told and closed (so a hung connection never
//     keeps anyone out, and nobody without the password can throw a client out).
//   - Log-in first: "Password: ", no echo, the device password. Nothing else is sent
//     before log-in. A wrong password locks every log-in out for kLockOutMs (attempts in
//     that time are refused without checking, as on the web page); kTries wrong passwords
//     close the connection, and so does kLoginMs without a log-in.
//   - After log-in the client gets the console output kept in the ConsoleLog (the boot
//     messages, if they still fit), then the live output, and what it types goes to the
//     console's AT session (read()). It is the same session as the UART, not a second one.
//   - Never waits: output goes to the socket only as far as it takes it now. A client
//     that stops reading falls behind in the ConsoleLog; when it is a whole log behind, the
//     oldest output is skipped and the client is told how much.
//
// Port 23 callers are a different thing (calls::Incoming) and never reach command mode.
#pragma once

#include <cstddef>
#include <cstdint>

#include "console_log.h"
#include "modemcfg.h"
#include "tcp_connection.h"

namespace netconsole {

constexpr uint16_t kPort = 2001;

// The on/off setting (AT*NETCONSOLE), in its own NVS record. Factory setting: on.
class Settings {
public:
    explicit Settings(modemcfg::Store &store) : store_(store) {}
    bool begin();  // false: the record is damaged; the factory setting is used
    bool enabled() const { return enabled_; }
    bool setEnabled(bool on);

private:
    modemcfg::Store &store_;
    bool enabled_ = true;
};

// What the console needs from the firmware.
class Host {
public:
    virtual ~Host() = default;
    // True if `password` is the device password. Compared in constant time.
    virtual bool passwordMatches(const char *password) = 0;
    virtual void loggedIn() = 0;  // the device password has been used
};

// The TCP port. A connection is accepted into one of the console's two sockets.
class Listener {
public:
    virtual ~Listener() = default;
    virtual bool begin(uint16_t port) = 0;
    // The waiting connection, if there is one, into socket `slot` (0 or 1); whatever that
    // socket had is closed then. False (and nothing changes) when none is waiting.
    virtual bool accept(unsigned slot) = 0;
    virtual void stop() = 0;
};

class Console {
public:
    static constexpr unsigned kTries = 3;
    static constexpr uint32_t kLockOutMs = 1000;  // same as the web log-in
    static constexpr uint32_t kLoginMs = 60000;   // to type the password
    // The prompt waits this long after the connection opens: pyserial's socket:// (and so
    // pio device monitor) empties its input buffer just after connecting, which would
    // throw away a prompt sent at once. An empty line prompts again.
    static constexpr uint32_t kPromptDelayMs = 250;
    static constexpr uint32_t kRetryMs = 5000;  // between tries to open the port
    static constexpr size_t kPasswordMax = 64;  // longer input is a wrong password
    static constexpr size_t kFlushMax = 4096;   // bytes sent per poll at most
    static constexpr size_t kLoginBytes = 256;  // password bytes read per poll at most

    enum class State : uint8_t { kOff, kListening, kLogin, kReady };

    Console(Listener &listener, calls::Socket &first, calls::Socket &second, const ConsoleLog &log,
            Host &host);
    ~Console();
    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;

    // Every loop pass. `ip` is the Wi-Fi address (0 while Wi-Fi is down); the port closes
    // and opens again when it changes. Turning the console off closes every client.
    void poll(bool enabled, uint32_t ip, uint32_t now);
    // Sends the logged-in client what it has not had yet, as far as its socket takes it.
    void flush();
    // What the logged-in client typed, for the console session.
    size_t read(uint8_t *out, size_t cap);

    // kReady while a client is logged in, else kLogin while one is at the password prompt.
    State state() const;
    bool loggedIn() const { return ready() != nullptr; }
    const char *peer() const;     // the logged-in client's address, or ""
    const char *waiting() const;  // the address of a client at the password prompt, or ""

private:
    enum class Stage : uint8_t { kFree, kLogin, kReady };
    struct Client {
        calls::Socket *socket = nullptr;
        Stage stage = Stage::kFree;
        uint32_t since = 0;
        unsigned tries = 0;
        bool afterCr = false;
        bool prompted = false;
        char password[kPasswordMax + 1] = {};
        size_t length = 0;
        bool overflow = false;
    };
    static constexpr unsigned kSlots = 2;

    const Client *ready() const;
    Client *ready();
    void stop();
    void accept(uint32_t now);
    void login(Client &client, uint32_t now);
    void submit(Client &client, uint32_t now);
    void drop(Client &client);
    static void send(Client &client, const char *text);
    static void clearPassword(Client &client);
    bool sendNote(Client &client);

    Listener &listener_;
    const ConsoleLog &log_;
    Host &host_;
    Client clients_[kSlots];
    bool listening_ = false;
    uint32_t ip_ = 0;
    bool tried_ = false;
    uint32_t triedAt_ = 0;
    bool wrong_ = false;
    uint32_t wrongAt_ = 0;
    // The logged-in client's place in the log, and the "skipped" note still to send.
    uint64_t cursor_ = 0;
    uint64_t skipped_ = 0;
    char note_[64] = {};
    size_t noteAt_ = 0, noteLength_ = 0;
};

}  // namespace netconsole
