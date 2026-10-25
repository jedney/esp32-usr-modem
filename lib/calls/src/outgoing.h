#pragma once
#include "phonebook.h"
#include "session.h"
#include "tcp_connection.h"

namespace calls {
enum class SoundStage : uint8_t { kDial, kDigits, kHandshake, kBusy, kCarrier, kRing };
class CallSound {
public:
    virtual ~CallSound() = default;
    virtual bool start(SoundStage stage, const char *digits, bool pulse, uint32_t speed,
                       const modemcfg::Profile &profile) = 0;
    virtual bool running() const = 0;
    virtual bool failed() const { return false; }
    virtual void stop() = 0;
    virtual void poll(uint32_t now, const modemcfg::Profile &profile) = 0;
};
enum class CallStage : uint8_t { kIdle, kDial, kDigits, kConnecting, kHandshake, kOnline, kBusy };
class Outgoing : public hostport::Session::Dialer {
public:
    using Clock = uint32_t (*)();
    using Network = bool (*)();
    Outgoing(atcmd::Modem &modem, Phonebook &book, hostport::CallSlot &slot, Socket &socket,
             CallSound &sound, Clock clock, Network network)
        : modem_(modem),
          book_(book),
          slot_(slot),
          socket_(socket),
          connection_(socket),
          sound_(sound),
          clock_(clock),
          network_(network) {}
    bool start(hostport::Session &session, const char *text, bool pulse) override;
    void poll();
    void cancel();
    CallStage stage() const { return stage_; }

private:
    static void cancelWait(void *self) { static_cast<Outgoing *>(self)->cancel(); }
    void connect(uint32_t now);
    void connected();
    void finish(atcmd::Result result);
    bool play(SoundStage stage);
    atcmd::Modem &modem_;
    Phonebook &book_;
    hostport::CallSlot &slot_;
    Socket &socket_;
    TcpConnection connection_;
    CallSound &sound_;
    Clock clock_;
    Network network_;
    hostport::Session *session_ = nullptr;
    Entry target_;
    modemcfg::Profile profile_ = {};
    char digits_[33] = {};
    uint32_t lineSpeed_ = 14400, reportSpeed_ = 14400, since_ = 0;
    bool pulse_ = false, dtrWasOn_ = false;
    CallStage stage_ = CallStage::kIdle;
};
}  // namespace calls
