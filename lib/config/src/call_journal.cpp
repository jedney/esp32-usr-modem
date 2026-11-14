#include "call_journal.h"
#include <cstring>
namespace config {
void CallJournal::started(bool incoming, const calls::Entry &target) {
    record_ = CallRecord{};
    record_.utc = utc_();
    connectedUtc_ = 0;
    port_ = target.port;
    record_.incoming = incoming;
    record_.kind = target.raw ? calls::Kind::Raw : target.kind;
    memcpy(record_.host, target.host, sizeof record_.host);
    memcpy(record_.number, target.number, sizeof record_.number);
    session_ = nullptr;
    active_ = true;
    log_.event(clock_(), Event::Dial);
}
void CallJournal::connected(hostport::Session &session) {
    session_ = &session;
    connectedAt_ = clock_();
    connectedUtc_ = utc_();
    log_.event(clock_(), Event::Connected);
}
void CallJournal::ended(atcmd::Result result) {
    if (!active_) {
        return;
    }
    active_ = false;
    record_.result = static_cast<uint16_t>(result);
    record_.duration = duration();
    if (session_) {
        record_.sent = session_->payloadSent();
        record_.received = session_->payloadReceived();
    }
    log_.event(clock_(), Event::Ended);
    if (!history_.append(record_)) {
        log_.event(clock_(), Event::StorageError);
    }
    session_ = nullptr;
}
}  // namespace config
