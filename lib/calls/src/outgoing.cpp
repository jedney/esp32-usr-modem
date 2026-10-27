#include "outgoing.h"
#include <cstring>

namespace calls {
bool Outgoing::start(hostport::Session &session, const char *text, bool pulse) {
    // ATH followed by ATD can arrive in one serial burst, before poll() observes
    // the old Session closing. Finish that lifecycle before replacing its target.
    if (stage_ == CallStage::kOnline && session_ && !session_->callUp()) {
        cancel();
    }
    dialstring::Result r = dialstring::classify(text);
    Entry target;
    if (r.kind == dialstring::Kind::Phonebook) {
        const Entry *entry = book_.find(r.number);
        if (!entry) {
            return false;
        }
        target = *entry;
    } else if (r.kind == dialstring::Kind::Host || r.kind == dialstring::Kind::IpAddress) {
        strcpy(target.host, r.host);
        target.port = r.port;
    } else {
        return false;
    }  // future reserved-number services are not TCP destinations
    if (slot_.busy()) {
        session.port().result(atcmd::Result::kBusy);
        return true;
    }
    if (observer_) {
        observer_->started(false, target);
        observed_ = true;
    }
    if (!network_()) {
        session.port().result(atcmd::Result::kNoDialTone);
        endObservation(atcmd::Result::kNoDialTone);
        return true;
    }
    if (!slot_.claim(&session)) {
        return false;
    }
    target_ = target;
    session_ = &session;
    profile_ = modem_.active();
    ringDelayMs_ = modem_.ringDelayMs();
    connectDelayMs_ = modem_.connectDelayMs();
    pulse_ = pulse;
    dtrWasOn_ = session.dtrOn();
    unsigned n = profile_.get(modemcfg::kAmpN);
    lineSpeed_ = target_.speed ? target_.speed
                 : n           ? atcmd::lineSpeedForN(n)
                               : modemcfg::kMaxLineSpeed;
    unsigned floor = profile_.get(modemcfg::kAmpU);
    if (floor && lineSpeed_ < atcmd::lineSpeedForN(floor)) {
        finish(atcmd::Result::kNoCarrier);
        return true;
    }
    reportSpeed_ = target_.speed || profile_.lineExplicit ? lineSpeed_ : modem_.model();
    // Dial the number the user entered, never the digits in a phonebook expansion.
    // A hostname has no number, so its digits come from its IP address (see poll).
    setDigits(r.kind == dialstring::Kind::Host ? "" : text);
    session.port().wait(cancelWait, this, atcmd::Result::kNoCarrier);
    if (profile_.get(modemcfg::kM) == 0 || profile_.get(modemcfg::kM) == 3) {
        connect(clock_());
    } else {
        stage_ = CallStage::kDial;
        if (r.kind == dialstring::Kind::Host) {
            // Look the name up while the dial tone plays.
            lookup_ = true;
            since_ = clock_();
            socket_.resolve(target_.host);
        }
        if (!play(SoundStage::kDial)) {
            finish(atcmd::Result::kNoCarrier);
        }
    }
    return true;
}
void Outgoing::setDigits(const char *text) {
    size_t count = 0;
    for (const char *p = text; *p && count < sizeof digits_ - 1; ++p) {
        if (*p >= '0' && *p <= '9') {
            digits_[count++] = *p;
        }
    }
    digits_[count] = 0;
}
bool Outgoing::lookupDone(uint32_t now) {
    socket_.poll();
    SocketState state = socket_.state();
    if (state == SocketState::kResolved) {
        // Connect to the address that was dialled, without a second lookup.
        strncpy(target_.host, socket_.address(), sizeof target_.host - 1);
        target_.host[sizeof target_.host - 1] = 0;
        setDigits(target_.host);
        lookup_ = false;
        return true;
    }
    if (state == SocketState::kResolving && now - since_ < uint32_t(profile_.s[7]) * 1000) {
        return false;
    }
    finish(atcmd::Result::kNoDialTone);
    return false;
}
bool Outgoing::play(SoundStage stage) {
    return sound_.start(stage, digits_, pulse_, lineSpeed_, profile_);
}
void Outgoing::connect(uint32_t now) {
    lookup_ = false;  // begin() replaces any unfinished lookup
    stage_ = CallStage::kConnecting;
    since_ = now;
    socket_.begin(target_.host, target_.port);
    connection_.begin(target_.raw);
}
void Outgoing::connected() {
    session_->port().finish(atcmd::Reply::kDone);
    if (!session_->startCall(connection_, lineSpeed_, reportSpeed_)) {
        finish(atcmd::Result::kNoCarrier);
        return;
    }
    stage_ = CallStage::kOnline;
    if (observer_) {
        observer_->connected(*session_);
    }
    sound_.stop();
    if (modem_.active().get(modemcfg::kM) == 2) {
        play(SoundStage::kCarrier);
    }
}
void Outgoing::cancel() {
    if (session_) {
        endObservation(atcmd::Result::kNoCarrier);
    }
    sound_.stop();
    connection_.close();
    if (session_) {
        slot_.release(session_);
    }
    session_ = nullptr;
    lookup_ = false;
    stage_ = CallStage::kIdle;
}
void Outgoing::finish(atcmd::Result result) {
    endObservation(result);
    if (session_) {
        session_->port().finish(atcmd::Reply::kDone);
        if (session_->callUp()) {
            session_->hangUp();
        }
        session_->port().result(result);
    }
    cancel();
}
void Outgoing::poll() {
    // A command can start a call after the main loop sampled its timestamp.
    // Read the clock here so unsigned deadline arithmetic cannot see the past.
    const uint32_t now = clock_();
    if (stage_ == CallStage::kIdle) {
        return;
    }
    sound_.poll(now, modem_.active());
    if (sound_.failed()) {
        finish(atcmd::Result::kNoCarrier);
        return;
    }
    if (stage_ == CallStage::kOnline) {
        if (!network_()) {
            connection_.close();
        }
        if (!session_->callUp()) {
            cancel();
            return;
        }
        if (modem_.active().get(modemcfg::kM) == 2) {
            if (!sound_.running()) {
                play(SoundStage::kCarrier);
            }
        } else {
            sound_.stop();
        }
        return;
    }
    if (!network_()) {
        finish(atcmd::Result::kNoDialTone);
        return;
    }
    bool dtr = session_->dtrOn();
    if (dtrWasOn_ && !dtr && profile_.get(modemcfg::kAmpD) >= 2) {
        finish(atcmd::Result::kNoCarrier);
        return;
    }
    dtrWasOn_ = dtr;
    bool silent = modem_.active().get(modemcfg::kM) == 0;
    if (silent) {
        sound_.stop();
    }
    if (stage_ == CallStage::kDial && !sound_.running()) {
        // Wait in silence after the dial tone if the lookup has not finished.
        // With ATM0 the digits are not played, so the connect does its own lookup.
        if (lookup_ && !silent && !lookupDone(now)) {
            return;
        }
        stage_ = CallStage::kDigits;
        if (!silent && !play(SoundStage::kDigits)) {
            finish(atcmd::Result::kNoCarrier);
            return;
        }
    }
    if (stage_ == CallStage::kDigits && !sound_.running()) {
        if (!silent && ringDelayMs_) {
            stage_ = CallStage::kRingback;
            since_ = now;
            if (!play(SoundStage::kRing)) {
                finish(atcmd::Result::kNoCarrier);
                return;
            }
        } else {
            connect(now);
        }
    }
    if (stage_ == CallStage::kRingback) {
        if (!silent && now - since_ < ringDelayMs_) {
            return;
        }
        sound_.stop();
        connect(now);
    }
    if (stage_ == CallStage::kBusy) {
        if (!sound_.running()) {
            finish(atcmd::Result::kBusy);
        }
        return;
    }
    if (stage_ != CallStage::kConnecting && stage_ != CallStage::kHandshake &&
        stage_ != CallStage::kConnectDelay) {
        return;
    }
    connection_.poll(now);
    SocketState state = socket_.state();
    if (state == SocketState::kDnsError) {
        finish(atcmd::Result::kNoDialTone);
        return;
    }
    if (state == SocketState::kRefused) {
        stage_ = CallStage::kBusy;
        if (silent || !play(SoundStage::kBusy)) {
            finish(atcmd::Result::kBusy);
        }
        return;
    }
    if (state == SocketState::kFailed) {
        finish(atcmd::Result::kNoAnswer);
        return;
    }
    if (state == SocketState::kClosed) {
        finish(atcmd::Result::kNoCarrier);
        return;
    }
    // The pause before CONNECT is a fixed, configured wait, so S7 does not apply to it.
    if (stage_ != CallStage::kConnectDelay && now - since_ >= uint32_t(profile_.s[7]) * 1000) {
        finish(state == SocketState::kResolving ? atcmd::Result::kNoDialTone
                                                : atcmd::Result::kNoAnswer);
        return;
    }
    if (stage_ == CallStage::kConnecting && state == SocketState::kOpen) {
        if (silent) {
            connected();
            return;
        }
        stage_ = CallStage::kHandshake;
        if (!play(SoundStage::kHandshake)) {
            finish(atcmd::Result::kNoCarrier);
            return;
        }
    }
    if (stage_ == CallStage::kHandshake && !sound_.running()) {
        if (!connectDelayMs_) {
            connected();
            return;
        }
        // Pause after the handshake sound, before CONNECT. The socket keeps being
        // serviced; host data waits until the call is up.
        stage_ = CallStage::kConnectDelay;
        delaySince_ = now;
    }
    if (stage_ == CallStage::kConnectDelay && (silent || now - delaySince_ >= connectDelayMs_)) {
        connected();
    }
}
}  // namespace calls
