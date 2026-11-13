#pragma once
#include "incoming.h"
#include "tcp_socket.h"

namespace hw {
class TcpListener : public calls::Listener {
public:
    explicit TcpListener(TcpSocket &socket) : socket_(socket) {}
    bool begin(uint16_t port) override;
    bool accept() override;
    bool accept(TcpSocket &into);  // the waiting connection into another socket
    void reject() override;
    void stop() override;

private:
    TcpSocket &socket_;
    int fd_ = -1;
};
}  // namespace hw
