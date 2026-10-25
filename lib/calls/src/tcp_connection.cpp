#include "tcp_connection.h"
#include <cerrno>
namespace calls {
SocketState connectErrorState(int error) {
    return error == ECONNREFUSED || error == ECONNRESET ? SocketState::kRefused
                                                        : SocketState::kFailed;
}
void TcpConnection::begin(bool raw) {
    raw_ = raw;
    telnet_.begin();
    if (raw_) {
        telnet_.tx.clear();
    }
}
size_t TcpConnection::space() const {
    return raw_ ? telnet_.tx.space() : telnet_.space();
}
size_t TcpConnection::write(const uint8_t *data, size_t len) {
    if (!raw_) {
        return telnet_.write(data, len);
    }
    size_t n = len < space() ? len : space();
    for (size_t i = 0; i < n; ++i) {
        telnet_.tx.put(data[i]);
    }
    return n;
}
size_t TcpConnection::read(uint8_t *buf, size_t cap) {
    return telnet_.rx.read(buf, cap);
}
bool TcpConnection::isOpen() const {
    return socket_.state() == SocketState::kOpen || telnet_.rx.size() != 0;
}
void TcpConnection::close() {
    socket_.close();
    telnet_.rx.clear();
    telnet_.tx.clear();
}
void TcpConnection::poll(uint32_t nowMs) {
    (void)nowMs;
    socket_.poll();
    if (socket_.state() != SocketState::kOpen) {
        return;
    }
    uint8_t buf[256];
    size_t n = telnet_.tx.peek(buf, sizeof buf);
    if (n) {
        telnet_.tx.drop(socket_.write(buf, n));
    }
    size_t cap = telnet_.rx.space();
    if (cap > sizeof buf) {
        cap = sizeof buf;
    }
    // Each negotiation byte may produce at most one terminal reply plus NAWS.
    size_t txRoom = telnet_.tx.space() / 16;
    if (!raw_ && cap > txRoom) {
        cap = txRoom;
    }
    n = socket_.read(buf, cap);
    for (size_t i = 0; i < n; ++i) {
        if (raw_) {
            telnet_.rx.put(buf[i]);
        } else {
            telnet_.feed(buf[i]);
        }
    }
}
}  // namespace calls
