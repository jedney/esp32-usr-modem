// One serial port's session: see session.h.
#include "session.h"

namespace hostport {

using modemcfg::Profile;

bool CallSlot::claim(const void *who) {
    if (owner_ != nullptr && owner_ != who) {
        return false;
    }
    owner_ = who;
    return true;
}

void CallSlot::release(const void *who) {
    if (owner_ == who) {
        owner_ = nullptr;
    }
}

Session::Session(atcmd::Modem &modem, atcmd::Sink &out, uint32_t baud, CallSlot &slot)
    : modem_(modem), port_(modem, out, baud), slot_(slot) {
    port_.setLink(this);
}

bool Session::startCall(Connection &conn) {
    if (conn_ != nullptr || !slot_.claim(this)) {
        return false;
    }
    conn_ = &conn;
    sendConnect();
    mode_ = Mode::kOnline;
    esc_.reset(now_);
    xoff_ = false;
    echoLen_ = 0;
    return true;
}

void Session::sendConnect() {
    const unsigned n = profile().get(modemcfg::kAmpN);
    const uint32_t bps = n == 0 ? modemcfg::kMaxLineSpeed : atcmd::lineSpeedForN(n);
    port_.result(atcmd::connectFor(bps));
}

void Session::resumeOnline() {
    sendConnect();
    mode_ = Mode::kOnline;
    esc_.reset(now_);
}

void Session::hangUp() {
    endCall(false);  // ATH0 / ATZ: the parser sends OK
}

void Session::endCall(bool noCarrier) {
    if (conn_ != nullptr) {
        conn_->close();
        conn_ = nullptr;
    }
    slot_.release(this);
    mode_ = Mode::kCommand;
    xoff_ = false;
    echoLen_ = 0;
    if (noCarrier) {
        port_.result(atcmd::Result::kNoCarrier);
    }
}

size_t Session::fromHost(const uint8_t *data, size_t len, uint32_t nowMs) {
    now_ = nowMs;
    size_t used = 0;
    while (used < len) {
        if (mode_ != Mode::kOnline) {
            port_.feed(static_cast<char>(data[used++]));  // may start or resume a call
            continue;
        }
        const Profile &p = profile();
        const uint8_t b = data[used];
        const FlowMode f = flowMode(p);
        if (f.obeyXonXoff && (b == p.s[22] || b == p.s[23])) {
            xoff_ = b == p.s[23];
            if (f.stripXonXoff) {
                ++used;
                continue;
            }
        }
        const bool localEcho = p.get(modemcfg::kF) == 0;
        if (conn_->space() == 0 || (localEcho && echoLen_ >= sizeof echo_)) {
            break;  // the call is full: leave the rest with the caller
        }
        conn_->write(&b, 1);
        if (localEcho) {
            echo_[echoLen_++] = b;
        }
        esc_.onByte(b, nowMs, p);
        ++used;
    }
    return used;
}

size_t Session::hostSpace() const {
    if (mode_ != Mode::kOnline) {
        return 256;  // the parser takes everything
    }
    size_t n = conn_->space();
    if (profile().get(modemcfg::kF) == 0) {
        size_t echoSpace = sizeof echo_ - echoLen_;
        n = n < echoSpace ? n : echoSpace;
    }
    return n;
}

size_t Session::toHost(uint8_t *buf, size_t cap) {
    if (mode_ != Mode::kOnline || xoff_ || conn_ == nullptr) {
        return 0;
    }
    size_t n = echoLen_ < cap ? echoLen_ : cap;
    for (size_t i = 0; i < n; ++i) {
        buf[i] = echo_[i];
    }
    for (size_t i = n; i < echoLen_; ++i) {
        echo_[i - n] = echo_[i];
    }
    echoLen_ -= n;
    return n + conn_->read(buf + n, cap - n);
}

void Session::tick(uint32_t nowMs, bool dtr) {
    now_ = nowMs;
    const Profile &p = profile();
    if (!dtrKnown_) {
        dtr_.reset(dtr);
        dtrKnown_ = true;
    }
    if (dtr_.update(dtr, nowMs, p)) {  // DTR went off
        const DtrAction a = dtrAction(p);
        if (a != DtrAction::kNone && callUp()) {
            endCall(true);
        }
        if (a == DtrAction::kReset) {
            modem_.reset(0);
        }
    }
    if (conn_ != nullptr) {
        conn_->poll(nowMs);
        if (!conn_->isOpen()) {
            endCall(true);  // the far end hung up
        }
    }
    if (mode_ == Mode::kOnline && esc_.poll(nowMs, p)) {
        mode_ = Mode::kOnlineCommand;
        port_.result(atcmd::Result::kOk);
    }
}

bool Session::dcd() const {
    return dcdOn(profile(), callUp());
}

}  // namespace hostport
