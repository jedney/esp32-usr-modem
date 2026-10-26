// Port-23 answer mode. Only local Sessions own AT parsers; a remote peer is data.
#pragma once
#include "outgoing.h"

namespace calls {
// accept() fills the Socket bound to this listener. reject() closes one new peer.
class Listener {
public:
    virtual ~Listener() = default;
    virtual bool begin(uint16_t port) = 0;
    virtual bool accept() = 0;
    virtual void reject() = 0;
    virtual void stop() = 0;
};
enum class IncomingStage : uint8_t { kIdle, kRinging, kHandshake, kOnline };
class Incoming : public hostport::Session::Answerer {
public:
    using Clock = Outgoing::Clock;
    using Network = Outgoing::Network;
    Incoming(atcmd::Modem &modem, hostport::CallSlot &slot, Listener &listener, Socket &socket,
             CallSound &sound, hostport::Session &console, hostport::Session &host, Clock clock,
             Network network)
        : modem_(modem),
          slot_(slot),
          listener_(listener),
          socket_(socket),
          connection_(socket),
          sound_(sound),
          console_(console),
          host_(host),
          clock_(clock),
          network_(network) {}
    void poll(bool available = true);
    bool answer(hostport::Session &session) override;
    void cancel(hostport::Session &session) override;
    void stop();
    bool ringing() const { return stage_ == IncomingStage::kRinging; }
    bool ri() const;
    bool listening() const { return listening_; }
    IncomingStage stage() const { return stage_; }
    void setObserver(Observer *observer) { observer_ = observer; }
    atcmd::Reply command(atcmd::Port &port, const char *args);

private:
    static void cancelWait(void *self);
    void clear(bool notify);
    void connected();
    void ring(uint32_t now);
    void ringResult(hostport::Session &session);
    bool eligible(hostport::Session &session) const;
    atcmd::Modem &modem_;
    hostport::CallSlot &slot_;
    Listener &listener_;
    Socket &socket_;
    TcpConnection connection_;
    CallSound &sound_;
    hostport::Session &console_, &host_;
    Clock clock_;
    Network network_;
    hostport::Session *session_ = nullptr;
    IncomingStage stage_ = IncomingStage::kIdle;
    bool listening_ = false;
    uint32_t ringAt_ = 0, retryAt_ = 0;
    bool retried_ = false;
    uint8_t rings_ = 0;
    uint32_t lineSpeed_ = 14400, reportSpeed_ = 14400;
    Observer *observer_ = nullptr;
};
}  // namespace calls
