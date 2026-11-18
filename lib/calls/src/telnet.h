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
// Adapted from Zimodem's handleAsciiIAC (pet2asc.ino), Copyright 2016-2026 Bo Zimmerman.
// Apache-2.0, see https://github.com/bozimmerman/Zimodem.
// Changed: incremental bounded parsing, BINARY both
// directions, live NAWS dimensions, ANSI terminal type, and negotiation state to avoid loops.
#pragma once
#include "byte_queue.h"

namespace calls {
class Telnet {
public:
    void begin();
    void resize(unsigned columns, unsigned rows);
    void poll();
    void feed(uint8_t byte);
    size_t write(const uint8_t *data, size_t len);
    size_t space() const { return tx.space() > 64 ? (tx.space() - 64) / 2 : 0; }
    ByteQueue<8192> rx, tx;

private:
    void command(uint8_t cmd, uint8_t option);
    void negotiate(uint8_t option);
    void subnegotiation();
    uint8_t local_[256] = {}, remote_[256] = {};
    uint8_t state_ = 0, cmd_ = 0;
    uint8_t sub_[32] = {};
    size_t subLen_ = 0;
    bool subOverflow_ = false;
    unsigned columns_ = 80, rows_ = 24;
    bool sizePending_ = false;
};
}  // namespace calls
