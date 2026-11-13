// The network console: the maintenance console on a raw TCP port (like ser2net), so the
// modem can be used and debugged over Wi-Fi without the console cable. No Telnet
// negotiation; bytes pass as they are.
//
//   - One client at a time. Another connection gets one "in use" line and is closed.
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
#include "incoming.h"
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

    Console(calls::Listener &listener, calls::Socket &socket, const ConsoleLog &log, Host &host)
        : listener_(listener), socket_(socket), log_(log), host_(host) {}
    ~Console();
    Console(const Console &) = delete;
    Console &operator=(const Console &) = delete;

    // Every loop pass. `ip` is the Wi-Fi address (0 while Wi-Fi is down); the port closes
    // and opens again when it changes. Turning the console off closes the client.
    void poll(bool enabled, uint32_t ip, uint32_t now);
    // Sends what the client has not had yet, as far as the socket takes it now.
    void flush();
    // What a logged-in client typed, for the console session.
    size_t read(uint8_t *out, size_t cap);

    State state() const { return state_; }
    bool loggedIn() const { return state_ == State::kReady; }
    const char *peer() const;  // the client's address, or "" without one

private:
    void stop();
    void accept(uint32_t now);
    void login(uint32_t now);
    void submit(uint32_t now);
    void drop();
    void send(const char *text);
    void clearPassword();
    bool sendNote();

    calls::Listener &listener_;
    calls::Socket &socket_;
    const ConsoleLog &log_;
    Host &host_;
    State state_ = State::kOff;
    uint32_t ip_ = 0;
    bool tried_ = false;
    uint32_t triedAt_ = 0;
    uint32_t since_ = 0;
    unsigned tries_ = 0;
    bool wrong_ = false;
    uint32_t wrongAt_ = 0;
    bool afterCr_ = false;
    bool prompted_ = false;
    char password_[kPasswordMax + 1] = {};
    size_t length_ = 0;
    bool overflow_ = false;
    uint64_t cursor_ = 0;
    uint64_t skipped_ = 0;  // output the client missed, for the next note
    char note_[64] = {};    // a "skipped" note still to send
    size_t noteAt_ = 0, noteLength_ = 0;
};

}  // namespace netconsole
