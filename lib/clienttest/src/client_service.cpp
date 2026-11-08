#include "client_service.h"
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace clienttest {
void Service::text(const char *s) {
    while (*s)
        if (!output_.put(*s++)) {
            finished_ = true;
            break;
        }
}
void Service::format(const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof buffer, fmt, args);
    va_end(args);
    text(buffer);
}
void Service::complete(const char *result) {
    if (mode_ == Mode::Observe && !observed_) {
        if (!*observations_[displaySlot_])
            snprintf(observations_[displaySlot_], sizeof observations_[displaySlot_],
                     "cancelled/unknown");
        findings_.charset = beforeDisplay_;
    }
    platform_.restore();
    restore_ = false;
    format("\r\n%s\r\n", result);
    if (hold_)
        text("Enter returns to Client Tests.\r\n");
    finished_ = true;
    finishedAt_ = now_;
}
void Service::close() {
    if (mode_ == Mode::Observe && !observed_)
        findings_.charset = beforeDisplay_;
    if (restore_)
        platform_.restore();
    platform_.release();
    restore_ = false;
    open_ = false;
    output_.clear();
    memset(input_, 0, sizeof input_);
}
bool Service::start(const char *command, const char *args, uint32_t now) {
    if (open_)
        return false;
    while (*args == ' ' || *args == '=')
        ++args;
    if (!strcmp(command, "TEST") && !strncmp(args, "TRANSFER=", 9)) {
        const char *nested = args + 9;
        if (strlen(nested) < 2 || (nested[2] && nested[2] != '=') ||
            (nested[0] != 'S' && nested[0] != 'R') || !strchr("XYZ", nested[1]))
            return false;
        char name[3] = {nested[0], nested[1], 0};
        return start(name, nested + 2, now);
    }
    output_.clear();
    mode_ = Mode::Output;
    now_ = start_ = last_ = now;
    duration_ = 5000;
    bytes_ = pauses_ = pausedMs_ = length_ = cableStep_ = 0;
    open_ = restore_ = true;
    finished_ = observed_ = false;
    beforeDisplay_ = findings_.charset;
    displaySlot_ = 4;
    protocolEnded_ = false;
    lastRts_ = platform_.rts();
    lastDtr_ = platform_.dtr();
    if (strcmp(command, "TEST")) {
        bool sending = command[0] == 'S';
        Protocol protocol = command[1] == 'Z'   ? Protocol::Z
                            : command[1] == 'Y' ? Protocol::Y
                                                : Protocol::Xcrc;
        char selection[24] = "TEST", variant[8] = "CRC", tail;
        unsigned size = 65536;
        int fields;
        if (sending) {
            fields = sscanf(args, "%23[A-Z0-9.],%7[A-Z0-9],%u%c", selection, variant, &size, &tail);
            if (fields < 1 || fields > 3)
                goto invalid;
            // Y/Z accept SOURCE,SIZE, while X accepts SOURCE,VARIANT,SIZE.
            if (command[1] != 'X') {
                fields = sscanf(args, "%23[A-Z0-9.],%u%c", selection, &size, &tail);
                if (fields < 1 || fields > 2)
                    goto invalid;
            }
        } else if (*args) {
            if (command[1] != 'X' || strlen(args) >= sizeof variant)
                goto invalid;
            snprintf(variant, sizeof variant, "%s", args);
        }
        if (!size || size > Transfer::kLimit)
            goto invalid;
        if (command[1] == 'X') {
            if (!strcmp(variant, "SUM"))
                protocol = Protocol::Xsum;
            else if (!strcmp(variant, "1K"))
                protocol = Protocol::X1k;
            else if (strcmp(variant, "CRC"))
                goto invalid;
        }
        Source *source = sending ? platform_.source(selection, size) : nullptr;
        if (!transfer_.start(protocol, sending, source, now))
            goto invalid;
        mode_ = Mode::Transfer;
        // No text is emitted into the protocol stream.
        return true;
    }
    if (!strcmp(args, "SUMMARY") || !*args || !strcmp(args, "?")) {
        summary();
        complete("OK");
    } else if (!strncmp(args, "SET=", 4)) {
        ClientSettings next;
        if (!parseSettings(args + 4, next))
            goto invalid;
        findings_ = next;
        text("Findings staged; TEST APPLY confirms and saves this port.\r\n");
        complete("OK");
    } else if (!strcmp(args, "APPLY") || !strcmp(args, "RESET")) {
        if (!strcmp(args, "RESET"))
            findings_ = {};
        format("Save %s client: %s,%u,%u,%u? Y confirms; N/Esc cancels.\r\n", portName(port_),
               charsetName(findings_.charset), findings_.columns, findings_.rows,
               findings_.backspace);
        mode_ = Mode::Confirm;
    } else if (!strcmp(args, "SIZE")) {
        text(
            "\x1b"
            "7\x1b[999;999H\x1b[6n");
        mode_ = Mode::Size;
    } else if (!strcmp(args, "KEYS")) {
        text("Raw key codes; Ctrl-] ends; 30 s timeout. Press Backspace to record 08/7F.\r\n");
        mode_ = Mode::Keys;
        duration_ = 30000;
    } else if (!strcmp(args, "ID")) {
        text("ENQ / DA / DSR replies (hex); 2 s capture.\r\n\5\x1b[c\x1b[5n");
        mode_ = Mode::Id;
        duration_ = 2000;
    } else if (!strcmp(args, "ECHO")) {
        text(
            "CR only: A\rB\r\nLF only: A\nB\r\nCRLF: A\r\nB\r\n"
            "Type abc then Enter: doubled letters mean local echo. Ctrl-] ends.\r\n");
        mode_ = Mode::Echo;
        duration_ = 30000;
    } else if (!strcmp(args, "FORMAT")) {
        format("%s: %lu 8N1 (%s); %s\r\n", portName(port_),
               static_cast<unsigned long>(platform_.baud()),
               port_ == kUsbPort ? "USB: speed set by the PC, not used" : "configured UART",
               port_ == kRs232Port ? "RTS/CTS per active &H/&R"
               : port_ == kUsbPort ? "USB flow control; DTR only"
                                   : "host-cable signals unavailable");
        complete("OK");
    } else if (!strcmp(args, "CABLE")) {
        if (port_ != kRs232Port) {
            complete("Unavailable on this port: run the cable test on the RS232 port.");
        } else {
            text(
                "DTR/RTS live; DSR always on (hard-wired). DCD/RI/CTS toggle every 1 s.\r\n"
                "Automatic recovery; Ctrl-] cancels.\r\n");
            mode_ = Mode::Cable;
            duration_ = 6000;
        }
    } else if (!strncmp(args, "TX", 2) || !strncmp(args, "RX", 2)) {
        unsigned seconds = 5;
        char tail;
        if (args[2] &&
            (sscanf(args + 2, "=%u%c", &seconds, &tail) != 1 || !seconds || seconds > 30))
            goto invalid;
        mode_ = args[0] == 'T' ? Mode::Tx : Mode::Rx;
        duration_ = seconds * 1000;
        text("Timed raw data test; starts after 250 ms; Ctrl-] cancels.\r\n");
    } else if (!strcmp(args, "ANSI")) {
        for (unsigned bg = 0; bg < 16; ++bg) {
            for (unsigned fg = 0; fg < 16; ++fg)
                format("\x1b[%u;%um%X", fg < 8 ? 30 + fg : 90 + fg - 8,
                       bg < 8 ? 40 + bg : 100 + bg - 8, fg);
            text("\x1b[0m\r\n");
        }
        text(
            "\x1b[1mBOLD\x1b[0m \x1b[4mUNDERLINE\x1b[0m \x1b[7mREVERSE\x1b[0m "
            "\x1b[5mBLINK\x1b[0m\r\nCursor: X\x1b[1DZ\x1b[K\r\n");
        text("Clear test: press C clears screen/line; Y visible, N failed, Esc cancels.\r\n");
        findings_.charset = Charset::Ansi;
        displaySlot_ = 0;
        observations_[displaySlot_][0] = 0;
        mode_ = Mode::Observe;
    } else if (!strcmp(args, "PC") || !strcmp(args, "LATIN1") || !strcmp(args, "PLAIN")) {
        findings_.charset = !strcmp(args, "PC")       ? Charset::Cp437
                            : !strcmp(args, "LATIN1") ? Charset::Latin1
                                                      : Charset::Plain;
        displaySlot_ = unsigned(findings_.charset);
        observations_[displaySlot_][0] = 0;
        text("Character table (controls represented by dots):\r\n");
        unsigned end = findings_.charset == Charset::Plain ? 128 : 256;
        for (unsigned i = 0; i < end; ++i) {
            if (!(i % 16))
                format("%02X: ", i);
            uint8_t b =
                i < 32 || i == 127 || (findings_.charset == Charset::Latin1 && i < 160 && i >= 128)
                    ? '.'
                    : i;
            output_.put(b);
            output_.put(' ');
            if (i % 16 == 15)
                text("\r\n");
        }
        text(
            "CP437 controls 00-1F/7F are terminal controls, not printable glyphs.\r\n"
            "Y visible, N failed; Esc cancels.\r\n");
        mode_ = Mode::Observe;
    } else if (!strcmp(args, "BELL")) {
        observations_[4][0] = 0;
        text("\7BEL sent; Y heard, N silent; Esc cancels.\r\n");
        mode_ = Mode::Observe;
    } else
        goto invalid;
    return true;
invalid:
    close();
    return false;
}
void Service::summary() {
    const auto &saved = settings_.get(port_);
    format("Client %s findings: %s,%u,%u,%u\r\nSaved: %s,%u,%u,%u\r\n", portName(port_),
           charsetName(findings_.charset), findings_.columns, findings_.rows, findings_.backspace,
           charsetName(saved.charset), saved.columns, saved.rows, saved.backspace);
    text("Unmeasured fields retain saved/default values; display/bell require observation.\r\n");
    text(
        "Tests: ANSI PC PLAIN LATIN1 SIZE KEYS ID ECHO BELL FORMAT TX=seconds RX=seconds "
        "CABLE.\r\n");
    const char *const labels[] = {"ANSI", "CP437", "PLAIN",    "LATIN1", "BELL", "Size",
                                  "Keys", "ID",    "Transfer", "TX",     "RX",   "Cable"};
    for (unsigned i = 0; i < 12; ++i)
        format("%s: %s\r\n", labels[i], *observations_[i] ? observations_[i] : "not run/unknown");
    text(
        "TEST SET=ANSI|CP437|PLAIN|LATIN1,cols,rows,8|127 stages manual findings.\r\n"
        "TEST APPLY confirms/save; RESET confirms/defaults.\r\n"
        "Sources: LOG, TEST (1-16777216 bytes), HS1200.WAV/HS2400.WAV/"
        "HS9600.WAV/HS14400.WAV/HS28800.WAV/HS33600.WAV/HS56000.WAV; ALL batch.\r\n");
}
size_t Service::space() const {
    if (!open_ || (finished_ && !hold_) || output_.space() < 512)
        return 0;
    if (mode_ == Mode::Transfer && !finished_)
        return transfer_.space();
    return 256;
}
size_t Service::write(const uint8_t *data, size_t len) {
    if (!space())
        return 0;
    if (finished_) {
        for (size_t i = 0; i < len; ++i)
            if (data[i] == '\r' || data[i] == 27 || data[i] == 3) {
                close();
                break;
            }
        return len;
    }
    if (mode_ == Mode::Transfer)
        return transfer_.write(data, len);
    size_t used = 0;
    while (used < len && space())
        byte(data[used++]);
    return used;
}
void Service::byte(uint8_t b) {
    if (b == 29 || b == 27 || b == 3) {
        if (mode_ != Mode::Size && mode_ != Mode::Keys && mode_ != Mode::Id) {
            complete("Cancelled");
            return;
        }
        if (b == 29) {
            complete("Capture ended");
            return;
        }
    }
    if (mode_ == Mode::Confirm) {
        if (b == 'y' || b == 'Y') {
            changed_ = settings_.save(port_, findings_);
            complete(changed_ ? "Client settings saved" : "ERROR: settings save failed");
        } else if (b == 'n' || b == 'N')
            complete("Cancelled; saved settings retained");
    } else if (mode_ == Mode::Observe) {
        if (b == 'c' || b == 'C')
            text("\x1b[2J\x1b[HClear screen/line test\x1b[K\r\nY visible, N failed.\r\n");
        else if (b == 'y' || b == 'Y' || b == 'n' || b == 'N') {
            observed_ = b == 'y' || b == 'Y';
            snprintf(observations_[displaySlot_], sizeof observations_[displaySlot_], "%s (manual)",
                     observed_ ? "pass" : "fail");
            complete(observed_ ? "Manual display/bell: pass" : "Manual display/bell: fail");
        }
    } else if (mode_ == Mode::Size) {
        if (length_ + 1 >= sizeof input_) {
            length_ = 0;
            return;
        }
        input_[length_++] = b;
        input_[length_] = 0;
        char *reply = strstr(input_, "\x1b[");
        unsigned rows, cols;
        int used = 0;
        if (reply && sscanf(reply, "\x1b[%u;%uR%n", &rows, &cols, &used) == 2 && used && rows &&
            rows <= 9999 && cols && cols <= 9999) {
            findings_.columns = cols;
            findings_.rows = rows;
            snprintf(observations_[5], sizeof observations_[5], "%u x %u measured", cols, rows);
            text(
                "\x1b"
                "8");
            format("Measured %u columns x %u rows; viewport capped at 132x50.\r\n", cols, rows);
            complete("OK");
        }
    } else if (mode_ == Mode::ManualSize) {
        if (b == '\r' || b == '\n') {
            unsigned cols, rows;
            char tail;
            if (sscanf(input_, "%u,%u%c", &cols, &rows, &tail) == 2 && cols && rows &&
                cols <= 9999 && rows <= 9999) {
                findings_.columns = cols;
                findings_.rows = rows;
                snprintf(observations_[5], sizeof observations_[5], "%u x %u manual", cols, rows);
                complete("Manual size recorded");
            } else {
                text("Enter columns,rows (1-9999): ");
                length_ = 0;
                input_[0] = 0;
            }
        } else if (b >= 32 && b < 127 && length_ + 1 < sizeof input_) {
            input_[length_++] = b;
            input_[length_] = 0;
            output_.put(b);
        }
    } else if (mode_ == Mode::Keys || mode_ == Mode::Id) {
        format("%02X ", b);
        if (mode_ == Mode::Id)
            snprintf(observations_[7], sizeof observations_[7],
                     "reply received (hex shown during capture)");
        if (mode_ == Mode::Keys && (b == 8 || b == 127)) {
            findings_.backspace = b;
            snprintf(observations_[6], sizeof observations_[6], "Backspace %02X observed", b);
            format("[Backspace candidate %02X]\r\n", b);
        }
    } else if (mode_ == Mode::Echo) {
        output_.put(b);
        if (b == '\r')
            text("\r\n[Enter CR received; observations remain manual]\r\n");
    } else if (mode_ == Mode::Rx)
        ++bytes_;
}
size_t Service::read(uint8_t *out, size_t cap) {
    size_t n = output_.read(out, cap);
    if (!n && mode_ == Mode::Transfer)
        n = transfer_.read(out, cap);
    else if (!n && !finished_ && mode_ == Mode::Tx && now_ - start_ >= 250 &&
             now_ - start_ < duration_ + 250) {
        n = cap;
        for (size_t i = 0; i < n; ++i)
            out[i] = ' ' + ((bytes_ + i) % 95);
        bytes_ += n;
    }
    return n;
}
void Service::transferResult() {
    const auto &s = transfer_.statistics();
    const char *result = s.outcome == Outcome::Success     ? "SUCCESS"
                         : s.outcome == Outcome::Cancelled ? "CANCELLED"
                         : s.outcome == Outcome::Timeout   ? "TIMEOUT"
                                                           : "ERROR";
    snprintf(observations_[8], sizeof observations_[8], "%s, %lu bytes", result,
             static_cast<unsigned long>(s.bytes));
    format(
        "\r\nTransfer %s: bytes=%lu ms=%lu cps=%lu retries=%lu errors=%lu files=%lu "
        "CRC32=%08lX\r\n",
        result, static_cast<unsigned long>(s.bytes), static_cast<unsigned long>(s.elapsed),
        static_cast<unsigned long>(s.elapsed ? uint64_t(s.bytes) * 1000 / s.elapsed : 0),
        static_cast<unsigned long>(s.retries), static_cast<unsigned long>(s.errors),
        static_cast<unsigned long>(s.files), static_cast<unsigned long>(~s.crc));
    text("Uploads discarded; XMODEM byte counts include final-block padding.\r\n");
    complete(result);
}
void Service::poll(uint32_t now) {
    // A command can start after the caller sampled this loop's timestamp.
    // Do not interpret that sub-millisecond ordering as an unsigned timeout.
    if (static_cast<int32_t>(now - start_) < 0)
        return;
    now_ = now;
    if (!open_)
        return;
    if (mode_ == Mode::Transfer) {
        transfer_.poll(now);
        if (!transfer_.running() && !protocolEnded_) {
            protocolEnded_ = true;
            protocolEndedAt_ = now;
        }
        if (!finished_ && protocolEnded_ && now - protocolEndedAt_ >= 2000 &&
            (transfer_.pending() || !platform_.drained())) {
            close();  // a paused peer must not retain the modem after a failed transfer
            return;
        }
        if (!finished_ && !transfer_.running() && !transfer_.pending() && platform_.drained())
            transferResult();
    }
    if (finished_) {
        if ((!hold_ && !output_.size() && platform_.drained()) ||
            (!platform_.drained() && now - finishedAt_ >= 2000) ||
            (hold_ && now - finishedAt_ >= 120000))
            close();
        return;
    }
    if (mode_ == Mode::Transfer)
        return;
    if (now - start_ > 120000) {
        complete("Timeout");
        return;
    }
    if (mode_ == Mode::Size && now - start_ >= 1000) {
        text(
            "\x1b"
            "8\r\nNo size reply. Column ruler:\r\n"
            "12345678901234567890123456789012345678901234567890123456789012345678901234567890\r\n"
            "Row ruler follows; note the last visible row before scrolling:\r\n");
        for (unsigned row = 1; row <= 25; ++row)
            format("Row %u\r\n", row);
        text("Enter visible columns,rows: ");
        mode_ = Mode::ManualSize;
        input_[0] = 0;
        length_ = 0;
    }
    if ((mode_ == Mode::Keys || mode_ == Mode::Id || mode_ == Mode::Echo) &&
        now - start_ >= duration_)
        complete("Capture ended; absent replies are unknown");
    if (mode_ == Mode::Confirm && now - start_ >= 30000)
        complete("Confirmation timed out; saved settings retained");
    if (mode_ == Mode::Tx || mode_ == Mode::Rx || mode_ == Mode::Cable) {
        bool rts = platform_.rts(), dtr = platform_.dtr();
        if (port_ == kRs232Port && !rts)
            pausedMs_ += now - last_;
        if (port_ == kRs232Port && lastRts_ && !rts)
            ++pauses_;
        if (mode_ == Mode::Cable) {
            if (rts != lastRts_ || dtr != lastDtr_)
                format("DTR=%u RTS=%u DSR=1\r\n", dtr, rts);
            unsigned step = std::min(6u, (now - start_) / 1000);
            if (!cableStep_ || step + 1 != cableStep_) {
                platform_.restore();
                if (step < 6)
                    platform_.signal(step / 2, step % 2 == 0);
                format("Cable step %u: DTR=%u RTS=%u DSR=1\r\n", step, dtr, rts);
                cableStep_ = step + 1;
            }
        }
        lastRts_ = rts;
        lastDtr_ = dtr;
        last_ = now;
        if (now - start_ >= duration_ + (mode_ == Mode::Cable ? 0 : 250)) {
            if (!platform_.drained() && now - start_ < duration_ + 2250)
                return;
            format("bytes=%lu ms=%lu cps=%lu RTS-pauses=%lu paused-ms=%lu%s\r\n",
                   static_cast<unsigned long>(bytes_), static_cast<unsigned long>(now - start_),
                   static_cast<unsigned long>(uint64_t(bytes_) * 1000 / std::max(1u, now - start_)),
                   static_cast<unsigned long>(pauses_), static_cast<unsigned long>(pausedMs_),
                   port_ == kRs232Port ? "" : " (host-cable signals unavailable)");
            if (!platform_.drained())
                text("Output bytes above are queued; completed transmission count is unknown.\r\n");
            unsigned slot = mode_ == Mode::Tx ? 9 : mode_ == Mode::Rx ? 10 : 11;
            snprintf(observations_[slot], sizeof observations_[slot], "bytes=%lu RTS-pauses=%lu%s",
                     static_cast<unsigned long>(bytes_), static_cast<unsigned long>(pauses_),
                     platform_.drained() ? "; completed" : "; output blocked");
            complete(platform_.drained() ? "Timed test complete" : "Timeout: UART output blocked");
        }
    }
}
}  // namespace clienttest
