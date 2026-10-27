#include "internet_dialer.h"
#include <cstring>

namespace dialup {
bool Dialer::start(hostport::Session &session, const char *text, bool pulse) {
    auto target = dialstring::classify(text);
    if (target.kind != dialstring::Kind::Special ||
        (target.special != dialstring::Special::Ppp && target.special != dialstring::Special::Slip))
        return fallback_.start(session, text, pulse);
    if (session_ && stage_ == 4 && !session_->callUp())
        cancel();
    if (slot_.busy() || session_) {
        session.port().result(atcmd::Result::kBusy);
        return true;
    }
    if (!net_.up()) {
        session.port().result(atcmd::Result::kNoDialTone);
        return true;
    }
    options_ = settings_.get();
    if (overlaps(options_, net_.link().ip, net_.link().mask))
        return false;
    if (!options_.dns)
        options_.dns = net_.link().dns ? net_.link().dns : net_.link().gateway;
    if (!validOptions(options_) || !options_.dns || !slot_.claim(&session))
        return false;
    session_ = &session;
    protocol_ = target.special == dialstring::Special::Ppp ? Protocol::Ppp : Protocol::Slip;
    profile_ = modem_.active();
    const unsigned n = profile_.get(modemcfg::kAmpN);
    lineSpeed_ = n ? atcmd::lineSpeedForN(n) : modemcfg::kMaxLineSpeed;
    reportSpeed_ = profile_.lineExplicit ? lineSpeed_ : modem_.model();
    stationIp_ = net_.link().ip;
    stationMask_ = net_.link().mask;
    pulse_ = pulse;
    dtr_ = session.dtrOn();
    target_ = {};
    strcpy(target_.number, target.number);
    strcpy(target_.host, protocol_ == Protocol::Ppp ? "Internet (PPP)" : "Internet (SLIP)");
    target_.kind = protocol_ == Protocol::Ppp ? calls::Kind::Ppp : calls::Kind::Slip;
    target_.port = 0;
    if (observer_) {
        observer_->started(false, target_);
        observed_ = true;
    }
    session.port().wait(cancelled, this, atcmd::Result::kNoCarrier);
    unsigned floor = profile_.get(modemcfg::kAmpU);
    if (floor && lineSpeed_ < atcmd::lineSpeedForN(floor)) {
        fail(atcmd::Result::kNoCarrier);
        return true;
    }
    stage_ = 1;
    if (profile_.get(modemcfg::kM) == 0 || profile_.get(modemcfg::kM) == 3)
        connect();
    else if (!play(calls::SoundStage::kDial))
        fail(atcmd::Result::kNoCarrier);
    return true;
}
bool Dialer::play(calls::SoundStage stage) {
    return sound_.start(stage, target_.number, pulse_, lineSpeed_, profile_);
}
void Dialer::cancel() {
    if (!session_)
        return;
    engine_.close();
    sound_.stop();
    if (observer_ && observed_)
        observer_->ended(atcmd::Result::kNoCarrier);
    observed_ = false;
    slot_.release(session_);
    session_ = nullptr;
    stage_ = 0;
}
void Dialer::fail(atcmd::Result result) {
    if (!session_)
        return;
    session_->port().finish(atcmd::Reply::kDone);
    session_->port().result(result);
    if (observer_ && observed_)
        observer_->ended(result);
    observed_ = false;
    cancel();
}
void Dialer::connect() {
    sound_.stop();
    if (!engine_.begin(protocol_, options_, clock_())) {
        fail(atcmd::Result::kNoCarrier);
        return;
    }
    session_->port().finish(atcmd::Reply::kDone);
    if (!session_->startCall(engine_, lineSpeed_, reportSpeed_)) {
        fail(atcmd::Result::kNoCarrier);
        return;
    }
    stage_ = 4;
    if (observer_)
        observer_->connected(*session_);
}
void Dialer::hangUp() {
    if (stage_ == 4)
        engine_.stop(Reason::Local);
    else if (session_)
        fail(atcmd::Result::kNoCarrier);
}
void Dialer::poll() {
    if (!session_)
        return;
    if (stage_ == 4) {
        if (!net_.up() || net_.link().ip != stationIp_ || net_.link().mask != stationMask_)
            engine_.stop(Reason::Network);
        if (!session_->callUp())
            cancel();
        else if (modem_.active().get(modemcfg::kM) == 2) {
            sound_.poll(clock_(), modem_.active());
            if (!sound_.running())
                play(calls::SoundStage::kCarrier);
        } else
            sound_.stop();
        return;
    }
    if (!net_.up()) {
        fail(atcmd::Result::kNoDialTone);
        return;
    }
    if (dtr_ && !session_->dtrOn() && profile_.get(modemcfg::kAmpD) >= 2) {
        fail(atcmd::Result::kNoCarrier);
        return;
    }
    dtr_ = session_->dtrOn();
    sound_.poll(clock_(), modem_.active());
    if (sound_.failed()) {
        fail(atcmd::Result::kNoCarrier);
        return;
    }
    if (modem_.active().get(modemcfg::kM) == 0) {
        connect();
        return;
    }
    if (sound_.running())
        return;
    if (stage_ == 1) {
        stage_ = 2;
        if (!play(calls::SoundStage::kDigits))
            fail(atcmd::Result::kNoCarrier);
    } else if (stage_ == 2) {
        stage_ = 3;
        if (!play(calls::SoundStage::kHandshake))
            fail(atcmd::Result::kNoCarrier);
    } else
        connect();
}
void Dialer::exportSettings(atcmd::Port &port) const {
    char local[16], peer[16], dns[16];
    const auto &o = settings_.get();
    netcfg::formatIp(o.local, local, sizeof local);
    netcfg::formatIp(o.peer, peer, sizeof peer);
    netcfg::formatIp(o.dns, dns, sizeof dns);
    port.printf("AT*INTERNET=IP,%s,%s\r\nAT*INTERNET=DNS,%s\r\n", local, peer,
                o.dns ? dns : "AUTO");
}
atcmd::Reply Dialer::command(atcmd::Port &port, const char *args) {
    using atcmd::Reply;
    if (!*args || !strcmp(args, "?")) {
        char local[16], peer[16], dns[16];
        const auto &o = settings_.get();
        netcfg::formatIp(o.local, local, sizeof local);
        netcfg::formatIp(o.peer, peer, sizeof peer);
        netcfg::formatIp(active() ? options_.dns
                         : o.dns  ? o.dns
                                  : net_.link().dns,
                         dns, sizeof dns);
        port.printf("Internet: %s; reason=%s\r\n",
                    !active()                     ? "idle"
                    : !connected()                ? "dialling"
                    : protocol_ == Protocol::Slip ? "SLIP ready"
                    : engine_.ready()             ? "PPP ready"
                                                  : "PPP negotiating",
                    reasonName(engine_.reason()));
        port.printf("IP=%s,%s /30; DNS=%s (%s)\r\n", local, peer, o.dns ? "fixed" : "AUTO", dns);
        port.printf("Packets TX=%lu RX=%lu drops=%lu\r\n",
                    static_cast<unsigned long>(engine_.sent()),
                    static_cast<unsigned long>(engine_.received()),
                    static_cast<unsigned long>(engine_.dropped()));
        return Reply::kOk;
    }
    if (!strcmp(args, "=HANGUP")) {
        hangUp();
        return Reply::kOk;
    }
    if (active())
        return Reply::kError;
    auto next = settings_.get();
    if (!strncmp(args, "=IP,", 4)) {
        const char *comma = strchr(args + 4, ',');
        if (!comma)
            return Reply::kError;
        char first[16];
        const size_t length = size_t(comma - args - 4);
        if (!length || length >= sizeof first)
            return Reply::kError;
        memcpy(first, args + 4, length);
        first[length] = 0;
        const char *end = nullptr;
        if (strpbrk(first, " \t") || strpbrk(comma + 1, ", \t") ||
            !netcfg::parseIp(first, &next.local, &end) || *end ||
            !netcfg::parseIp(comma + 1, &next.peer, &end) || *end)
            return Reply::kError;
    } else if (!strncmp(args, "=DNS,", 5)) {
        if (!strcmp(args + 5, "AUTO"))
            next.dns = 0;
        else {
            const char *end = nullptr;
            if (strpbrk(args + 5, ", \t") || !netcfg::parseIp(args + 5, &next.dns, &end) || *end ||
                !next.dns)
                return Reply::kError;
        }
    } else
        return Reply::kError;
    return (!net_.up() || !overlaps(next, net_.link().ip, net_.link().mask)) && settings_.save(next)
               ? Reply::kOk
               : Reply::kError;
}
}  // namespace dialup
