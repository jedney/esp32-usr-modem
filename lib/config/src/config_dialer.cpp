#include "config_dialer.h"
namespace config {
bool Dialer::start(hostport::Session &session, const char *text, bool pulse) {
    auto r = dialstring::classify(text);
    if (r.kind != dialstring::Kind::Special || r.special != dialstring::Special::ConfigMenu) {
        return outgoing_.start(session, text, pulse);
    }
    if (slot_.busy()) {
        session.port().result(atcmd::Result::kBusy);
        return true;
    }
    if (!slot_.claim(&session)) {
        return false;
    }
    session_ = &session;
    menu_ = &session == &console_                  ? &consoleMenu_
            : usb_ && usbMenu_ && &session == usb_ ? usbMenu_
                                                   : &hostMenu_;
    pulse_ = pulse;
    dtrOn_ = session.dtrOn();
    since_ = clock_();
    stage_ = 1;
    session.port().wait(cancelWait, this, atcmd::Result::kNoCarrier);
    auto &p = session.port().modem().active();
    if (p.get(modemcfg::kM) == 0 || p.get(modemcfg::kM) == 3) {
        connect();
    } else if (!sound_.start(calls::SoundStage::kDial, "123", pulse_, 14400, p)) {
        session.port().finish(atcmd::Reply::kError);
        cancel();
    }
    return true;
}
void Dialer::cancel() {
    if (!session_) {
        return;
    }
    sound_.stop();
    slot_.release(session_);
    session_ = nullptr;
    menu_ = nullptr;
    stage_ = 0;
}
void Dialer::connect() {
    auto *s = session_;
    auto *m = menu_;
    sound_.stop();
    s->port().finish(atcmd::Reply::kDone);
    if (!m->open() || !s->startCall(*m)) {
        m->close();
        s->port().result(atcmd::Result::kNoCarrier);
        cancel();
        return;
    }
    // Session now owns the menu connection and reservation.
    session_ = nullptr;
    menu_ = nullptr;
    stage_ = 0;
}
void Dialer::poll() {
    if (!session_) {
        return;
    }
    uint32_t now = clock_();
    auto &p = session_->port().modem().active();
    sound_.poll(now, p);
    if ((dtrOn_ && !session_->dtrOn() && p.get(modemcfg::kAmpD) >= 2) || sound_.failed()) {
        session_->port().finish(atcmd::Reply::kError);
        cancel();
        return;
    }
    dtrOn_ = session_->dtrOn();
    if (p.get(modemcfg::kM) == 0) {
        connect();
        return;
    }
    if (stage_ == 3 && now - since_ >= 1500) {
        connect();
        return;
    }
    if (sound_.running()) {
        return;
    }
    if (stage_ == 1) {
        stage_ = 2;
        if (!sound_.start(calls::SoundStage::kDigits, "123", pulse_, 14400, p)) {
            session_->port().finish(atcmd::Reply::kError);
            cancel();
        }
    } else if (stage_ == 2) {
        stage_ = 3;
        since_ = now;
        if (!sound_.start(calls::SoundStage::kHandshake, "", false, 14400, p)) {
            session_->port().finish(atcmd::Reply::kError);
            cancel();
        }
    } else {
        connect();
    }
}
}  // namespace config
