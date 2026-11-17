#include "incoming.h"
#include <cctype>
#include <cstring>

namespace calls {
namespace {
bool word(const char *text, const char *expected) {
    while (*expected && std::toupper(static_cast<unsigned char>(*text)) == *expected) {
        ++text;
        ++expected;
    }
    while (*text == ' ') {
        ++text;
    }
    return *text == 0 && *expected == 0;
}
}  // namespace

modemcfg::AnswerPort pickAnswerPort(modemcfg::AnswerPort setting, bool rs232Dtr, bool usbDtr) {
    using modemcfg::AnswerPort;
    if (setting != AnswerPort::kAuto) {
        return setting;
    }
    if (rs232Dtr) {
        return AnswerPort::kRs232;
    }
    return usbDtr ? AnswerPort::kUsb : AnswerPort::kConsole;
}

hostport::Session &Incoming::autoAnswerTarget() {
    switch (pickAnswerPort(modem_.answerPort(), host_.dtrOn(), usb_ && usb_->dtrOn())) {
        case modemcfg::AnswerPort::kConsole:
            return console_;
        case modemcfg::AnswerPort::kUsb:
            return usb_ ? *usb_ : host_;
        default:
            return host_;
    }
}

bool Incoming::eligible(hostport::Session &session) const {
    return session.mode() == hostport::Mode::kCommand && session.port().lineEmpty() &&
           (modem_.active().get(modemcfg::kAmpD) == 0 || session.dtrOn());
}
void Incoming::ringResult(hostport::Session &session) {
    if (session.mode() == hostport::Mode::kCommand && session.port().lineEmpty() &&
        modem_.active().get(modemcfg::kQ) != 2) {
        session.port().unsolicited(atcmd::Result::kRing);  // dropped if nobody reads
    }
}
void Incoming::ring(uint32_t now) {
    ringAt_ = now;
    if (rings_ < kMaxRings) {
        ++rings_;
    }
    // The Sportster documents S1 counting when S0 is enabled. Manual rings are
    // counted internally too, so changing S0 during a pending call is deterministic.
    modem_.active().s[1] = modem_.active().s[0] ? rings_ : 0;
    ringResult(console_);
    ringResult(host_);
    if (usb_) {
        ringResult(*usb_);
    }
    hostport::Session &target = autoAnswerTarget();
    // S0 above kMaxRings answers on the last ring: the call is dropped after it.
    const uint8_t s0 = modem_.active().s[0];
    if (s0 && rings_ >= (s0 < kMaxRings ? s0 : kMaxRings) && eligible(target)) {
        answer(target);
    }
}
bool Incoming::ri() const {
    uint32_t elapsed = (clock_() - ringAt_) % kRingPeriodMs;
    return ringing() && (elapsed < 400 || (elapsed >= 600 && elapsed < 1000));
}
void Incoming::cancelWait(void *self) {
    static_cast<Incoming *>(self)->clear(false);
}
void Incoming::clear(bool notify) {
    if (stage_ == IncomingStage::kIdle) {
        return;
    }
    if (observer_) {
        observer_->ended(atcmd::Result::kNoCarrier);
    }
    hostport::Session *session = session_;
    const bool online = stage_ == IncomingStage::kOnline;
    const bool hadCarrier = session && session->callUp();
    stage_ = IncomingStage::kIdle;
    session_ = nullptr;
    sound_.stop();
    connection_.close();
    slot_.release(this);
    rings_ = 0;
    modem_.active().s[1] = 0;
    if (session) {
        session->port().finish(atcmd::Reply::kDone);
        if (online && session->callUp()) {
            session->hangUp();
        }
        if (notify && (!online || hadCarrier) && modem_.active().get(modemcfg::kQ) != 2) {
            session->port().result(atcmd::Result::kNoCarrier);
        }
    }
}
void Incoming::stop() {
    clear(true);
    listener_.stop();
    listening_ = false;
    retried_ = false;
}
void Incoming::cancel(hostport::Session &session) {
    if (ringing() || session_ == &session) {
        clear(false);
    }
}
bool Incoming::answer(hostport::Session &session) {
    if (!ringing() || !eligible(session)) {
        return false;
    }
    session_ = &session;
    sound_.stop();
    const modemcfg::Profile &p = modem_.active();
    unsigned n = p.get(modemcfg::kAmpN);
    lineSpeed_ = n ? atcmd::lineSpeedForN(n) : modemcfg::kMaxLineSpeed;
    reportSpeed_ = p.lineExplicit ? lineSpeed_ : modem_.model();
    stage_ = IncomingStage::kHandshake;
    session.port().wait(cancelWait, this, atcmd::Result::kNoCarrier, true);
    if (p.get(modemcfg::kAmpU) && lineSpeed_ < atcmd::lineSpeedForN(p.get(modemcfg::kAmpU))) {
        clear(true);
    } else if (p.get(modemcfg::kM) == 0 || p.get(modemcfg::kM) == 3) {
        connected();
    } else if (!sound_.start(SoundStage::kHandshake, "", false, lineSpeed_, p)) {
        clear(true);
    }
    return true;
}
void Incoming::connected() {
    session_->port().finish(atcmd::Reply::kDone);
    if (!session_->startCall(connection_, lineSpeed_, reportSpeed_, this, true)) {
        clear(true);
        return;
    }
    stage_ = IncomingStage::kOnline;
    if (observer_) {
        observer_->connected(*session_);
    }
    sound_.stop();
    if (modem_.active().get(modemcfg::kM) == 2) {
        sound_.start(SoundStage::kCarrier, "", false, lineSpeed_, modem_.active());
    }
}
void Incoming::poll(bool available) {
    const uint32_t now = clock_();
    if (!network_() || !available || modem_.commandsStopped()) {
        stop();
        return;
    }
    if (!listening_ && (!retried_ || now - retryAt_ >= 5000)) {
        retryAt_ = now;
        retried_ = true;
        listening_ = listener_.begin(23);
    }
    if (stage_ == IncomingStage::kIdle) {
        if (!listening_) {
            return;
        }
        if (slot_.busy()) {
            listener_.reject();
        } else if (listener_.accept()) {
            if (!slot_.claim(this)) {
                socket_.close();
                return;
            }
            connection_.begin(Mode::Telnet);
            if (observer_) {
                Entry peer;
                strncpy(peer.host, socket_.peer(), sizeof peer.host - 1);
                observer_->started(true, peer);
            }
            stage_ = IncomingStage::kRinging;
            rings_ = 0;
            ring(now);
            if (ringing() && modem_.active().get(modemcfg::kM) != 0) {
                // Audio failure must not prevent ATA or make the ring clock stall.
                sound_.start(SoundStage::kRing, "", false, 0, modem_.active());
            }
        }
    } else {
        if (listening_) {
            listener_.reject();
        }
        sound_.poll(now, modem_.active());
        if (stage_ == IncomingStage::kOnline) {
            if (!session_->callUp()) {
                clear(false);
            } else if (socket_.state() != SocketState::kOpen) {
                clear(true);
            }
            return;  // local Session polls this connection, including while escaped
        }
        connection_.poll(now);
        if (socket_.state() != SocketState::kOpen) {
            clear(true);
        } else if (ringing()) {
            if (modem_.active().get(modemcfg::kM) == 0) {
                sound_.stop();
            }
            if (now - ringAt_ >= kRingPeriodMs) {
                if (rings_ >= kMaxRings) {
                    clear(true);  // nobody answered. End it like a caller who hung up.
                } else {
                    ring(now);  // no catch-up burst after a delayed poll
                }
            }
        } else if (session_ && modem_.active().get(modemcfg::kAmpD) != 0 && !session_->dtrOn()) {
            clear(true);
        } else if (sound_.failed()) {
            clear(true);
        } else if (!sound_.running()) {
            connected();
        }
    }
}
atcmd::Reply Incoming::command(atcmd::Port &port, const char *args) {
    while (*args == ' ') {
        ++args;
    }
    if (word(args, "=?")) {
        port.beginInfo();
        port.line("RS232,USB,CONSOLE,AUTO");
    } else if (word(args, "?") || *args == 0) {
        port.beginInfo();
        port.line(modemcfg::answerPortName(modem_.answerPort()));
    } else {
        if (*args++ != '=' || slot_.busy()) {
            return atcmd::Reply::kError;
        }
        modemcfg::AnswerPort choice;
        if (!modemcfg::parseAnswerPort(args, &choice) || !modem_.setAnswerPort(choice)) {
            return atcmd::Reply::kError;
        }
    }
    return atcmd::Reply::kOk;
}
}  // namespace calls
