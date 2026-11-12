#include "config.h"
namespace config {
const char *settingHelp(modemcfg::Setting setting) {
    static const char *const descriptions[] = {
        "Answer sequence: 0 ITU-T, 1 Bell (emulated).",
        "Command echo: 0 off, 1 on. Password values stay masked.",
        "Online local echo: 0 on, 1 off. Configuration always masks secrets.",
        "Speaker level: 0-3; the volume knob still applies.",
        "Speaker: 0 silent, 1 until connect, 2 always, 3 omit dial tone.",
        "Results: 0 shown, 1 quiet, 2 quiet when answering.",
        "Result format: 0 numeric, 1 words.",
        "Result detail: X0-X4 USR progress/error code sets.",
        "Default dialing: 0 pulse, 1 tone.",
        "Extra CONNECT detail: 0 none, 1 ARQ, 2 modulation, 3 protocol.",
        "Host speed: 0 follows call, 1 fixed, 2 mixed; console stays 115200.",
        "DCD: 0 always asserted, 1 follows carrier.",
        "DTR: 0 ignore, 2 drop hangs up, 3 drop resets; S25 filters drops.",
        "Guard tone selection (compatibility setting).",
        "Transmit flow: 0 none, 1 CTS, 2 XON/XOFF, 3 both.",
        "Receive software flow control; USR modes 0,1,2,5.",
        "Compression preference (compatibility setting; TCP payload is unchanged).",
        "Error-control preference (compatibility setting).",
        "Operational speed index: 0 maximum, 1-38 USR table. Overrides model reporting.",
        "Pulse ratio: 0 US, 1 UK.",
        "Receive hardware flow: 1 ignore RTS, 2 obey RTS.",
        "DSR is hard-wired asserted; only 0 is supported.",
        "Remote digital loopback preference: 4 allow, 5 refuse (emulated).",
        "Minimum line-speed index: 0 disabled, 1-38 USR table.",
        "Break handling preference (compatibility setting).",
    };
    static_assert(sizeof descriptions / sizeof descriptions[0] == modemcfg::kSettingCount,
                  "settings_help needs one description per setting");
    unsigned index = static_cast<unsigned>(setting);
    return index < modemcfg::kSettingCount ? descriptions[index] : "Unknown setting.";
}
const char *registerHelp(unsigned reg) {
    switch (reg) {
        case 0:
            return "Auto-answer threshold: 0 manual only; otherwise number of rings. An unanswered "
                   "call is dropped after 8 rings, so more than 8 counts as 8.";
        case 1:
            return "Live ring count; read only in this screen and never saved.";
        case 2:
            return "Escape character code; values above 127 disable guarded escape.";
        case 3:
            return "AT command terminator character code (normally 13 / CR). 0-127, never "
                   "printable.";
        case 4:
            return "Response line-feed character code (normally 10 / LF).";
        case 5:
            return "AT Backspace character code; DEL is also accepted.";
        case 6:
            return "Dial-tone wait in seconds.";
        case 7:
            return "Outgoing connection timeout in seconds; not an incoming ring timeout.";
        case 8:
            return "Comma dial-pause compatibility register.";
        case 10:
            return "Carrier-loss compatibility delay; TCP disconnect is detected directly.";
        case 11:
            return "DTMF digit duration in milliseconds.";
        case 12:
            return "Escape guard time in fiftieths of a second (50 = one second).";
        case 22:
            return "XON character code.";
        case 23:
            return "XOFF character code.";
        case 25:
            return "DTR drop filter in hundredths of a second.";
        default:
            return "USR compatibility register; stored value, no additional hardware behavior.";
    }
}
}  // namespace config
