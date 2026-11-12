#include "upload_handshake.h"

namespace webinterface {

bool UploadHandshake::ask(uint32_t client) {
    if (phase_ != Phase::kIdle) {
        return false;
    }
    phase_ = Phase::kAsking;
    unmount_ = Unmount::kNone;
    taken_ = false;
    client_ = client;
    return true;
}
bool UploadHandshake::giveUp() {
    if (phase_ != Phase::kAsking) {
        return false;
    }
    phase_ = Phase::kIdle;
    return true;
}
bool UploadHandshake::askUnmount() {
    if (phase_ != Phase::kActive) {
        return false;
    }
    unmount_ = Unmount::kAsking;
    return true;
}
void UploadHandshake::giveUpUnmount() {
    if (unmount_ != Unmount::kDone) {
        unmount_ = Unmount::kNone;
    }
}
void UploadHandshake::finish(bool ok, bool filesystem) {
    if (phase_ != Phase::kActive) {
        return;
    }
    phase_ = Phase::kDone;
    unmount_ = Unmount::kNone;
    ok_ = ok;
    filesystem_ = filesystem;
}
bool UploadHandshake::takeRequest(uint32_t &client) {
    if (phase_ != Phase::kAsking || taken_) {
        return false;
    }
    taken_ = true;
    client = client_;
    return true;
}
bool UploadHandshake::allow(bool yes) {
    if (phase_ != Phase::kAsking || !taken_) {
        return false;  // the server gave up waiting: nothing changes
    }
    phase_ = yes ? Phase::kActive : Phase::kIdle;
    return yes;
}
bool UploadHandshake::takeUnmount() {
    if (phase_ != Phase::kActive || unmount_ != Unmount::kAsking) {
        return false;
    }
    unmount_ = Unmount::kTaken;
    return true;
}
bool UploadHandshake::unmounted() {
    if (phase_ != Phase::kActive || unmount_ != Unmount::kTaken) {
        return false;
    }
    unmount_ = Unmount::kDone;
    return true;
}
bool UploadHandshake::takeDone(bool &ok, bool &filesystem) {
    if (phase_ != Phase::kDone) {
        return false;
    }
    phase_ = Phase::kIdle;
    ok = ok_;
    filesystem = filesystem_;
    return true;
}

}  // namespace webinterface
