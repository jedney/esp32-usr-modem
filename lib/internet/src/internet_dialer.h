#pragma once
#include "internet.h"
#include "netcfg.h"
#include "outgoing.h"

namespace dialup {
class Dialer : public hostport::Session::Dialer {
public:
    using Clock = uint32_t (*)();
    Dialer(atcmd::Modem &modem, Settings &settings, netcfg::Net &net, hostport::CallSlot &slot,
           Network &network, calls::CallSound &sound, Clock clock,
           hostport::Session::Dialer &fallback)
        : modem_(modem),
          settings_(settings),
          net_(net),
          slot_(slot),
          engine_(network),
          sound_(sound),
          clock_(clock),
          fallback_(fallback) {}
    bool start(hostport::Session &session, const char *text, bool pulse) override;
    void poll();
    void hangUp();
    atcmd::Reply command(atcmd::Port &port, const char *args);
    void exportSettings(atcmd::Port &port) const;
    bool active() const { return session_ != nullptr; }
    bool connected() const { return stage_ == 4; }
    Engine &engine() { return engine_; }
    void setObserver(calls::Observer *observer) { observer_ = observer; }

private:
    static void cancelled(void *self) { static_cast<Dialer *>(self)->cancel(); }
    void cancel();
    void connect();
    void fail(atcmd::Result result);
    bool play(calls::SoundStage stage);
    atcmd::Modem &modem_;
    Settings &settings_;
    netcfg::Net &net_;
    hostport::CallSlot &slot_;
    Engine engine_;
    calls::CallSound &sound_;
    Clock clock_;
    hostport::Session::Dialer &fallback_;
    hostport::Session *session_ = nullptr;
    calls::Observer *observer_ = nullptr;
    modemcfg::Profile profile_ = {};
    calls::Entry target_;
    Options options_;
    Protocol protocol_ = Protocol::Ppp;
    uint32_t lineSpeed_ = 0, reportSpeed_ = 0, stationIp_ = 0, stationMask_ = 0;
    unsigned stage_ = 0;
    bool pulse_ = false, dtr_ = false, observed_ = false;
};
}  // namespace dialup
