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

bool CallSlot::transfer(const void *from, const void *to) {
    if (owner_ != from || to == nullptr) {
        return false;
    }
    owner_ = to;
    return true;
}

Session::Session(atcmd::Modem &modem, atcmd::Sink &out, uint32_t baud, CallSlot &slot)
    : modem_(modem), port_(modem, out, baud), slot_(slot) {
    port_.setLink(this);
}

bool Session::startCall(Connection &conn, uint32_t operationalSpeed, uint32_t reportSpeed,
                        const void *reservation, bool answered) {
    if (conn_ != nullptr ||
        !(reservation ? slot_.transfer(reservation, this) : slot_.claim(this))) {
        return false;
    }
    conn_ = &conn;
    answered_ = answered;
    const unsigned n = profile().get(modemcfg::kAmpN);
    lineSpeed_ = operationalSpeed ? operationalSpeed
                                  : (n == 0 ? modemcfg::kMaxLineSpeed : atcmd::lineSpeedForN(n));
    reportSpeed_ =
        reportSpeed ? reportSpeed : (profile().lineExplicit ? lineSpeed_ : modem_.model());
    throttle_ = modem_.throttle();
    credit_[0] = credit_[1] = 0;
    creditAt_ = now_;
    sendConnect();
    mode_ = Mode::kOnline;
    if (baudControl_ && !baudControl_->online(lineSpeed_)) {
        endCall(false);
        return false;
    }
    esc_.reset(now_);
    xoff_ = false;
    echoLen_ = 0;
    return true;
}

void Session::sendConnect() {
    if (answered_ && profile().get(modemcfg::kQ) == 2) {
        return;
    }
    char result[96];
    size_t n = atcmd::formatConnect(reportSpeed_, lineSpeed_, profile(), result, sizeof result);
    port_.write(result, n);
}

bool Session::dial(const char *text, bool pulse) {
    return dialer_ != nullptr && dialer_->start(*this, text, pulse);
}

size_t Session::allowance(unsigned direction) const {
    return throttle_ ? static_cast<size_t>(credit_[direction] / 10000) : 65536;
}

void Session::consume(unsigned direction, size_t bytes) {
    if (throttle_) {
        credit_[direction] -= static_cast<uint64_t>(bytes) * 10000;
    }
}

void Session::resumeOnline() {
    sendConnect();
    mode_ = Mode::kOnline;
    if (baudControl_ && !baudControl_->online(lineSpeed_)) {
        endCall(true);
        return;
    }
    esc_.reset(now_);
}

void Session::hangUp() {
    endCall(false);  // ATH0 / ATZ: the parser sends OK
    if (answerer_) {
        answerer_->cancel(*this);
    }
}

bool Session::answer() {
    return answerer_ != nullptr && answerer_->answer(*this);
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
    if (baudControl_) {
        baudControl_->command();
    }
    if (noCarrier) {
        port_.result(atcmd::Result::kNoCarrier);
    }
    answered_ = false;
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
        if (allowance(0) == 0 || conn_->space() == 0 || (localEcho && echoLen_ >= sizeof echo_)) {
            break;  // the call is full: leave the rest with the caller
        }
        conn_->write(&b, 1);
        consume(0, 1);
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
    if (n > allowance(0)) {
        n = allowance(0);
    }
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
    if (cap > allowance(1)) {
        cap = allowance(1);
    }
    size_t n = echoLen_ < cap ? echoLen_ : cap;
    for (size_t i = 0; i < n; ++i) {
        buf[i] = echo_[i];
    }
    for (size_t i = n; i < echoLen_; ++i) {
        echo_[i - n] = echo_[i];
    }
    echoLen_ -= n;
    n += conn_->read(buf + n, cap - n);
    consume(1, n);
    return n;
}

void Session::tick(uint32_t nowMs, bool dtr) {
    now_ = nowMs;
    if (throttle_) {
        uint32_t elapsed = nowMs - creditAt_;
        uint64_t limit = static_cast<uint64_t>(lineSpeed_) * 100;
        if (limit < 10000) {
            limit = 10000;
        }
        for (auto &credit : credit_) {
            credit += static_cast<uint64_t>(elapsed) * lineSpeed_;
            if (credit > limit) {
                credit = limit;
            }
        }
    }
    creditAt_ = nowMs;
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
        if (baudControl_) {
            baudControl_->command();
        }
        port_.result(atcmd::Result::kOk);
    }
}

bool Session::dcd() const {
    return dcdOn(profile(), callUp());
}

}  // namespace hostport
