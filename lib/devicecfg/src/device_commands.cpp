#include "device_commands.h"

#include <cctype>
#include <cstring>

namespace devicecfg {
namespace {
Commands *attached = nullptr;
bool word(const char *text, const char *expected) {
    while (*text != '\0' && *expected != '\0') {
        if (toupper(static_cast<unsigned char>(*text++)) != *expected++) {
            return false;
        }
    }
    return *text == '\0' && *expected == '\0';
}
}  // namespace

atcmd::Reply Commands::pass(atcmd::Port &port, const char *args) {
    if (confirmation_ != nullptr || device_.needsReset()) {
        return atcmd::Reply::kError;
    }
    if (*args == '=') {
        if (!device_.change(args + 1)) {
            return atcmd::Reply::kError;
        }
        port.beginInfo();
        port.line("Device password changed.");
        return atcmd::Reply::kOk;
    }
    if (*args != '\0') {
        return atcmd::Reply::kError;
    }
    if (port.browser()) {
        port.line("Enter a new password with AT*PASS=<text>; browser passwords are write-only.");
        return atcmd::Reply::kError;
    }
    char password[kGeneratedLength + 1] = {};
    bool ok = device_.generate(password) && device_.change(password);
    if (ok) {
        port.beginInfo();
        // Deliberate one-time disclosure to the requesting local port only. Avoid
        // printf's scratch buffer, which would hold another plaintext copy.
        port.print("Device password: ");
        port.line(password);
    }
    wipe(password, sizeof password);
    return ok ? atcmd::Reply::kOk : atcmd::Reply::kError;
}

atcmd::Reply Commands::ota(atcmd::Port &port, const char *args) {
    if (*args == '\0' || strcmp(args, "?") == 0) {
        port.beginInfo();
        port.printf("OTA: %s, %s", device_.policy() == Policy::kAlways ? "ALWAYS" : "DEMAND",
                    device_.wantsListener(control_.now(), control_.wifiUp(), control_.idle())
                        ? "listening"
                        : "closed");
        if (!device_.ready()) {
            port.print(" (credentials unavailable; use AT*PASS or AT*FACTORY)");
        } else if (!control_.wifiUp()) {
            port.print(" (Wi-Fi disconnected)");
        } else if (!control_.idle()) {
            port.print(" (modem busy)");
        }
        port.newline();
        if (device_.policy() == Policy::kDemand) {
            port.printf("Window: %u seconds",
                        static_cast<unsigned>((device_.remainingMs(control_.now()) + 999) / 1000));
            port.newline();
        }
        return atcmd::Reply::kOk;
    }
    if (*args++ != '=' || confirmation_ != nullptr) {
        return atcmd::Reply::kError;
    }
    bool ok = false;
    if (word(args, "ALWAYS")) {
        ok = device_.setPolicy(Policy::kAlways);
    } else if (word(args, "DEMAND")) {
        ok = device_.setPolicy(Policy::kDemand);
    } else if (word(args, "ON")) {
        ok = device_.openWindow(control_.now());
    } else if (word(args, "OFF") && device_.ready() && device_.policy() == Policy::kDemand) {
        device_.closeWindow();
        ok = true;
    }
    return ok ? atcmd::Reply::kOk : atcmd::Reply::kError;
}

atcmd::Reply Commands::factory(atcmd::Port &port, const char *args) {
    if (*args != '\0' || confirmation_ != nullptr || !control_.idle()) {
        return atcmd::Reply::kError;
    }
    confirmation_ = &port;
    askedAt_ = control_.now();
    port.beginInfo();
    port.askConfirmation("Erase ALL settings, Wi-Fi and device password? Y/N (Enter): ", confirm,
                         this);
    return atcmd::Reply::kPending;
}

atcmd::Reply Commands::confirm(atcmd::Port &port, const char *text, void *ctx) {
    Commands &self = *static_cast<Commands *>(ctx);
    self.confirmation_ = nullptr;
    if (!word(text, "Y") || !self.control_.idle() ||
        self.control_.now() - self.askedAt_ >= kConfirmationMs) {
        port.line("Factory reset cancelled.");
        return atcmd::Reply::kError;
    }
    if (!self.device_.factoryReset(self.stores_, self.count_)) {
        port.line("Factory reset failed. Retry AT*FACTORY; a pending reset resumes at boot.");
        return atcmd::Reply::kError;
    }
    port.line("Factory settings erased. Restarting.");
    port.modem().stopCommands();
    self.control_.restart();
    return atcmd::Reply::kOk;
}

void Commands::poll() {
    device_.tick(control_.now());
    if (confirmation_ != nullptr) {
        if (!confirmation_->busy()) {
            confirmation_ = nullptr;
        } else if (control_.now() - askedAt_ >= kConfirmationMs) {
            confirmation_->cancelPrompt();
            confirmation_ = nullptr;
        }
    }
}

void attachCommands(Commands &commands) {
    attached = &commands;
}
atcmd::Reply cmdPass(atcmd::Port &port, const char *args) {
    return attached != nullptr ? attached->pass(port, args) : atcmd::Reply::kError;
}
atcmd::Reply cmdOta(atcmd::Port &port, const char *args) {
    return attached != nullptr ? attached->ota(port, args) : atcmd::Reply::kError;
}
atcmd::Reply cmdFactory(atcmd::Port &port, const char *args) {
    return attached != nullptr ? attached->factory(port, args) : atcmd::Reply::kError;
}

}  // namespace devicecfg
