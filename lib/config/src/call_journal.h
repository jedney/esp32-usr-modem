#pragma once
#include "diagnostics.h"
#include "outgoing.h"
namespace config {
class CallJournal : public calls::Observer {
public:
    using Clock = uint32_t (*)();
    using Utc = int64_t (*)();
    CallJournal(History &history, Log &log, Clock clock, Utc utc)
        : history_(history), log_(log), clock_(clock), utc_(utc) {}
    void started(bool incoming, const calls::Entry &target) override;
    void connected(hostport::Session &session) override;
    void ended(atcmd::Result result) override;
    bool active() const { return active_; }
    const CallRecord &current() const { return record_; }
    uint32_t duration() const { return session_ ? (clock_() - connectedAt_) / 1000 : 0; }
    hostport::Session *session() const { return session_; }
    int64_t connectedUtc() const { return connectedUtc_; }
    uint16_t port() const { return port_; }

private:
    History &history_;
    Log &log_;
    Clock clock_;
    Utc utc_;
    CallRecord record_;
    hostport::Session *session_ = nullptr;
    uint32_t connectedAt_ = 0;
    int64_t connectedUtc_ = 0;
    uint16_t port_ = 23;
    bool active_ = false;
};
}  // namespace config
