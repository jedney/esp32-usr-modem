/*
   Copyright 2016-2026 Bo Zimmerman

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

           http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
// Adapted from Zimodem's handleAsciiIAC, Copyright 2016-2026 Bo Zimmerman.
// Apache-2.0; see credits/zimodem. Changed for this project: nonblocking parser,
// bounded subnegotiation, stateful option replies, BINARY, ANSI, and 80x24 NAWS.
#include "telnet.h"
#include <cstring>
#include <initializer_list>

namespace calls {
namespace {
constexpr uint8_t IAC = 255, WILL = 251, WONT = 252, DO = 253, DONT = 254, SB = 250, SE = 240;
constexpr uint8_t BINARY = 0, ECHO = 1, SGA = 3, TTYPE = 24, NAWS = 31;
}  // namespace
void Telnet::command(uint8_t cmd, uint8_t option) {
    tx.put(IAC);
    tx.put(cmd);
    tx.put(option);
}
void Telnet::begin() {
    rx.clear();
    tx.clear();
    memset(local_, 0, sizeof local_);
    memset(remote_, 0, sizeof remote_);
    state_ = 0;
    subLen_ = 0;
    subOverflow_ = false;
    for (uint8_t option : {BINARY, SGA, TTYPE, NAWS}) {
        local_[option] = 1;
        command(WILL, option);
    }
    remote_[BINARY] = remote_[SGA] = 1;
    command(DO, BINARY);
    command(DO, SGA);
}
void Telnet::negotiate(uint8_t option) {
    bool positive = cmd_ == DO || cmd_ == WILL;
    bool local = cmd_ == DO || cmd_ == DONT;
    uint8_t &state = local ? local_[option] : remote_[option];
    bool supported = option == BINARY || option == SGA ||
                     (local ? option == TTYPE || option == NAWS : option == ECHO);
    uint8_t yes = local ? WILL : DO, no = local ? WONT : DONT;
    if (positive && supported) {
        if (state == 0 || state == 3) {
            command(yes, option);
        }
        bool newlyEnabled = state != 2;
        state = 2;
        if (local && option == NAWS && newlyEnabled) {
            const uint8_t size[] = {IAC, SB, NAWS, 0, 80, 0, 24, IAC, SE};
            for (uint8_t b : size) {
                tx.put(b);
            }
        }
    } else if (positive) {
        if (state != 3) {
            command(no, option);
        }
        state = 3;
    } else {
        if (state == 2) {
            command(no, option);
        }
        state = 0;
    }
}
void Telnet::subnegotiation() {
    if (!subOverflow_ && subLen_ == 2 && sub_[0] == TTYPE && sub_[1] == 1 && local_[TTYPE] == 2) {
        const uint8_t type[] = {IAC, SB, TTYPE, 0, 'A', 'N', 'S', 'I', IAC, SE};
        for (uint8_t b : type) {
            tx.put(b);
        }
    }
}
void Telnet::feed(uint8_t b) {
    switch (state_) {
        case 0:
            if (b == IAC) {
                state_ = 1;
            } else {
                rx.put(b);
            }
            break;
        case 1:
            if (b == IAC) {
                rx.put(IAC);
                state_ = 0;
            } else if (b == WILL || b == WONT || b == DO || b == DONT) {
                cmd_ = b;
                state_ = 2;
            } else if (b == SB) {
                subLen_ = 0;
                subOverflow_ = false;
                state_ = 3;
            } else {
                state_ = 0;
            }
            break;
        case 2:
            negotiate(b);
            state_ = 0;
            break;
        case 3:
            if (b == IAC) {
                state_ = 4;
            } else if (subLen_ < sizeof sub_) {
                sub_[subLen_++] = b;
            } else {
                subOverflow_ = true;
            }
            break;
        case 4:
            if (b == SE) {
                subnegotiation();
                state_ = 0;
            } else {
                if (b == IAC && subLen_ < sizeof sub_) {
                    sub_[subLen_++] = b;
                } else {
                    subOverflow_ = true;
                }
                state_ = 3;
            }
            break;
        default:
            state_ = 0;
            break;
    }
}
size_t Telnet::write(const uint8_t *data, size_t len) {
    size_t n = len < space() ? len : space();
    for (size_t i = 0; i < n; ++i) {
        tx.put(data[i]);
        if (data[i] == IAC) {
            tx.put(IAC);
        }
    }
    return n;
}
}  // namespace calls
