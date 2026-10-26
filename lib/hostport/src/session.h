// One serial port's session (project brief 6.2): the RS232 port and the USB console each
// have one. It owns the port's AT parser and moves between three modes:
//   command        -> bytes from the computer go to the AT parser
//   online         -> bytes go to the call; "+++" with guard time escapes
//   online-command -> the call stays up; ATO goes back to it, ATH0 hangs up
// DTR (&D), DCD (&C) and XON/XOFF (&I) are handled here; hardware flow control is the
// board's UART (see linectl.h FlowMode).
//
// Only one port can have a call at a time (CallSlot). No Arduino dependencies.

#pragma once

#include <cstddef>
#include <cstdint>

#include "atcmd.h"
#include "connection.h"
#include "linectl.h"

namespace hostport {

enum class Mode : uint8_t { kCommand, kOnline, kOnlineCommand };

// Shared by all sessions: who has the call.
class CallSlot {
public:
    bool busy() const { return owner_ != nullptr; }
    bool claim(const void *who);
    bool transfer(const void *from, const void *to);
    void release(const void *who);

private:
    const void *owner_ = nullptr;
};

class Session : public atcmd::Link {
public:
    class Answerer {
    public:
        virtual ~Answerer() = default;
        virtual bool answer(Session &session) = 0;
        virtual void cancel(Session &session) = 0;
    };
    class Dialer {
    public:
        virtual ~Dialer() = default;
        virtual bool start(Session &session, const char *text, bool pulse) = 0;
    };
    class BaudControl {
    public:
        virtual ~BaudControl() = default;
        virtual bool online(uint32_t speed) = 0;
        virtual void command() = 0;
    };
    Session(atcmd::Modem &modem, atcmd::Sink &out, uint32_t baud, CallSlot &slot);
    Session(const Session &) = delete;
    Session &operator=(const Session &) = delete;

    atcmd::Port &port() { return port_; }
    Mode mode() const { return mode_; }
    bool localOpen() const { return local_ != nullptr; }
    uint32_t payloadSent() const { return payloadSent_; }
    uint32_t payloadReceived() const { return payloadReceived_; }
    bool startLocal(Connection &connection);

    // Starts a call on `conn` (AT*TESTCALL now, ATD from v0.8): sends CONNECT and goes
    // online. False if this or another port already has a call.
    bool startCall(Connection &conn, uint32_t operationalSpeed = 0, uint32_t reportSpeed = 0,
                   const void *reservation = nullptr, bool answered = false);
    void setAnswerer(Answerer *answerer) { answerer_ = answerer; }
    bool answer() override;
    bool answering() const override { return answered_; }
    void setDialer(Dialer *dialer) { dialer_ = dialer; }
    void setBaudControl(BaudControl *control) { baudControl_ = control; }
    bool dial(const char *text, bool pulse) override;
    uint32_t lineSpeed() const { return lineSpeed_; }
    uint32_t reportedSpeed() const { return reportSpeed_; }

    // Bytes from the computer. Returns how many were taken: online, fewer than `len`
    // when the call can't take more (the caller keeps the rest, so flow control holds
    // the computer).
    size_t fromHost(const uint8_t *data, size_t len, uint32_t nowMs);
    size_t hostSpace() const;  // how many bytes fromHost() would take now

    // Bytes for the computer from the call (online only, and not after XOFF).
    size_t toHost(uint8_t *buf, size_t cap);

    // Call often (every loop): guard time, DTR, the far end hanging up. `dtr` is the
    // raw DTR input (true = asserted); pass true for a port without DTR.
    void tick(uint32_t nowMs, bool dtr);

    bool dcd() const;  // DCD output, per &C
    bool dtrOn() const { return dtr_.on(); }
    bool heldByXoff() const { return xoff_; }

    // atcmd::Link
    bool callUp() const override { return conn_ != nullptr; }
    void resumeOnline() override;
    void hangUp() override;

private:
    void endCall(bool noCarrier);
    void sendConnect();
    size_t allowance(unsigned direction) const;
    void consume(unsigned direction, size_t bytes);
    const modemcfg::Profile &profile() const { return modem_.active(); }

    atcmd::Modem &modem_;
    atcmd::Port port_;
    CallSlot &slot_;
    Connection *conn_ = nullptr;
    Connection *local_ = nullptr;
    Dialer *dialer_ = nullptr;
    Answerer *answerer_ = nullptr;
    bool answered_ = false;
    BaudControl *baudControl_ = nullptr;
    uint32_t lineSpeed_ = 14400;
    uint32_t reportSpeed_ = 14400;
    bool throttle_ = false;
    uint64_t credit_[2] = {};
    uint32_t creditAt_ = 0;
    Mode mode_ = Mode::kCommand;
    EscapeDetector esc_;
    DtrFilter dtr_;
    bool dtrKnown_ = false;
    bool xoff_ = false;
    uint32_t now_ = 0;
    uint32_t payloadSent_ = 0, payloadReceived_ = 0;
    uint8_t echo_[256] = {};  // F0: online data echoed back to the computer
    size_t echoLen_ = 0;
};

}  // namespace hostport
