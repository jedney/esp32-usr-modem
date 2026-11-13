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
    // Like reject(), but first sends `line` if the connection takes it at once.
    virtual void refuse(const char *line) {
        (void)line;
        reject();
    }
    virtual void stop() = 0;
};
enum class IncomingStage : uint8_t { kIdle, kRinging, kHandshake, kOnline };

// The port S0 auto-answer uses (AT*ANSWERPORT). A fixed setting is returned as it is; AUTO
// picks the first port whose DTR is on, in the order RS232, USB, console. The console has
// no DTR, so it is the fallback. Returns kRs232, kUsb or kConsole, never kAuto.
modemcfg::AnswerPort pickAnswerPort(modemcfg::AnswerPort setting, bool rs232Dtr, bool usbDtr);
class Incoming : public hostport::Session::Answerer {
public:
    // An unanswered call is dropped after this many rings (SEC-01; about 24 s). S0 above it
    // answers on this ring.
    static constexpr uint8_t kMaxRings = 8;
    static constexpr uint32_t kRingPeriodMs = 3000;
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
    // The USB modem port's session (v2.0). Without one, USB is never chosen.
    void setUsb(hostport::Session *usb) { usb_ = usb; }
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
    hostport::Session &autoAnswerTarget();
    atcmd::Modem &modem_;
    hostport::CallSlot &slot_;
    Listener &listener_;
    Socket &socket_;
    TcpConnection connection_;
    CallSound &sound_;
    hostport::Session &console_, &host_;
    hostport::Session *usb_ = nullptr;
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
