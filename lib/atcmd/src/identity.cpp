// ATI0-ATI7, ATI9 and the settings screens: Sportster 14400 identity (Jon, v0.8).
// Commands and selectable speeds retain the later USR capabilities through 56000.
//
// Sources (credits/LICENSES.md):
//   - Settings and diagnostic screen layout: a capture from a real
//     "Sportster 33600/Fax V2.31", posted in the fido7.ru.modem echo (2004), archived at
//     https://fido7.ru.modem.narkive.com/YzTdiljZ/sportster-33600
//   - What each inquiry shows: USR Sportster technical reference (techref_uk.pdf) and
//     the Sportster user guide.
//   - No verified 14400 ROM capture was found: ATI0/ATI1 are emulator placeholders,
//     ATI3 has no invented ROM revision, and ATI7 labels hardware details as emulated.
//     ATI9 returns the model name, without claiming a manufacturer's PnP identifier.
//
// Changes from the capture (project settings that a real Sportster keeps on DIP
// switches or doesn't have): ATI4 adds L; ATI5 adds E, L, Q, V, &C and &D; ATI5 doesn't
// pause with "Strike a key when ready".
#include <cstdio>
#include <cstring>

#include "atcmd.h"

namespace atcmd {

using modemcfg::Profile;
using modemcfg::Setting;

namespace {

const char kProductCode[] = "1441";  // ATI0 (emulator placeholder, see above)
const char kRomChecksum[] = "0000";  // ATI1 (no physical ROM)
const char kProductId[] = "USRobotics Sportster 14400 Fax";
const char kName[] = "USRobotics Sportster 14400 Fax";

void settingsLine(Port &port, const Profile &p, const Setting *list, size_t count,
                  const char *before, const char *after) {
    char buf[96] = {};
    size_t n = 0;
    auto add = [&](const char *text) {
        if (n > 0) {
            n += static_cast<size_t>(snprintf(buf + n, sizeof buf - n, "  "));
        }
        n += static_cast<size_t>(snprintf(buf + n, sizeof buf - n, "%s", text));
    };
    if (before != nullptr) {
        add(before);
    }
    for (size_t i = 0; i < count; ++i) {
        char item[8];
        snprintf(item, sizeof item, "%s%u", modemcfg::settingInfo(list[i]).name,
                 static_cast<unsigned>(p.get(list[i])));
        add(item);
    }
    if (after != nullptr) {
        add(after);
    }
    port.line(buf);
}

void sRegisters(Port &port, const Profile &p, const uint8_t *list) {
    char buf[80];
    size_t n = 0;
    int onLine = 0;
    for (const uint8_t *r = list; *r != 0xFF; ++r) {
        n += static_cast<size_t>(snprintf(buf + n, sizeof buf - n, "%sS%02u=%03u",
                                          onLine > 0 ? "  " : "", static_cast<unsigned>(*r),
                                          static_cast<unsigned>(p.s[*r])));
        if (++onLine == 7) {
            port.line(buf);
            n = 0;
            onLine = 0;
        }
    }
    if (onLine > 0) {
        port.line(buf);
    }
}

const Setting kAmp1[] = {modemcfg::kAmpA, modemcfg::kAmpB, modemcfg::kAmpC, modemcfg::kAmpD,
                         modemcfg::kAmpG, modemcfg::kAmpH, modemcfg::kAmpI, modemcfg::kAmpK,
                         modemcfg::kAmpM, modemcfg::kAmpN};
const Setting kAmp2[] = {modemcfg::kAmpP, modemcfg::kAmpR, modemcfg::kAmpS,
                         modemcfg::kAmpT, modemcfg::kAmpU, modemcfg::kAmpY};

void ampersandLines(Port &port, const Profile &p) {
    settingsLine(port, p, kAmp1, sizeof kAmp1 / sizeof kAmp1[0], nullptr, nullptr);
    settingsLine(port, p, kAmp2, sizeof kAmp2 / sizeof kAmp2[0], nullptr, nullptr);
}

void baudLine(Port &port, uint32_t baud) {
    port.printf("BAUD=%u  PARITY=N  WORDLEN=8", static_cast<unsigned>(baud));
    port.newline();
}

const char *dialName(const Profile &p) {
    return p.get(modemcfg::kTone) != 0 ? "DIAL=TONE" : "DIAL=PULSE";
}

// "Chars sent                    0      Chars Received                0"
void counters(Port &port, const char *left, const char *right) {
    if (right == nullptr) {
        port.printf("%s%*u", left, static_cast<int>(31 - strlen(left)), 0u);
    } else {
        port.printf("%s%*u      %s%*u", left, static_cast<int>(31 - strlen(left)), 0u, right,
                    static_cast<int>(31 - strlen(right)), 0u);
    }
    port.newline();
}

void field(Port &port, const char *label, const char *value) {
    port.printf("%-23s%s", label, value);
    port.newline();
}

void showDiagnostics(Port &port) {  // ATI6, with no call since power-on
    port.beginInfo();
    port.newline();
    port.printf("%s Link Diagnostics...", kName);
    port.newline();
    port.newline();
    counters(port, "Chars sent", "Chars Received");
    counters(port, "Chars lost", nullptr);
    counters(port, "Octets sent", "Octets Received");
    counters(port, "Blocks sent", "Blocks Received");
    counters(port, "Blocks resent", nullptr);
    port.newline();
    counters(port, "Retrains Requested", "Retrains Granted");
    counters(port, "Line Reversals", "Blers");
    counters(port, "Link Timeouts", "Link Naks");
    port.newline();
    field(port, "Data Compression", "NONE");
    field(port, "Equalization", "Long");
    field(port, "Fallback", "Disabled");
    field(port, "Protocol", "NONE");
    field(port, "Speed", "0/0");
    field(port, "Last Call", "00:00:00");
}

void showConfiguration(Port &port) {  // ATI7
    port.beginInfo();
    port.line("Configuration Profile...");
    port.newline();
    field(port, "Product type", "US/Canada External");
    field(port, "Options", "V32bis");
    field(port, "Fax Options", "NONE");
    field(port, "Clock Freq", "Emulated");
    field(port, "Eprom", "Emulated");
    field(port, "Ram", "Emulated");
    port.newline();
    field(port, "EPROM date", "N/A");
    field(port, "DSP date", "N/A");
    port.newline();
    field(port, "EPROM rev", "N/A");
    field(port, "DSP rev", "N/A");
}

}  // namespace

void showCurrent(Port &port) {
    const Profile &p = port.modem().active();
    port.beginInfo();
    port.newline();
    port.printf("%s Settings...", kName);
    port.newline();
    port.newline();
    static const Setting kLetters[] = {modemcfg::kB, modemcfg::kE, modemcfg::kF, modemcfg::kL,
                                       modemcfg::kM, modemcfg::kQ, modemcfg::kV, modemcfg::kX};
    char y[12];
    snprintf(y, sizeof y, "Y%u", static_cast<unsigned>(port.modem().nvram().y));
    settingsLine(port, p, kLetters, sizeof kLetters / sizeof kLetters[0], nullptr, y);
    baudLine(port, port.baud());
    port.printf("%s  %s", dialName(p), port.callUp() ? "OFF HOOK" : "ON HOOK");
    port.newline();
    port.newline();
    ampersandLines(port, p);
    port.newline();
    sRegisters(port, p, modemcfg::kSRegsCurrent);
    port.newline();
    const char *last = port.modem().lastDialed();
    port.printf("LAST DIALED #:%s%s", last[0] != '\0' ? " " : "", last);
    port.newline();
}

void showStored(Port &port) {
    const modemcfg::Nvram &n = port.modem().nvram();
    port.beginInfo();
    port.printf("%s NVRAM Settings...", kName);
    port.newline();
    static const Setting kLetters[] = {modemcfg::kB, modemcfg::kE, modemcfg::kF, modemcfg::kL,
                                       modemcfg::kM, modemcfg::kQ, modemcfg::kV, modemcfg::kX};
    for (unsigned t = 0; t < modemcfg::kTemplates; ++t) {
        const Profile &p = n.tmpl[t];
        port.newline();
        if (t > 0) {
            port.newline();
        }
        port.printf("Template Y%u", t);
        port.newline();
        port.newline();
        settingsLine(port, p, kLetters, sizeof kLetters / sizeof kLetters[0], dialName(p), nullptr);
        baudLine(port, p.baud);
        port.newline();
        ampersandLines(port, p);
        port.newline();
        sRegisters(port, p, modemcfg::kSRegsStored);
    }
    port.newline();
    for (unsigned i = 0; i < modemcfg::kPhoneSlots; ++i) {
        const char *num = n.phone[i];
        port.printf("%s#%u:%s%s", i == 0 ? "STORED PHONE " : "", i, num[0] != '\0' ? " " : "", num);
        port.newline();
    }
}

bool showInfo(Port &port, unsigned n) {
    switch (n) {
        case 0:
            port.beginInfo();
            port.line(kProductCode);
            return true;
        case 1:
            port.beginInfo();
            port.line(kRomChecksum);
            return true;
        case 2:  // RAM test: passes
            port.beginInfo();
            port.line("OK");
            return true;
        case 3:
            port.beginInfo();
            port.line(kProductId);
            return true;
        case 4:
            showCurrent(port);
            return true;
        case 5:
            showStored(port);
            return true;
        case 6:
            showDiagnostics(port);
            return true;
        case 7:
            showConfiguration(port);
            return true;
        case 9:  // model inquiry; no unverified manufacturer PnP ID
            port.beginInfo();
            port.line(kProductId);
            return true;
        default:
            return false;
    }
}

void showStarHelp(Port &port) {
    const StarCommand *t = port.modem().starTable();
    port.beginInfo();
    for (size_t i = 0; i < port.modem().starCount(); ++i) {
        port.printf("  %-52s %s", t[i].usage, t[i].help);
        port.newline();
    }
}

}  // namespace atcmd
