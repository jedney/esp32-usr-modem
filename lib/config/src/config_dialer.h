#pragma once
#include "config.h"
#include "outgoing.h"
namespace config {
// Reserved number 123 is a local service, never a TCP destination.
class Dialer : public hostport::Session::Dialer {
public:
    Dialer(hostport::Session &console, Terminal &consoleMenu, Terminal &hostMenu,
           calls::Outgoing &outgoing, hostport::CallSlot &slot, calls::CallSound &sound,
           calls::Outgoing::Clock clock)
        : console_(console),
          consoleMenu_(consoleMenu),
          hostMenu_(hostMenu),
          outgoing_(outgoing),
          slot_(slot),
          sound_(sound),
          clock_(clock) {}
    // The USB modem port's session and menu.
    void setUsb(hostport::Session *usb, Terminal *usbMenu) {
        usb_ = usb;
        usbMenu_ = usbMenu;
    }
    bool start(hostport::Session &session, const char *text, bool pulse) override;
    void poll();
    void cancel();

private:
    static void cancelWait(void *self) { static_cast<Dialer *>(self)->cancel(); }
    void connect();
    hostport::Session &console_;
    Terminal &consoleMenu_, &hostMenu_;
    hostport::Session *usb_ = nullptr;
    Terminal *usbMenu_ = nullptr;
    calls::Outgoing &outgoing_;
    hostport::CallSlot &slot_;
    calls::CallSound &sound_;
    calls::Outgoing::Clock clock_;
    hostport::Session *session_ = nullptr;
    Terminal *menu_ = nullptr;
    unsigned stage_ = 0;
    bool pulse_ = false, dtrOn_ = false;
    uint32_t since_ = 0;
};
}  // namespace config
