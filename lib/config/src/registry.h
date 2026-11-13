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
// Each of these AT* commands is handled by config::Commands under its own name.
#define USR_CONFIG_COMMAND(NAME)                                        \
    inline atcmd::Reply registry##NAME(atcmd::Port &p, const char *a) { \
        return Commands::dispatch(p, #NAME, a);                         \
    }
USR_CONFIG_COMMAND(CONFIG)
USR_CONFIG_COMMAND(WEB)
USR_CONFIG_COMMAND(NETCONSOLE)
USR_CONFIG_COMMAND(INTERNET)
USR_CONFIG_COMMAND(STYLE)
USR_CONFIG_COMMAND(SIZE)
USR_CONFIG_COMMAND(TEST)
USR_CONFIG_COMMAND(SX)
USR_CONFIG_COMMAND(SY)
USR_CONFIG_COMMAND(SZ)
USR_CONFIG_COMMAND(RX)
USR_CONFIG_COMMAND(RY)
USR_CONFIG_COMMAND(RZ)
USR_CONFIG_COMMAND(STATUS)
USR_CONFIG_COMMAND(SETTING)
USR_CONFIG_COMMAND(PROFILE)
USR_CONFIG_COMMAND(PROFILECTL)
USR_CONFIG_COMMAND(EXPLICIT)
USR_CONFIG_COMMAND(RESTOREEND)
USR_CONFIG_COMMAND(DIAL)
USR_CONFIG_COMMAND(REFERENCE)
USR_CONFIG_COMMAND(ABOUT)
USR_CONFIG_COMMAND(SOUNDS)
USR_CONFIG_COMMAND(PLAY)
USR_CONFIG_COMMAND(LOG)
USR_CONFIG_COMMAND(CALLLOG)
USR_CONFIG_COMMAND(DNS)
USR_CONFIG_COMMAND(PING)
USR_CONFIG_COMMAND(EXPORT)
USR_CONFIG_COMMAND(BOOKCLEAR)
USR_CONFIG_COMMAND(RESTART)
USR_CONFIG_COMMAND(RESETINFO)
#undef USR_CONFIG_COMMAND
class Registry {
public:
    explicit Registry(const Hooks &hooks) {
        const atcmd::StarCommand rows[] = {

            {"ANSWERPORT", "AT*ANSWERPORT=<RS232|USB|CONSOLE|AUTO>",
             "auto-answer destination (? or =?), saved", hooks.cmdAnswerPort, false},
            {"MODEL", "AT*MODEL=<14400|28800|33600|56000>",
             "identity and default CONNECT (? or =?)", atcmd::cmdModel, false},
            {"THROTTLE", "AT*THROTTLE=<ON|OFF>",
             "limit data to operational line speed (default OFF)", atcmd::cmdThrottle, false},
            {"RINGDELAY", "AT*RINGDELAY=<0-60000>",
             "outgoing ringback ms (default 1500; ? or =?); saved", atcmd::cmdRingDelay, false},
            {"CONNECTDELAY", "AT*CONNECTDELAY=<0-60000>",
             "pre-CONNECT delay ms (default 250; ? or =?); saved", atcmd::cmdConnectDelay, false},
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
            {"NETCONSOLE", "AT*NETCONSOLE[?]|=<ON|OFF>",
             "network console on TCP port 2001; saved immediately", registryNETCONSOLE, false},
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
