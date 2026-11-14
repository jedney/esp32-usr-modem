#pragma once
#include <cstdint>

namespace webinterface {

// Browser upload hand-shake between the HTTP server task and the modem loop.
// No locking here: the web service calls every method under its own mutex, so each
// call is one atomic step. Each side only acts while the other is still waiting:
//
//   Server task                         Modem loop
//   ask(client)          -> asking
//                                       takeRequest()  (asking, not yet taken)
//                                       ... checks, hangs up the menu, claims the modem ...
//   waits up to 2 s for answered()      allow(yes)     -> active, or idle if no
//   giveUp() on time-out -> idle        allow() after giveUp() returns false and changes
//                                       nothing: the loop undoes its claim
//
//   LittleFS images only, while active:
//   askUnmount()                        takeUnmount()
//                                       ... stops audio, unmounts LittleFS ...
//   waits up to 2 s for unmountReady()  unmounted()    (false if the server gave up)
//   giveUpUnmount() on time-out         takeUnmount() after giveUpUnmount() is false, so a
//                                       late request can't unmount LittleFS again
//
//   finish(ok, fs)       -> done        takeDone(ok, fs) -> idle (remounts if fs and !ok)
//
// A late takeUnmount() result (taken just before the server gave up) still ends in
// finish(false, true), so the loop remounts LittleFS on its next pass.
class UploadHandshake {
public:
    // Server task.
    bool ask(uint32_t client);
    bool answered() const { return phase_ != Phase::kAsking; }
    bool granted() const { return phase_ == Phase::kActive; }
    bool giveUp();
    bool askUnmount();
    bool unmountReady() const { return unmount_ == Unmount::kDone; }
    void giveUpUnmount();
    void finish(bool ok, bool filesystem);

    // Modem loop.
    bool takeRequest(uint32_t &client);
    bool allow(bool yes);
    bool takeUnmount();
    bool unmounted();
    bool takeDone(bool &ok, bool &filesystem);

    // Both: an upload holds the modem from allow(true) until takeDone().
    bool uploading() const { return phase_ == Phase::kActive || phase_ == Phase::kDone; }
    bool idle() const { return phase_ == Phase::kIdle; }

private:
    enum class Phase : uint8_t { kIdle, kAsking, kActive, kDone };
    enum class Unmount : uint8_t { kNone, kAsking, kTaken, kDone };
    Phase phase_ = Phase::kIdle;
    Unmount unmount_ = Unmount::kNone;
    bool taken_ = false, ok_ = false, filesystem_ = false;
    uint32_t client_ = 0;
};

}  // namespace webinterface
