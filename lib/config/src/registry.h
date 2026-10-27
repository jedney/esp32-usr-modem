#pragma once
#include "config.h"
#include "device_commands.h"
#include "net_commands.h"
namespace config {
struct Hooks {
    atcmd::StarHandler cmdAnswerPort = nullptr;
    atcmd::StarHandler cmdPhonebook = nullptr;
    atcmd::StarHandler cmdPhoneDelete = nullptr;
    atcmd::StarHandler cmdHelp = nullptr;
    atcmd::StarHandler cmdVer = nullptr;
    atcmd::StarHandler cmdBaud = nullptr;
    atcmd::StarHandler cmdTestCall = nullptr;
    atcmd::StarHandler cmdDialTest = nullptr;
    atcmd::StarHandler cmdHw = nullptr;
};
inline atcmd::Reply registryCONFIG(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "CONFIG", a);
}
inline atcmd::Reply registryWEB(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "WEB", a);
}
inline atcmd::Reply registryINTERNET(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "INTERNET", a);
}
inline atcmd::Reply registrySTYLE(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "STYLE", a);
}
inline atcmd::Reply registrySIZE(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "SIZE", a);
}
inline atcmd::Reply registryTEST(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "TEST", a);
}
inline atcmd::Reply registrySX(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "SX", a);
}
inline atcmd::Reply registrySY(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "SY", a);
}
inline atcmd::Reply registrySZ(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "SZ", a);
}
inline atcmd::Reply registryRX(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "RX", a);
}
inline atcmd::Reply registryRY(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "RY", a);
}
inline atcmd::Reply registryRZ(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "RZ", a);
}
inline atcmd::Reply registrySTATUS(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "STATUS", a);
}
inline atcmd::Reply registrySETTING(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "SETTING", a);
}
inline atcmd::Reply registryPROFILE(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "PROFILE", a);
}
inline atcmd::Reply registryPROFILECTL(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "PROFILECTL", a);
}
inline atcmd::Reply registryEXPLICIT(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "EXPLICIT", a);
}
inline atcmd::Reply registryRESTOREEND(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "RESTOREEND", a);
}
inline atcmd::Reply registryDIAL(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "DIAL", a);
}
inline atcmd::Reply registryREFERENCE(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "REFERENCE", a);
}
inline atcmd::Reply registryABOUT(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "ABOUT", a);
}
inline atcmd::Reply registrySOUNDS(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "SOUNDS", a);
}
inline atcmd::Reply registryPLAY(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "PLAY", a);
}
inline atcmd::Reply registryLOG(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "LOG", a);
}
inline atcmd::Reply registryCALLLOG(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "CALLLOG", a);
}
inline atcmd::Reply registryDNS(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "DNS", a);
}
inline atcmd::Reply registryPING(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "PING", a);
}
inline atcmd::Reply registryEXPORT(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "EXPORT", a);
}
inline atcmd::Reply registryBOOKCLEAR(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "BOOKCLEAR", a);
}
inline atcmd::Reply registryRESTART(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "RESTART", a);
}
inline atcmd::Reply registryRESETINFO(atcmd::Port &p, const char *a) {
    return Commands::dispatch(p, "RESETINFO", a);
}
class Registry {
public:
    explicit Registry(const Hooks &hooks) {
        const atcmd::StarCommand rows[] = {

            {"ANSWERPORT", "AT*ANSWERPORT=<RS232|CONSOLE>",
             "auto-answer destination (? or =?), saved", hooks.cmdAnswerPort, false},
            {"MODEL", "AT*MODEL=<14400|28800|33600|56000>",
             "identity and default CONNECT (? or =?)", atcmd::cmdModel, false},
            {"THROTTLE", "AT*THROTTLE=<ON|OFF>",
             "limit data to operational line speed (default OFF)", atcmd::cmdThrottle, false},
            {"RINGDELAY", "AT*RINGDELAY=<0-60000>",
             "outgoing ringback ms (default 1500; ? or =?); saved", atcmd::cmdRingDelay, false},
            {"CONNECTDELAY", "AT*CONNECTDELAY=<0-60000>",
             "post-CONNECT payload delay ms (default 250; ? or =?); saved", atcmd::cmdConnectDelay,
             false},
            {"PHONEBOOK", "AT*PHONEBOOK[?]|=<number>,<host>,<port>,<TELNET|RAW>[,<speed>]",
             "list/save 64 phonebook entries", hooks.cmdPhonebook, false},
            {"PHONEDELETE", "AT*PHONEDELETE=<number>", "delete a phonebook entry",
             hooks.cmdPhoneDelete, false},
            {"HELP", "AT*HELP", "this list", hooks.cmdHelp, false},
            {"?", "AT*?", "same as AT*HELP", hooks.cmdHelp, false},
            {"VER", "AT*VER", "firmware version", hooks.cmdVer, false},
            {"BAUD", "AT*BAUD=<AUTO|300-115200>", "RS232 speed: from \"AT\" (AUTO) or fixed",
             hooks.cmdBaud, false},
            {"TESTCALL", "AT*TESTCALL[=<seconds>]", "test call to a built-in echo (+++ returns)",
             hooks.cmdTestCall, false},
            {"DIALTEST", "AT*DIALTEST <dial string>", "show how a number is dialled",
             hooks.cmdDialTest, false},
            NETCFG_STAR_COMMANDS,
            DEVICECFG_STAR_COMMANDS,
            {"HW", "AT*HW...", "hardware tests (AT*HW? lists them)", hooks.cmdHw, true},

            {"CONFIG", "AT*CONFIG[?]", "open local configuration", registryCONFIG, false},
            {"TEST", "AT*TEST <test|SUMMARY|SET=charset,cols,rows,bs|APPLY|RESET>",
             "serial client diagnostics; saved per port", registryTEST, false},
            {"SX", "AT*SX=<source>[,SUM|CRC|1K[,bytes]]", "XMODEM download; TEST/LOG/HSxxxxx.WAV",
             registrySX, false},
            {"SY", "AT*SY=<source|ALL>[,bytes]", "YMODEM batch download", registrySY, false},
            {"SZ", "AT*SZ=<source|ALL>[,bytes]", "ZMODEM download", registrySZ, false},
            {"RX", "AT*RX[=SUM|CRC|1K]", "XMODEM upload; validate and discard", registryRX, false},
            {"RY", "AT*RY", "YMODEM upload; validate and discard", registryRY, false},
            {"RZ", "AT*RZ", "ZMODEM upload; validate and discard", registryRZ, false},
            {"WEB", "AT*WEB[?]|=<ON|OFF>", "web listener; saved immediately", registryWEB, false},
            {"INTERNET", "AT*INTERNET[?]|=IP,local,peer|=DNS,AUTO|ip|=HANGUP",
             "PPP/SLIP settings, status and local hang-up", registryINTERNET, false},
            {"STYLE", "AT*STYLE=<ANSI|CP437|PLAIN>", "terminal style, local session", registrySTYLE,
             false},
            {"SIZE", "AT*SIZE[?]|=<1-132>,<1-50>", "terminal cells, local session", registrySIZE,
             false},
            {"STATUS", "AT*STATUS", "status and live panel", registrySTATUS, false},
            {"SETTING", "AT*SETTING[?]|=<modem command>", "active/profile settings; &W saves",
             registrySETTING, false},
            {"PROFILE", "AT*PROFILE=<slot>,<baud>", "save active profile with serial speed",
             registryPROFILE, false},
            {"PROFILECTL", "AT*PROFILECTL=<slot>,<cr>,<lf>,<q>,<v>",
             "stored profile response formatting", registryPROFILECTL, false},
            {"EXPLICIT", "AT*EXPLICIT=<0|1>", "active line-speed override flag", registryEXPLICIT,
             false},
            {"RESTOREEND", "AT*RESTOREEND=<baud>,<q>,<v>,<cr>,<lf>",
             "finish export restore; baud 0 = AUTO", registryRESTOREEND, false},
            {"DIAL", "AT*DIAL=<number|host:port>", "dial using shared call engine", registryDIAL,
             false},
            {"REFERENCE", "AT*REFERENCE", "AT and AT* reference", registryREFERENCE, false},
            {"ABOUT", "AT*ABOUT", "version and licensing record", registryABOUT, false},
            {"SOUNDS", "AT*SOUNDS", "list generated tones and WAVs", registrySOUNDS, false},
            {"PLAY", "AT*PLAY=<name|speed>", "play a sound; any key cancels", registryPLAY, false},
            {"LOG", "AT*LOG[?]|=<0|1|2|CLEAR|offset,count>", "structured debug ring", registryLOG,
             false},
            {"CALLLOG", "AT*CALLLOG", "last 50 calls, retained across restart", registryCALLLOG,
             false},
            {"DNS", "AT*DNS=<host>", "asynchronous DNS lookup", registryDNS, false},
            {"PING", "AT*PING=<host>", "bounded ICMP test", registryPING, false},
            {"EXPORT", "AT*EXPORT", "configuration restore commands without passwords",
             registryEXPORT, false},
            {"BOOKCLEAR", "AT*BOOKCLEAR", "empty phonebook for export restore", registryBOOKCLEAR,
             false},
            {"RESTART", "AT*RESTART", "restart with confirmation", registryRESTART, false, true},
            {"RESETINFO", "AT*RESETINFO", "persisted reset and crash information",
             registryRESETINFO, false},
        };
        count_ = sizeof rows / sizeof rows[0];
        static_assert(sizeof rows / sizeof rows[0] <= 64, "AT* registry capacity");
        for (size_t i = 0; i < count_; ++i) {
            table_[i] = rows[i];
        }
    }
    const atcmd::StarCommand *data() const { return table_; }
    size_t size() const { return count_; }

private:
    atcmd::StarCommand table_[64] = {{"", "", "", nullptr, false}};
    size_t count_ = 0;
};
}  // namespace config
