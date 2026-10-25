#include "tcp_socket.h"
#include <fcntl.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <cerrno>
#include <new>

namespace hw {
using calls::SocketState;
bool TcpSocket::adopt(int fd) {
    close();
    if (fd < 0) {
        return false;
    }
    fd_ = fd;
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        close();
        return false;
    }
    int yes = 1;
    lwip_setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    state_ = SocketState::kOpen;
    return true;
}
struct TcpSocket::Request {
    TcpSocket *owner;
    uint32_t generation;
};

void TcpSocket::resolved(const char *name, const ip_addr_t *address, void *context) {
    (void)name;
    Request *request = static_cast<Request *>(context);
    TcpSocket &self = *request->owner;
    portENTER_CRITICAL(&self.dnsLock_);
    if (self.generation_.load() == request->generation) {
        if (address && IP_IS_V4(address)) {
            self.address_.store(ip4_addr_get_u32(ip_2_ip4(address)));
            self.dnsState_.store(1);
        } else {
            self.dnsState_.store(2);
        }
    }
    portEXIT_CRITICAL(&self.dnsLock_);
    --self.pending_;
    delete request;
}
void TcpSocket::begin(const char *host, uint16_t port) {
    close();
    port_ = port;
    state_ = SocketState::kResolving;
    ip_addr_t address;
    if (ipaddr_aton(host, &address) && IP_IS_V4(&address)) {
        connectAddress(ip4_addr_get_u32(ip_2_ip4(&address)));
        return;
    }
    if (pending_.load() >= 4) {
        state_ = SocketState::kDnsError;
        return;
    }
    Request *request = new (std::nothrow) Request{this, generation_.load()};
    if (!request) {
        state_ = SocketState::kDnsError;
        return;
    }
    ++pending_;
    // DNS cache and callbacks belong to lwIP's TCP/IP core.
    LOCK_TCPIP_CORE();
    err_t result = dns_gethostbyname(host, &address, resolved, request);
    UNLOCK_TCPIP_CORE();
    if (result != ERR_INPROGRESS) {
        --pending_;
        delete request;
        if (result == ERR_OK && IP_IS_V4(&address)) {
            connectAddress(ip4_addr_get_u32(ip_2_ip4(&address)));
        } else {
            state_ = SocketState::kDnsError;
        }
    }
}
void TcpSocket::connectAddress(uint32_t address) {
    state_ = SocketState::kPending;
    fd_ = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ < 0) {
        state_ = SocketState::kFailed;
        return;
    }
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        close();
        state_ = SocketState::kFailed;
        return;
    }
    int yes = 1;
    lwip_setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    sockaddr_in endpoint = {};
    endpoint.sin_family = AF_INET;
    endpoint.sin_port = htons(port_);
    endpoint.sin_addr.s_addr = address;
    int result = lwip_connect(fd_, reinterpret_cast<sockaddr *>(&endpoint), sizeof endpoint);
    if (result == 0) {
        state_ = SocketState::kOpen;
    } else if (errno != EINPROGRESS && errno != EWOULDBLOCK) {
        int error = errno;
        close();
        state_ = calls::connectErrorState(error);
    }
}
void TcpSocket::poll() {
    if (state_ == SocketState::kOpen) {
        uint8_t byte;
        int n = lwip_recv(fd_, &byte, 1, MSG_PEEK | MSG_DONTWAIT);
        if (n == 0 || (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)) {
            close();
        }
        return;
    }
    if (state_ != SocketState::kPending && state_ != SocketState::kResolving) {
        return;
    }
    if (fd_ < 0) {
        portENTER_CRITICAL(&dnsLock_);
        unsigned result = dnsState_.exchange(0);
        uint32_t address = address_.load();
        portEXIT_CRITICAL(&dnsLock_);
        if (result == 1) {
            connectAddress(address);
        } else if (result == 2) {
            state_ = SocketState::kDnsError;
        }
        return;
    }
    fd_set writable, readable, errors;
    FD_ZERO(&writable);
    FD_ZERO(&readable);
    FD_ZERO(&errors);
    FD_SET(fd_, &writable);
    FD_SET(fd_, &readable);
    FD_SET(fd_, &errors);
    timeval timeout = {};
    int result = lwip_select(fd_ + 1, &readable, &writable, &errors, &timeout);
    int error = 0;
    socklen_t len = sizeof error;
    if (result < 0 || lwip_getsockopt(fd_, SOL_SOCKET, SO_ERROR, &error, &len) < 0) {
        int failure = errno;
        close();
        state_ = calls::connectErrorState(failure);
    } else if (error) {
        close();
        state_ = calls::connectErrorState(error);
    } else if (result > 0) {
        state_ = SocketState::kOpen;
    }
}
size_t TcpSocket::write(const uint8_t *data, size_t len) {
    if (state_ != SocketState::kOpen || !len) {
        return 0;
    }
    int n = lwip_send(fd_, data, len, MSG_DONTWAIT);
    if (n > 0) {
        return static_cast<size_t>(n);
    }
    if (n < 0 && errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
        close();
    }
    return 0;
}
size_t TcpSocket::read(uint8_t *data, size_t len) {
    if (state_ != SocketState::kOpen || !len) {
        return 0;
    }
    int n = lwip_recv(fd_, data, len, MSG_DONTWAIT);
    if (n > 0) {
        return static_cast<size_t>(n);
    }
    if (n == 0 || (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR)) {
        close();
    }
    return 0;
}
void TcpSocket::close() {
    portENTER_CRITICAL(&dnsLock_);
    ++generation_;
    dnsState_ = 0;
    portEXIT_CRITICAL(&dnsLock_);
    if (fd_ >= 0) {
        lwip_close(fd_);
        fd_ = -1;
    }
    state_ = SocketState::kClosed;
}
}  // namespace hw
