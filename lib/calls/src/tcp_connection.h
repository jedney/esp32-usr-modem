#pragma once
#include "connection.h"
#include "phonebook.h"
#include "ssh_client.h"
#include "telnet.h"

namespace calls {
enum class SocketState : uint8_t {
    kClosed,
    kResolving,
    kResolved,  // after resolve(): address() holds the IPv4 address; no connection
    kPending,
    kOpen,
    kDnsError,
    kRefused,
    kFailed
};
// A reset while connect is pending is an active rejection on lwIP. This mapping
// is not used for established connections, whose reset is a carrier loss.
SocketState connectErrorState(int error);
class Socket {
public:
    virtual ~Socket() = default;
    virtual void begin(const char *host, uint16_t port) = 0;
    // Looks up a name without connecting. state() goes kResolving, then kResolved
    // (address() gives the dotted IPv4 address) or kDnsError. close() abandons it.
    virtual void resolve(const char *host) = 0;
    virtual const char *address() const = 0;
    virtual void poll() = 0;
    virtual SocketState state() const = 0;
    virtual size_t write(const uint8_t *data, size_t len) = 0;
    virtual size_t read(uint8_t *data, size_t len) = 0;
    virtual void close() = 0;
    virtual const char *peer() const { return "unknown"; }
};
// How the bytes of a call are carried. SshOrTelnet is a direct dial to port 22: the call
// is SSH when the server opens with an SSH greeting, otherwise Telnet.
enum class Mode : uint8_t { Telnet, Raw, Ssh, SshOrTelnet };
class TcpConnection : public hostport::Connection {
public:
    // How long a port-22 call waits for the server's first bytes before it is Telnet.
    static constexpr uint32_t kSniffMs = 3000;
    // ssh: the client for SSH calls; without one, SSH falls back to Telnet.
    explicit TcpConnection(Socket &socket, ssh::Client *ssh = nullptr)
        : socket_(socket), ssh_(ssh) {}
    // host is shown in the SSH password prompt.
    void begin(Mode mode, const char *host = "");
    size_t space() const override;
    size_t write(const uint8_t *data, size_t len) override;
    size_t read(uint8_t *buf, size_t cap) override;
    bool isOpen() const override;
    void close() override;
    void poll(uint32_t nowMs) override;
    void terminalSize(unsigned columns, unsigned rows) override;
    // The far end has answered: at once for Telnet and raw calls, after the key exchange
    // for SSH, and once a port-22 call knows what it is.
    bool ready() const;
    // Why an SSH call ended, or "".
    const char *reason() const;
    // What the call turned out to be.
    Protocol protocol() const;

private:
    void decide(uint32_t now);
    void startSsh();
    void pollSsh();
    Socket &socket_;
    ssh::Client *ssh_;
    Telnet telnet_;
    Mode mode_ = Mode::Telnet;
    bool sshStarted_ = false, openSeen_ = false;
    uint32_t openAt_ = 0;
    uint8_t held_[16] = {};
    size_t heldSize_ = 0;
    char host_[64] = {};
    unsigned columns_ = 80, rows_ = 24;
};
}  // namespace calls
