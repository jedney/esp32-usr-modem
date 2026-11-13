#include "tcp_listener.h"
#include <fcntl.h>
#include <lwip/sockets.h>
#include <cstring>

namespace hw {
bool TcpListener::begin(uint16_t port) {
    stop();
    fd_ = lwip_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd_ < 0) {
        return false;
    }
    int yes = 1;
    lwip_setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    int flags = fcntl(fd_, F_GETFL, 0);
    sockaddr_in endpoint = {};
    endpoint.sin_family = AF_INET;
    endpoint.sin_addr.s_addr = htonl(INADDR_ANY);
    endpoint.sin_port = htons(port);
    if (flags < 0 || fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0 ||
        lwip_bind(fd_, reinterpret_cast<sockaddr *>(&endpoint), sizeof endpoint) < 0 ||
        lwip_listen(fd_, 1) < 0) {
        stop();
        return false;
    }
    return true;
}
bool TcpListener::accept() {
    int peer = fd_ < 0 ? -1 : lwip_accept(fd_, nullptr, nullptr);
    return peer >= 0 && socket_.adopt(peer);
}
void TcpListener::reject() {
    int peer = fd_ < 0 ? -1 : lwip_accept(fd_, nullptr, nullptr);
    if (peer >= 0) {
        lwip_close(peer);
    }
}
// MSG_DONTWAIT: the line is sent only if it fits now (a short line on a new connection
// always does); the connection closes either way.
void TcpListener::refuse(const char *line) {
    int peer = fd_ < 0 ? -1 : lwip_accept(fd_, nullptr, nullptr);
    if (peer >= 0) {
        lwip_send(peer, line, strlen(line), MSG_DONTWAIT);
        lwip_close(peer);
    }
}
void TcpListener::stop() {
    if (fd_ >= 0) {
        lwip_close(fd_);
        fd_ = -1;
    }
}
}  // namespace hw
