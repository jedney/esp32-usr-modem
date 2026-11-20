#pragma once
#include "phonebook.h"
#include "session.h"
#include "tcp_connection.h"

namespace calls {
class Observer {
public:
    virtual ~Observer() = default;
    virtual void started(bool incoming, const Entry &target) = 0;
    virtual void connected(hostport::Session &session) = 0;
    // Before connected(): what the call turned out to be (a port-22 dial is SSH or Telnet).
    virtual void kindKnown(Kind kind) { (void)kind; }
    virtual void ended(atcmd::Result result) = 0;
};
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
enum class CallStage : uint8_t {
    kIdle,
    kDial,
    kDigits,
    kRingback,
    kConnecting,
    kHandshake,
    kConnectDelay,
    kOnline,
    kBusy
};
class Outgoing : public hostport::Session::Dialer {
public:
    using Clock = uint32_t (*)();
    using Network = bool (*)();
    // crypto serves SSH calls (lib/ssh).
    Outgoing(atcmd::Modem &modem, Phonebook &book, hostport::CallSlot &slot, Socket &socket,
             CallSound &sound, Clock clock, Network network, ssh::Crypto &crypto)
        : modem_(modem),
          book_(book),
          slot_(slot),
          socket_(socket),
          ssh_(crypto),
          connection_(socket, &ssh_),
          sound_(sound),
          clock_(clock),
          network_(network) {}
    bool start(hostport::Session &session, const char *text, bool pulse) override;
    void poll();
    void cancel();
    CallStage stage() const { return stage_; }
    void setObserver(Observer *observer) { observer_ = observer; }

private:
    static void cancelWait(void *self) { static_cast<Outgoing *>(self)->cancel(); }
    void connect(uint32_t now);
    void connected();
    void finish(atcmd::Result result);
    bool play(SoundStage stage);
    void setDigits(const char *text);
    bool lookupDone(uint32_t now);
    void endObservation(atcmd::Result result) {
        if (observed_ && observer_) {
            observer_->ended(result);
        }
        observed_ = false;
    }
    atcmd::Modem &modem_;
    Phonebook &book_;
    hostport::CallSlot &slot_;
    Socket &socket_;
    ssh::Client ssh_;  // before connection_, which uses it
    TcpConnection connection_;
    CallSound &sound_;
    Clock clock_;
    Network network_;
    hostport::Session *session_ = nullptr;
    Entry target_;
    Mode mode_ = Mode::Telnet;
    char name_[64] = {};  // the host as dialled, before any lookup
    modemcfg::Profile profile_ = {};
    char digits_[33] = {};
    uint32_t lineSpeed_ = 14400, reportSpeed_ = 14400, since_ = 0;
    uint32_t ringDelayMs_ = 1500, connectDelayMs_ = 250, delaySince_ = 0;
    bool pulse_ = false, dtrWasOn_ = false, lookup_ = false;
    CallStage stage_ = CallStage::kIdle;
    Observer *observer_ = nullptr;
    bool observed_ = false;
};
}  // namespace calls
