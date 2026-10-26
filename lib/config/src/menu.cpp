#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "config.h"

namespace config {
namespace {
using S = Screen;
const Action kActions[] = {
    {S::Main, "Status", "*STATUS", "Call, front panel, network and host serial", S::Main},
    {S::Main, "Phonebook", "", "List, add, edit, delete and dial", S::Phonebook},
    {S::Main, "Settings", "", "Settings and help", S::Settings},
    {S::Main, "Command reference", "*REFERENCE", "Standard and project commands", S::Main},
    {S::Main, "About", "*ABOUT", "Version, build and credits", S::Main},
    {S::Main, "Sounds", "", "Play generated tones and recorded handshakes", S::Sounds},
    {S::Main, "Debug", "", "Verbosity, logs and reset diagnostics", S::Debug},
    {S::Main, "Call history", "*CALLLOG", "Last 50 network calls; retained across restart",
     S::Main},
    {S::Main, "Wi-Fi and clock", "", "Network tools and time", S::Wifi},
    {S::Main, "Updates and reset", "", "OTA, password and confirmed reset", S::Updates},
    {S::Main, "Export configuration", "*EXPORT", "Restore commands; no passwords", S::Main},
    {S::Main, "Hardware tests", "", "Local electronics tests", S::Hardware},
    {S::Main, "Client Tests (v0.13)", "", "Unavailable until v0.13", S::Main},
    {S::Settings, "Current and stored profiles", "*SETTING?", "Active and stored settings",
     S::Settings},
    {S::Settings, "Edit profile / registers", "", "All supported modem fields", S::Profile},
    {S::Settings, "Model", "*MODEL=", "14400|28800|33600|56000; saved immediately", S::Settings,
     true},
    {S::Settings, "Auto-answer rings (S0)",
     "*SETTING=S0=", "0 disables auto-answer; 1-255 rings; &W saves", S::Settings, true},
    {S::Settings, "Auto-answer destination", "*ANSWERPORT=", "RS232|CONSOLE; saved immediately",
     S::Settings, true},
    {S::Settings, "Host baud", "*BAUD=", "AUTO|300-115200; saved immediately; console fixed 115200",
     S::Settings, true, false, false, true},
    {S::Settings, "Data throttle", "*THROTTLE=", "ON|OFF; saved immediately", S::Settings, true},
    {S::Settings, "Save active profile", "*SETTING=&W",
     "0|1: save active modem settings to template", S::Settings, true},
    {S::Settings, "Load stored/factory profile", "*SETTING=Z",
     "0-5: USR reset selection; leaves menu", S::Settings, true, false, false, true},
    {S::Settings, "Load factory profile", "*SETTING=&F", "0|1|2: load active profile; &W saves",
     S::Settings, true},
    {S::Settings, "Power-on profile", "*SETTING=Y", "0|1 saved; 2-4 factory; saved immediately",
     S::Settings, true},
    {S::Settings, "Stored number", "*SETTING=&Z", "slot=number; slots 0-3, saved immediately",
     S::Settings, true},
    {S::Settings, "Terminal style", "*STYLE=", "ANSI|CP437|PLAIN; this local session only",
     S::Settings, true},
    {S::Phonebook, "List entries", "*PHONEBOOK?",
     "64 entries; number 1 is Amiga Retro BBS by default", S::Phonebook},
    {S::Phonebook, "Add / edit entry",
     "*PHONEBOOK=", "number,host,port,TELNET|RAW[,speed]; saved immediately", S::Phonebook, true},
    {S::Phonebook, "Delete entry", "*PHONEDELETE=", "number; saved immediately", S::Phonebook,
     true},
    {S::Phonebook, "Dial", "*DIAL=", "phonebook number or host:port; leaves menu", S::Phonebook,
     true, false, false, true},
    {S::Wifi, "Network status", "*WIFI?", "SSID, IP, signal and password set/not set", S::Wifi},
    {S::Wifi, "Scan", "*WIFISCAN", "Scan networks; any key cancels", S::Wifi},
    {S::Wifi, "Join", "*WIFIJOIN=", "scan number or quoted SSID; then masked password prompt",
     S::Wifi, true},
    {S::Wifi, "Wi-Fi on / off", "*WIFI=", "ON|OFF; saved immediately", S::Wifi, true},
    {S::Wifi, "Forget network", "*WIFIFORGET", "Forget SSID and password", S::Wifi},
    {S::Wifi, "IP configuration", "*WIFIIP=", "DHCP|ip,mask,gateway[,dns]; saved immediately",
     S::Wifi, true},
    {S::Wifi, "Network name", "*HOSTNAME=", "mDNS name; saved immediately", S::Wifi, true},
    {S::Wifi, "Time", "*TIME", "NTP clock; unavailable until synchronized", S::Wifi},
    {S::Wifi, "Time zone", "*TZ=", "zone or POSIX rule; saved immediately; =? lists zones", S::Wifi,
     true},
    {S::Wifi, "NTP server", "*NTP=", "server name; saved immediately", S::Wifi, true},
    {S::Wifi, "DNS lookup", "*DNS=", "host; bounded cancellable lookup", S::Wifi, true},
    {S::Wifi, "Ping", "*PING=", "host; bounded cancellable ICMP test", S::Wifi, true},
    {S::Updates, "Firmware version", "*VER", "Version and build", S::Updates},
    {S::Updates, "OTA status", "*OTA?", "Listener policy and window", S::Updates},
    {S::Updates, "OTA policy / window",
     "*OTA=", "ALWAYS|DEMAND|ON|OFF; leave menu to permit upload", S::Updates, true},
    {S::Updates, "Change device password", "*PASS=", "8-32 printable characters; saved immediately",
     S::Updates, true, true},
    {S::Updates, "Restart", "*RESTART", "Requires confirmation; leaves menu", S::Updates, false,
     false, false, true},
    {S::Updates, "Factory reset", "*FACTORY",
     "Erase ALL settings and passwords; asks Y/N; leaves menu", S::Updates, false, false, false,
     true},
    {S::Updates, "Web interface", "*WEB=", "ON|OFF; OFF closes browser sessions", S::Updates, true},
    {S::Sounds, "List sounds", "*SOUNDS", "Generated tones and LittleFS recordings", S::Sounds},
    {S::Sounds, "Play", "*PLAY=", "DIAL|BUSY|RING|DTMF|PULSE|SWEEP|1200..56000; key cancels",
     S::Sounds, true},
    {S::Debug, "View log", "*LOG", "Structured events; passwords and payload never recorded",
     S::Debug},
    {S::Debug, "Verbosity", "*LOG=", "0 off, 1 lifecycle, 2 detailed; RAM only", S::Debug, true},
    {S::Debug, "Clear debug log", "*LOG=CLEAR", "Clear RAM ring buffer", S::Debug},
    {S::Debug, "Reset / crash information", "*RESETINFO",
     "Persisted reset reason; available crash record", S::Debug},
    {S::Hardware, "Guided tests", "*HWTEST", "RS232 skips line/loopback steps", S::Hardware},
    {S::Hardware, "Pin map", "*HWPINS", "Confirmed pin map", S::Hardware},
    {S::Hardware, "Board information", "*HWINFO", "Chip, memory, firmware and filesystem",
     S::Hardware},
    {S::Hardware, "Files", "*HWFILES", "LittleFS files", S::Hardware},
    {S::Hardware, "LEDs", "*HWLED=", "AA|CD|RD|SD|TR|CS|ARQ|ALL|OFF|AUTO|CHASE|WALK", S::Hardware,
     true},
    {S::Hardware, "Amplifier", "*HWAMP=", "AUTO|ON|OFF", S::Hardware, true},
    {S::Hardware, "Test volume", "*HWVOL=", "0-100|POT; cap always applies", S::Hardware, true},
    {S::Hardware, "Tone / sweep", "*HWTONE=", "Hz[,ms]|DIAL|BUSY|RING|SWEEP", S::Hardware, true},
    {S::Hardware, "DTMF", "*HWDTMF=", "digits", S::Hardware, true},
    {S::Hardware, "Pulse", "*HWPULSE=", "digits", S::Hardware, true},
    {S::Hardware, "Potentiometer", "*HWPOT", "Live ADC reading; key exits", S::Hardware},
    {S::Hardware, "RS232 lines", "*HWLINES", "Unavailable on RS232: takes over this port",
     S::Hardware, false, false, true},
    {S::Hardware, "Loopback", "*HWLOOP=ALL", "Unavailable on RS232: takes over this port",
     S::Hardware, false, false, true},
    {S::Hardware, "Bridge", "*HWBRIDGE", "Unavailable on RS232: takes over this port", S::Hardware,
     false, false, true},
    {S::Hardware, "Output signal", "*HWSIG=", "DCD|RI|CTS,ON|OFF; console only while idle",
     S::Hardware, true, false, true},
};
constexpr size_t kPageItems = 12;
}  // namespace
const Action *actions(size_t &count) {
    count = sizeof kActions / sizeof kActions[0];
    return kActions;
}
const char *screenName(Screen screen) {
    static const char *const names[] = {"Configuration", "Settings",      "Profile",
                                        "Phonebook",     "Wi-Fi / clock", "Updates",
                                        "Sounds",        "Debug",         "Hardware tests"};
    return names[static_cast<unsigned>(screen)];
}
bool Terminal::open() {
    if (open_) {
        return false;
    }
    open_ = true;
    screen_ = S::Main;
    page_ = 0;
    capturing_ = response_ = more_ = overflow_ = skipLf_ = departure_ = false;
    inputLen_ = 0;
    inputOverflow_ = false;
    prompt_ = nullptr;
    render();
    return true;
}
void Terminal::clearOutput() {
    length_ = pos_ = pageEnd_ = 0;
    lines_ = column_ = 0;
    more_ = overflow_ = false;
}
void Terminal::append(const char *text) {
    write(text, strlen(text));
}
void Terminal::line(const char *text) {
    append(text);
    append("\r\n");
}
void Terminal::write(const char *data, size_t len) {
    // Treat command output as untrusted display data: keep CR/LF and printable
    // bytes, replace terminal control sequences. Renderers add their own ANSI.
    for (size_t i = 0; i < len; ++i) {
        if (length_ >= sizeof output_ - 96) {
            overflow_ = true;
            break;
        }
        unsigned char c = static_cast<unsigned char>(data[i]);
        if (c >= 128 && style_ != Style::Cp437) {
            c = '?';
        }
        if (c == '\r' || c == '\n') {
            column_ = 0;
        } else if (column_++ >= 76 && !exporting_) {
            output_[length_++] = '\r';
            output_[length_++] = '\n';
            column_ = 1;
        }
        output_[length_++] = (c == '\r' || c == '\n' || c >= 32) && c != 127 ? c : '?';
    }
    pageEnd_ = length_;
}
void Terminal::render() {
    inputOverflow_ = false;
    clearOutput();
    response_ = liveStatus_ = exporting_ = false;
    if (style_ != Style::Plain) {
        const char *ansi = "\x1b[0m\x1b[2J\x1b[H\x1b[1;36m";
        memcpy(output_, ansi, strlen(ansi));
        length_ = strlen(ansi);
    }
    line(style_ == Style::Cp437 ? "\xda\xc4\xc4 USR MODEM \xc4\xc4\xbf" : "+--- USR MODEM ---+");
    line(modemcfg::modelName(session_.port().modem().model()));
    line(screenName(screen_));
    if (style_ != Style::Plain) {
        const char *reset = "\x1b[0m";
        memcpy(output_ + length_, reset, strlen(reset));
        length_ += strlen(reset);
    }
    char row[180];
    size_t item = 0;
    if (screen_ == S::Profile) {
        for (unsigned i = 0; i < modemcfg::kSettingCount + modemcfg::kSRegCount; ++i) {
            if (item / kPageItems == page_) {
                if (i < modemcfg::kSettingCount) {
                    const auto &info = modemcfg::settingInfo(static_cast<modemcfg::Setting>(i));
                    snprintf(row, sizeof row, "%2u. %s = %u", unsigned(item % kPageItems + 1),
                             info.name, session_.port().modem().active().v[i]);
                } else {
                    unsigned reg = i - modemcfg::kSettingCount;
                    snprintf(row, sizeof row, "%2u. S%u = %u%s", unsigned(item % kPageItems + 1),
                             reg, session_.port().modem().active().s[reg],
                             reg == 1 ? " (read only)" : "");
                }
                line(row);
            }
            ++item;
        }
    } else {
        for (const auto &a : kActions) {
            if (a.screen != screen_) {
                continue;
            }
            if (item / kPageItems == page_) {
                snprintf(row, sizeof row, "%2u. %s%s", unsigned(item % kPageItems + 1), a.label,
                         rs232_ && a.rs232Unavailable ? " [unavailable: takes over RS232]" : "");
                line(row);
            }
            ++item;
        }
    }
    snprintf(row, sizeof row, "Page %u/%u. Number + Enter; N next, P previous, B back, Q quit.",
             unsigned(page_ + 1), unsigned((item + kPageItems - 1) / kPageItems));
    line(row);
    append("> ");
    pageEnd_ = length_;
}
void Terminal::close() {
    if (capturing_) {
        session_.port().cancelPrompt();
        session_.port().redirect(nullptr);
    }
    open_ = capturing_ = false;
    prompt_ = nullptr;
    devicecfg::wipe(input_, sizeof input_);
    inputLen_ = 0;
}
void Terminal::escaped() {
    if (capturing_) {
        session_.port().cancelPrompt();
        session_.port().redirect(nullptr);
        capturing_ = false;
    }
    devicecfg::wipe(input_, sizeof input_);
    inputLen_ = 0;
    prompt_ = nullptr;
    render();
}
bool Terminal::setStyle(const char *text) {
    if (!strcmp(text, "ANSI")) {
        style_ = Style::Ansi;
    } else if (!strcmp(text, "CP437")) {
        style_ = Style::Cp437;
    } else if (!strcmp(text, "PLAIN")) {
        style_ = Style::Plain;
    } else {
        return false;
    }
    return true;
}
bool Terminal::takeDeparture(char *command, size_t cap) {
    if (!departure_ || open_ || strlen(departureCommand_) >= cap) {
        return false;
    }
    strcpy(command, departureCommand_);
    devicecfg::wipe(departureCommand_, sizeof departureCommand_);
    departure_ = false;
    return true;
}
void Terminal::execute(const char *command) {
    clearOutput();
    response_ = capturing_ = true;
    exporting_ = !strcmp(command, "*EXPORT");
    liveStatus_ = !strcmp(command, "*STATUS");
    if (liveStatus_ && style_ != Style::Plain) {
        const char *ansi = "\x1b[2J\x1b[H";
        memcpy(output_, ansi, strlen(ansi));
        length_ = strlen(ansi);
    }
    session_.port().redirect(this, true);
    session_.port().execute(command);
    devicecfg::wipe(input_, sizeof input_);
    inputLen_ = 0;
    if (!session_.port().busy()) {
        finishResponse();
    }
}
void Terminal::finishResponse() {
    session_.port().redirect(nullptr);
    capturing_ = false;
    if (overflow_) {
        // Reserve was left for this explicit message.
        const char *warning = "\r\n[Output limit reached; use a smaller query.]\r\n";
        memcpy(output_ + length_, warning, strlen(warning));
        length_ += strlen(warning);
    }
    append(liveStatus_ ? "\r\nR refreshes; Enter returns to menu.\r\n"
                       : "\r\nEnter returns to menu.\r\n");
    pageEnd_ = length_;
}
void Terminal::poll(uint32_t now) {
    if (capturing_ && !session_.port().busy()) {
        finishResponse();
    }
    if (liveStatus_ && style_ != Style::Plain && !capturing_ && !more_ && pos_ == pageEnd_ &&
        now - refreshedAt_ >= 2000) {
        refreshedAt_ = now;
        execute("*STATUS");
    }
}
size_t Terminal::read(uint8_t *data, size_t cap) {
    size_t n = 0;
    while (!more_ && n < cap && pos_ < pageEnd_) {
        char c = output_[pos_++];
        data[n++] = static_cast<uint8_t>(c);
        if (c == '\n' && ++lines_ == 18 && pos_ < pageEnd_ && !capturing_) {
            more_ = true;
            break;
        }
    }
    if (more_ && pos_ < pageEnd_ && lines_ >= 18) {
        // The 18th line is a natural page break; append prompt from a separate
        // tiny cursor so even one-byte UART reads retain it.
        static const char prompt[] = "[More: Enter, Q returns]\r\n";
        size_t index = lines_ - 18;
        while (n < cap && index < sizeof prompt - 1) {
            data[n++] = prompt[index++];
        }
        lines_ = 18 + index;
    }
    return n;
}
size_t Terminal::write(const uint8_t *data, size_t len) {
    if (!open_ || !len) {
        return 0;
    }
    // One character at a time permits prompt/ownership changes between bytes.
    input(static_cast<char>(data[0]));
    return 1;
}
void Terminal::input(char c) {
    if (c == '\n' && skipLf_) {
        skipLf_ = false;
        return;
    }
    skipLf_ = c == '\r';
    if (c == 3) {
        close();
        return;
    }  // unconditional local menu exit, including prompts
    if (capturing_) {
        if (c == '\r' || c == '\n') {
            c = static_cast<char>(session_.port().modem().active().s[3]);
        } else if (c == '\b' || c == 127) {
            c = static_cast<char>(session_.port().modem().active().s[5]);
        }
        session_.port().feed(c);
        return;
    }
    if (more_) {
        if (c == '\r' || c == '\n') {
            more_ = false;
            lines_ = 0;
        } else if (c == 'q' || c == 'Q') {
            more_ = false;
            render();
        }
        return;
    }
    if (response_) {
        if (liveStatus_ && (c == 'r' || c == 'R')) {
            execute("*STATUS");
        } else if (c == '\r' || c == '\n') {
            render();
        }
        return;
    }
    if (c == 27 || c == 3) {
        prompt_ = nullptr;
        inputLen_ = 0;
        devicecfg::wipe(input_, sizeof input_);
        render();
        return;
    }
    if (c == '\b' || c == 127) {
        if (inputLen_) {
            input_[--inputLen_] = 0;
            if (length_ + 3 < sizeof output_) {
                memcpy(output_ + length_, "\b \b", 3);
                length_ += 3;
                pageEnd_ = length_;
            }
        }
        return;
    }
    if (c == '\r' || c == '\n') {
        select();
        return;
    }
    if (c < 32 || c > 126) {
        return;
    }
    if (inputLen_ >= sizeof input_ - 1) {
        inputOverflow_ = true;
        return;
    }
    input_[inputLen_++] = c;
    input_[inputLen_] = 0;
    // Do not buffer password text in output; only the mask enters the display.
    char echo[2] = {prompt_ && prompt_->secret ? '*' : c, 0};
    append(echo);
}
void Terminal::select() {
    if (inputOverflow_) {
        inputOverflow_ = false;
        prompt_ = nullptr;
        inputLen_ = 0;
        devicecfg::wipe(input_, sizeof input_);
        clearOutput();
        response_ = true;
        line("Input too long; action cancelled.");
        line("Enter returns to menu.");
        return;
    }
    if (prompt_) {
        char command[atcmd::kLineMax + 1];
        if (snprintf(command, sizeof command, "%s%s", prompt_->command, input_) >=
            int(sizeof command)) {
            devicecfg::wipe(input_, sizeof input_);
            inputLen_ = 0;
            prompt_ = nullptr;
            clearOutput();
            response_ = true;
            line("Value too long; action cancelled.");
            line("Enter returns to menu.");
            return;
        }
        bool leave = prompt_->leave;
        prompt_ = nullptr;
        if (leave) {
            strcpy(departureCommand_, command);
            departure_ = true;
            close();
        } else {
            execute(command);
        }
        devicecfg::wipe(command, sizeof command);
        return;
    }
    char c = static_cast<char>(toupper(static_cast<unsigned char>(input_[0])));
    char *end = nullptr;
    unsigned choice = static_cast<unsigned>(strtoul(input_, &end, 10));
    bool numeric = input_[0] && *end == 0 && choice >= 1 && choice <= kPageItems;
    devicecfg::wipe(input_, sizeof input_);
    inputLen_ = 0;
    size_t total = 0;
    if (screen_ == S::Profile) {
        total = modemcfg::kSettingCount + modemcfg::kSRegCount;
    } else {
        for (const auto &a : kActions) {
            if (a.screen == screen_) {
                ++total;
            }
        }
    }
    if (!numeric) {
        if (c == 'Q') {
            close();
            return;
        }
        if (c == 'B') {
            screen_ = S::Main;
            page_ = 0;
        }
        if (c == 'N' && (page_ + 1) * kPageItems < total) {
            ++page_;
        }
        if (c == 'P' && page_) {
            --page_;
        }
        render();
        return;
    }
    size_t selected = page_ * kPageItems + choice - 1;
    if (screen_ == S::Profile) {
        if (selected >= total) {
            render();
            return;
        }
        // Stable per-terminal storage for the generated setting action.
        static Action field;
        static char command[32], help[384];
        if (selected < modemcfg::kSettingCount) {
            auto setting = static_cast<modemcfg::Setting>(selected);
            const auto &info = modemcfg::settingInfo(setting);
            snprintf(command, sizeof command, "*SETTING=%s", info.name);
            char values[192] = {};
            size_t used = 0;
            for (unsigned v = 0; v < 64; ++v) {
                if (modemcfg::settingAllows(setting, v)) {
                    used +=
                        snprintf(values + used, sizeof values - used, "%s%u", used ? "," : "", v);
                }
            }
            snprintf(help, sizeof help, "%s %s Allowed: %s. Active only; &W saves.", info.name,
                     settingHelp(setting), values);
        } else {
            unsigned reg = selected - modemcfg::kSettingCount;
            if (reg == 1) {
                render();
                return;
            }
            snprintf(command, sizeof command, "*SETTING=S%u=", reg);
            snprintf(help, sizeof help, "S%u: %s Enter 0-255; active only, &W saves.", reg,
                     registerHelp(reg));
        }
        field = {S::Profile, "Profile field", command, help, S::Profile, true};
        prompt_ = &field;
    } else {
        size_t i = 0;
        for (const auto &a : kActions) {
            if (a.screen == screen_ && i++ == selected) {
                prompt_ = &a;
                break;
            }
        }
    }
    if (!prompt_) {
        render();
        return;
    }
    const Action &a = *prompt_;
    if (rs232_ && a.rs232Unavailable) {
        prompt_ = nullptr;
        render();
        return;
    }
    if (!*a.command) {
        screen_ = a.target;
        page_ = 0;
        prompt_ = nullptr;
        render();
        return;
    }
    if (a.input) {
        clearOutput();
        line(a.help);
        append("Value (Esc cancels): ");
        return;
    }
    prompt_ = nullptr;
    if (a.leave) {
        strcpy(departureCommand_, a.command);
        departure_ = true;
        close();
    } else {
        execute(a.command);
    }
}
}  // namespace config
