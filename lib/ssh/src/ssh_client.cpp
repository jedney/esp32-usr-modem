#include "ssh_client.h"
#include <cstring>

namespace ssh {
void Client::begin(const char *host, unsigned columns, unsigned rows) {
    transport_.begin();
    screen_.clear();
    typed_.clear();
    strncpy(host_, host, sizeof host_ - 1);
    host_[sizeof host_ - 1] = 0;
    wipe(user_, sizeof user_);
    wipe(line_, sizeof line_);
    lineSize_ = 0;
    stage_ = Stage::kHandshake;
    tries_ = 0;
    columns_ = columns;
    rows_ = rows;
    remote_ = remoteWindow_ = remoteMax_ = localWindow_ = 0;
    failures_ = globalFailures_ = 0;
    serviceWanted_ = true;
    authWanted_ = openWanted_ = ptyWanted_ = shellWanted_ = sizeWanted_ = false;
    reason_ = "";
}
void Client::end() {
    wipe(user_, sizeof user_);
    wipe(line_, sizeof line_);
    wipe(scratch_, sizeof scratch_);
    lineSize_ = 0;
    authWanted_ = false;
    typed_.clear();
    transport_.end();
    stage_ = Stage::kOver;
}
void Client::finish(const char *message, const char *reason) {
    if (message) {
        show(message);
    }
    reason_ = reason;
    stage_ = Stage::kOver;
    wipe(user_, sizeof user_);
    wipe(line_, sizeof line_);
    lineSize_ = 0;
    authWanted_ = false;
}

void Client::show(const char *text) {
    for (; *text && screen_.put(uint8_t(*text)); ++text) {
    }
}
void Client::showText(const uint8_t *text, size_t size) {
    bool cr = false;
    for (size_t i = 0; i < size && screen_.space() >= 2; ++i) {
        if (text[i] == '\n' && !cr) {
            screen_.put('\r');
        }
        screen_.put(text[i]);
        cr = text[i] == '\r';
    }
}
void Client::prompt() {
    if (stage_ == Stage::kUser) {
        show("login as: ");
    } else {
        show(user_);
        show("@");
        show(host_);
        show("'s password: ");
    }
}

size_t Client::space() const {
    switch (stage_) {
        case Stage::kOpening:
        case Stage::kShell:
        case Stage::kOnline:
            return typed_.space();
        case Stage::kOver:
            return 0;
        default:
            return 256;
    }
}
size_t Client::write(const uint8_t *data, size_t size) {
    if (stage_ == Stage::kOpening || stage_ == Stage::kShell || stage_ == Stage::kOnline) {
        size_t n = size < typed_.space() ? size : typed_.space();
        for (size_t i = 0; i < n; ++i) {
            typed_.put(data[i]);
        }
        return n;
    }
    for (size_t i = 0; i < size; ++i) {
        if (stage_ != Stage::kUser && stage_ != Stage::kPassword) {
            break;  // typed while nothing reads it: dropped
        }
        type(data[i]);
    }
    return size;
}
void Client::type(uint8_t b) {
    const bool user = stage_ == Stage::kUser;
    if (b == '\r') {
        show("\r\n");
        if (!user) {
            stage_ = Stage::kAuth;
            authWanted_ = true;
            return;
        }
        if (lineSize_) {
            memcpy(user_, line_, lineSize_);
            user_[lineSize_] = 0;
            wipe(line_, sizeof line_);
            lineSize_ = 0;
            stage_ = Stage::kPassword;
        }
        prompt();
        return;
    }
    if (b == 8 || b == 127) {
        if (lineSize_) {
            line_[--lineSize_] = 0;
            if (user) {
                show("\b \b");
            }
        }
        return;
    }
    if (b < 32) {
        return;  // LF and other control characters
    }
    if (lineSize_ < (user ? kMaxUser : kMaxPassword)) {
        line_[lineSize_++] = char(b);
        if (user) {
            screen_.put(b);
        }
    }
}
void Client::resize(unsigned columns, unsigned rows) {
    if (columns == columns_ && rows == rows_) {
        return;
    }
    columns_ = columns;
    rows_ = rows;
    if ((stage_ == Stage::kShell || stage_ == Stage::kOnline) && !ptyWanted_) {
        sizeWanted_ = true;
    }
}

void Client::poll() {
    if (stage_ == Stage::kOver) {
        return;
    }
    transport_.poll();
    const uint8_t *payload;
    size_t size;
    while (stage_ != Stage::kOver && transport_.next(payload, size)) {
        handle(payload, size);
        transport_.done();
    }
    if (stage_ != Stage::kOver && transport_.failed()) {
        if (transport_.established()) {
            show("\r\n");
            show(transport_.reason());
            show("\r\n");
        }
        finish(nullptr, transport_.reason());
    }
    if (stage_ != Stage::kOver) {
        send();
    }
}

void Client::handle(const uint8_t *payload, size_t size) {
    Reader r(payload + 1, size - 1);
    const uint8_t *text;
    size_t length;
    uint32_t channel = 0, value = 0;
    bool want = false;
    switch (payload[0]) {
        case kServiceAccept:
            if (stage_ == Stage::kService) {
                stage_ = Stage::kUser;
                prompt();
            }
            break;
        case kUserauthBanner:
            if (r.string(text, length)) {
                showText(text, length);
            }
            break;
        case kUserauthSuccess:
            if (stage_ == Stage::kAuth) {
                stage_ = Stage::kOpening;
                openWanted_ = true;
            }
            break;
        case kUserauthFailure:
            if (stage_ != Stage::kAuth) {
                break;
            }
            if (!r.string(text, length) || !listHas(text, length, "password")) {
                finish("This server does not accept password login.\r\n",
                       "SSH: password login refused");
            } else if (++tries_ >= kMaxTries) {
                finish("Access denied\r\nToo many failed logins.\r\n", "SSH: login failed");
            } else {
                show("Access denied\r\n");
                stage_ = Stage::kPassword;
                prompt();
            }
            break;
        case kUserauthPasswdChangereq:
            finish("The server wants a new password, which the modem cannot set.\r\n",
                   "SSH: password change needed");
            break;
        case kChannelOpenConfirmation: {
            uint32_t theirs = 0, window = 0, max = 0;
            r.u32(channel);
            r.u32(theirs);
            r.u32(window);
            r.u32(max);
            if (r.ok() && stage_ == Stage::kOpening) {
                remote_ = theirs;
                remoteWindow_ = window;
                remoteMax_ = max ? max : 1;
                stage_ = Stage::kShell;
                ptyWanted_ = shellWanted_ = true;
            }
            break;
        }
        case kChannelOpenFailure:
            if (stage_ == Stage::kOpening) {
                finish("The server refused a terminal session.\r\n", "SSH: session refused");
            }
            break;
        case kChannelWindowAdjust:
            r.u32(channel);
            r.u32(value);
            if (r.ok()) {
                remoteWindow_ =
                    value > 0xFFFFFFFFu - remoteWindow_ ? 0xFFFFFFFFu : remoteWindow_ + value;
            }
            break;
        case kChannelData:
        case kChannelExtendedData:
            r.u32(channel);
            if (payload[0] == kChannelExtendedData) {
                r.u32(value);
            }
            if (!r.string(text, length)) {
                break;
            }
            if (length > localWindow_ || length > screen_.space()) {
                finish(nullptr, "SSH: the server sent too much");
                break;
            }
            localWindow_ -= uint32_t(length);
            for (size_t i = 0; i < length; ++i) {
                screen_.put(text[i]);
            }
            break;
        case kChannelClose: {
            uint8_t close[5];
            Writer(close, sizeof close).byte(kChannelClose).u32(remote_);
            transport_.send(close, sizeof close);
            finish(nullptr, "");
            break;
        }
        case kChannelRequest:
            r.u32(channel);
            r.string(text, length);
            r.boolean(want);
            if (r.ok() && want) {
                ++failures_;
            }
            break;
        case kChannelSuccess:
            if (stage_ == Stage::kShell) {
                stage_ = Stage::kOnline;
            }
            break;
        case kChannelFailure:
            if (stage_ == Stage::kShell) {
                finish("The server refused a shell.\r\n", "SSH: shell refused");
            }
            break;
        case kGlobalRequest:
            r.string(text, length);
            r.boolean(want);
            if (r.ok() && want) {
                ++globalFailures_;
            }
            break;
        default:
            break;  // EOF, replies and anything else need nothing
    }
}

bool Client::sendAuth() {
    Writer w(scratch_, sizeof scratch_);
    w.byte(kUserauthRequest)
        .string(user_)
        .string("ssh-connection")
        .string("password")
        .boolean(false)
        .string(line_, lineSize_);
    const bool ok = w.ok() && transport_.send(scratch_, w.size());
    wipe(scratch_, sizeof scratch_);
    if (ok) {
        wipe(line_, sizeof line_);
        lineSize_ = 0;
    }
    return ok;
}
bool Client::sendData() {
    while (typed_.size() && remoteWindow_) {
        size_t n = typed_.size();
        const size_t limits[] = {remoteWindow_, remoteMax_, kMaxChannelPacket};
        for (size_t limit : limits) {
            n = n < limit ? n : limit;
        }
        // Built in place: the loop task's stack is small.
        Writer w(scratch_, sizeof scratch_);
        w.byte(kChannelData).u32(remote_).u32(uint32_t(n));
        typed_.peek(scratch_ + w.size(), n);
        if (!w.ok() || !transport_.send(scratch_, w.size() + n)) {
            return false;
        }
        typed_.drop(n);
        remoteWindow_ -= uint32_t(n);
    }
    return true;
}
void Client::send() {
    Writer w(scratch_, sizeof scratch_);
    if (serviceWanted_ && transport_.established()) {
        w.byte(kServiceRequest).string("ssh-userauth");
        if (transport_.send(scratch_, w.size())) {
            serviceWanted_ = false;
            stage_ = Stage::kService;
        }
    }
    if (authWanted_ && sendAuth()) {
        authWanted_ = false;
    }
    if (openWanted_) {
        localWindow_ = uint32_t(screen_.space());
        w = Writer(scratch_, sizeof scratch_);
        w.byte(kChannelOpen).string("session").u32(0).u32(localWindow_).u32(kMaxChannelPacket);
        if (transport_.send(scratch_, w.size())) {
            openWanted_ = false;
        }
    }
    if (ptyWanted_) {
        const uint8_t modes[] = {0};  // TTY_OP_END: no terminal modes
        w = Writer(scratch_, sizeof scratch_);
        w.byte(kChannelRequest).u32(remote_).string("pty-req").boolean(false).string("ansi");
        w.u32(columns_).u32(rows_).u32(0).u32(0).string(modes, sizeof modes);
        if (transport_.send(scratch_, w.size())) {
            ptyWanted_ = false;
        }
    }
    if (shellWanted_ && !ptyWanted_) {
        w = Writer(scratch_, sizeof scratch_);
        w.byte(kChannelRequest).u32(remote_).string("shell").boolean(true);
        if (transport_.send(scratch_, w.size())) {
            shellWanted_ = false;
        }
    }
    if (sizeWanted_) {
        w = Writer(scratch_, sizeof scratch_);
        w.byte(kChannelRequest).u32(remote_).string("window-change").boolean(false);
        w.u32(columns_).u32(rows_).u32(0).u32(0);
        if (transport_.send(scratch_, w.size())) {
            sizeWanted_ = false;
        }
    }
    while (globalFailures_) {
        const uint8_t reply = kRequestFailure;
        if (!transport_.send(&reply, 1)) {
            break;
        }
        --globalFailures_;
    }
    while (failures_) {
        uint8_t reply[5];
        Writer(reply, sizeof reply).byte(kChannelFailure).u32(remote_);
        if (!transport_.send(reply, sizeof reply)) {
            break;
        }
        --failures_;
    }
    if (stage_ != Stage::kShell && stage_ != Stage::kOnline) {
        return;
    }
    // Grant the server more window as the terminal reads what it sent.
    const uint32_t free = uint32_t(screen_.space());
    if (free > localWindow_ && free - localWindow_ >= kScreenSize / 2) {
        uint8_t adjust[9];
        Writer(adjust, sizeof adjust)
            .byte(kChannelWindowAdjust)
            .u32(remote_)
            .u32(free - localWindow_);
        if (transport_.send(adjust, sizeof adjust)) {
            localWindow_ = free;
        }
    }
    if (stage_ == Stage::kOnline) {
        sendData();
    }
}
}  // namespace ssh
