// Local AT* commands. Remote callers must never be attached to an atcmd::Port.
#pragma once

#include "atcmd.h"
#include "devicecfg.h"

namespace devicecfg {

constexpr uint32_t kConfirmationMs = 30000;

class Control {
public:
    virtual ~Control() = default;
    virtual uint32_t now() = 0;
    virtual bool idle() = 0;
    virtual bool wifiUp() = 0;
    virtual void restart() = 0;  // schedule reboot after the response has drained
};

class Commands {
public:
    Commands(Device &device, Control &control, modemcfg::Store *const *resetStores, size_t count)
        : device_(device), control_(control), stores_(resetStores), count_(count) {}
    atcmd::Reply pass(atcmd::Port &port, const char *args);
    atcmd::Reply ota(atcmd::Port &port, const char *args);
    atcmd::Reply factory(atcmd::Port &port, const char *args);
    void poll();

private:
    static atcmd::Reply confirm(atcmd::Port &port, const char *text, void *ctx);
    Device &device_;
    Control &control_;
    modemcfg::Store *const *stores_;
    size_t count_;
    atcmd::Port *confirmation_ = nullptr;
    uint32_t askedAt_ = 0;
};

void attachCommands(Commands &commands);
atcmd::Reply cmdPass(atcmd::Port &port, const char *args);
atcmd::Reply cmdOta(atcmd::Port &port, const char *args);
atcmd::Reply cmdFactory(atcmd::Port &port, const char *args);

}  // namespace devicecfg

// clang-format off
#define DEVICECFG_STAR_COMMANDS \
    {"PASS", "AT*PASS[=<text>]", "replace device password (8-32 printable characters)", \
     devicecfg::cmdPass, false, true}, \
    {"OTA", "AT*OTA[?] | AT*OTA=<ALWAYS|DEMAND|ON|OFF>", "OTA policy; ON opens 10 minutes", \
     devicecfg::cmdOta, false}, \
    {"FACTORY", "AT*FACTORY", "erase all settings and restart (asks Y/N)", \
     devicecfg::cmdFactory, false, true}
// clang-format on
