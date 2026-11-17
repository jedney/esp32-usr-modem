#include "tcp_connection.h"
#include <cerrno>
#include <cstring>
namespace calls {
namespace {
const char kGreeting[] = "SSH-";
}
SocketState connectErrorState(int error) {
    return error == ECONNREFUSED || error == ECONNRESET ? SocketState::kRefused
                                                        : SocketState::kFailed;
}
void TcpConnection::begin(Mode mode, const char *host) {
    if (sshStarted_) {
        ssh_->end();
        sshStarted_ = false;
    }
    mode_ = (mode == Mode::Ssh || mode == Mode::SshOrTelnet) && !ssh_ ? Mode::Telnet : mode;
    heldSize_ = 0;
    openSeen_ = false;
    strncpy(host_, host, sizeof host_ - 1);
    host_[sizeof host_ - 1] = 0;
    telnet_.begin();
    if (mode_ != Mode::Telnet) {
        telnet_.tx.clear();  // no Telnet negotiation, at least not yet
    }
}
size_t TcpConnection::space() const {
    switch (mode_) {
        case Mode::Raw:
            return telnet_.tx.space();
        case Mode::Telnet:
            return telnet_.space();
        case Mode::Ssh:
            return sshStarted_ ? ssh_->space() : 0;
        default:
            return 0;
    }
}
size_t TcpConnection::write(const uint8_t *data, size_t len) {
    if (mode_ == Mode::Telnet) {
        return telnet_.write(data, len);
    }
    if (mode_ == Mode::Ssh) {
        return sshStarted_ ? ssh_->write(data, len) : 0;
    }
    size_t n = len < space() ? len : space();
    for (size_t i = 0; i < n; ++i) {
        telnet_.tx.put(data[i]);
    }
    return n;
}
size_t TcpConnection::read(uint8_t *buf, size_t cap) {
    if (mode_ == Mode::Ssh) {
        return sshStarted_ ? ssh_->read(buf, cap) : 0;
    }
    return telnet_.rx.read(buf, cap);
}
bool TcpConnection::isOpen() const {
    const bool open = socket_.state() == SocketState::kOpen;
    if (mode_ == Mode::Ssh) {
        return sshStarted_ && ((open && !ssh_->over()) || ssh_->waiting() != 0);
    }
    return open || telnet_.rx.size() != 0;
}
void TcpConnection::close() {
    socket_.close();
    telnet_.rx.clear();
    telnet_.tx.clear();
    if (sshStarted_) {
        ssh_->end();
        sshStarted_ = false;
    }
}
void TcpConnection::terminalSize(unsigned columns, unsigned rows) {
    columns_ = columns;
    rows_ = rows;
    telnet_.resize(columns, rows);
    if (sshStarted_) {
        ssh_->resize(columns, rows);
    }
}
bool TcpConnection::ready() const {
    switch (mode_) {
        case Mode::Ssh:
            return sshStarted_ && ssh_->ready() && !ssh_->over();
        case Mode::SshOrTelnet:
            return false;
        default:
            return true;
    }
}
const char *TcpConnection::reason() const {
    return sshStarted_ ? ssh_->reason() : "";
}
Protocol TcpConnection::protocol() const {
    return mode_ == Mode::Raw   ? Protocol::Raw
           : mode_ == Mode::Ssh ? Protocol::Ssh
                                : Protocol::Telnet;
}
void TcpConnection::startSsh() {
    mode_ = Mode::Ssh;
    ssh_->begin(host_, columns_, rows_);
    sshStarted_ = true;
}
// A port-22 call: the server's first bytes say what it is. An SSH server greets first;
// a Telnet server either negotiates at once or waits for the client.
void TcpConnection::decide(uint32_t now) {
    if (!openSeen_) {
        openSeen_ = true;
        openAt_ = now;
    }
    heldSize_ += socket_.read(held_ + heldSize_, sizeof kGreeting - 1 - heldSize_);
    const size_t compare = heldSize_ < sizeof kGreeting - 1 ? heldSize_ : sizeof kGreeting - 1;
    const bool greeting = memcmp(held_, kGreeting, compare) == 0;
    if (greeting && heldSize_ == sizeof kGreeting - 1) {
        startSsh();
        ssh_->receive(held_, heldSize_);
        return;
    }
    if (greeting && now - openAt_ < kSniffMs) {
        return;  // nothing yet, or the start of a greeting: wait
    }
    mode_ = Mode::Telnet;
    telnet_.begin();
    telnet_.resize(columns_, rows_);
    for (size_t i = 0; i < heldSize_; ++i) {
        telnet_.feed(held_[i]);
    }
}
void TcpConnection::pollSsh() {
    if (ssh_->outSize()) {
        ssh_->sent(socket_.write(ssh_->outData(), ssh_->outSize()));
    }
    uint8_t buf[256];
    size_t cap = ssh_->inSpace();
    size_t n = socket_.read(buf, cap < sizeof buf ? cap : sizeof buf);
    ssh_->receive(buf, n);
    ssh_->poll();
    if (ssh_->outSize()) {
        ssh_->sent(socket_.write(ssh_->outData(), ssh_->outSize()));
    }
    if (ssh_->over()) {
        socket_.close();  // what is still on screen stays readable
    }
}
void TcpConnection::poll(uint32_t nowMs) {
    socket_.poll();
    if (socket_.state() != SocketState::kOpen) {
        return;
    }
    if (mode_ == Mode::SshOrTelnet) {
        decide(nowMs);
        if (mode_ == Mode::SshOrTelnet) {
            return;
        }
    }
    if (mode_ == Mode::Ssh) {
        if (!sshStarted_) {
            startSsh();
        }
        pollSsh();
        return;
    }
    const bool raw = mode_ == Mode::Raw;
    uint8_t buf[256];
    size_t n = telnet_.tx.peek(buf, sizeof buf);
    if (n) {
        telnet_.tx.drop(socket_.write(buf, n));
    }
    if (!raw)
        telnet_.poll();
    size_t cap = telnet_.rx.space();
    if (cap > sizeof buf) {
        cap = sizeof buf;
    }
    // Each negotiation byte may produce at most one terminal reply plus NAWS.
    size_t txRoom = telnet_.tx.space() / 16;
    if (!raw && cap > txRoom) {
        cap = txRoom;
    }
    n = socket_.read(buf, cap);
    for (size_t i = 0; i < n; ++i) {
        if (raw) {
            telnet_.rx.put(buf[i]);
        } else {
            telnet_.feed(buf[i]);
        }
    }
}
}  // namespace calls
