#include "tcp_socket.h"
#include <fcntl.h>
#include <lwip/dns.h>
#include <lwip/sockets.h>
#include <lwip/tcpip.h>
#include <cerrno>
#include <new>

namespace hw {
using calls::SocketState;
namespace {
// Keepalive as TcpSocket::KeepAlive says (tcp_socket.h).
void tuneSocket(int fd, const TcpSocket::KeepAlive &keep) {
    const int yes = 1;
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &yes, sizeof yes);
    lwip_setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &yes, sizeof yes);
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPIDLE, &keep.idleSeconds, sizeof keep.idleSeconds);
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPINTVL, &keep.intervalSeconds,
                    sizeof keep.intervalSeconds);
    lwip_setsockopt(fd, IPPROTO_TCP, TCP_KEEPCNT, &keep.count, sizeof keep.count);
}
}  // namespace

bool TcpSocket::adopt(int fd) {
    close();
    if (fd < 0) {
        return false;
    }
    fd_ = fd;
    sockaddr_in remote = {};
    socklen_t len = sizeof remote;
    if (lwip_getpeername(fd_, reinterpret_cast<sockaddr *>(&remote), &len) == 0) {
        ip4addr_ntoa_r(reinterpret_cast<const ip4_addr_t *>(&remote.sin_addr), peer_, sizeof peer_);
    }
    int flags = fcntl(fd_, F_GETFL, 0);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
        close();
        return false;
    }
    tuneSocket(fd_, keepAlive_);
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
    resolveOnly_ = false;
    lookup(host);
}
void TcpSocket::resolve(const char *host) {
    close();
    resolveOnly_ = true;
    lookup(host);
}
void TcpSocket::found(uint32_t address) {
    if (!resolveOnly_) {
        connectAddress(address);
        return;
    }
    ip4_addr_t ip;
    ip4_addr_set_u32(&ip, address);
    ip4addr_ntoa_r(&ip, resolved_, sizeof resolved_);
    state_ = SocketState::kResolved;
}
void TcpSocket::lookup(const char *host) {
    resolved_[0] = 0;
    state_ = SocketState::kResolving;
    ip_addr_t ip;
    if (ipaddr_aton(host, &ip) && IP_IS_V4(&ip)) {
        found(ip4_addr_get_u32(ip_2_ip4(&ip)));
        return;
    }
    if (pending_.load() >= 4) {
        state_ = SocketState::kDnsError;
        return;
    }
    // lwIP hands the request to resolved(), which deletes it; cppcheck cannot follow the void *
    // cppcheck-suppress [unreadVariable,unusedAllocatedMemory,constVariablePointer]
    Request *request = new (std::nothrow) Request{this, generation_.load()};
    if (!request) {
        state_ = SocketState::kDnsError;
        return;
    }
    ++pending_;
    // DNS cache and callbacks belong to lwIP's TCP/IP core.
    LOCK_TCPIP_CORE();
    err_t result = dns_gethostbyname(host, &ip, resolved, request);
    UNLOCK_TCPIP_CORE();
    if (result != ERR_INPROGRESS) {
        --pending_;
        delete request;
        if (result == ERR_OK && IP_IS_V4(&ip)) {
            found(ip4_addr_get_u32(ip_2_ip4(&ip)));
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
    tuneSocket(fd_, keepAlive_);
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
        uint32_t ip = address_.load();
        portEXIT_CRITICAL(&dnsLock_);
        if (result == 1) {
            found(ip);
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
