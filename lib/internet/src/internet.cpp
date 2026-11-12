// PPP/SLIP framing adapted from Zimodem, Copyright 2022-2026 Bo Zimmerman.
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
// Modified for USR Modem: all negotiation, bounds, private addressing and teardown.
#include "internet.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include "net_settings.h"

namespace dialup {
namespace {
constexpr uint16_t kLcp = 0xc021, kIpcp = 0x8021, kIp = 0x0021, kPap = 0xc023;
uint16_t be16(const uint8_t *p) {
    return uint16_t(p[0]) << 8 | p[1];
}
uint32_t be32(const uint8_t *p) {
    return uint32_t(p[0]) << 24 | uint32_t(p[1]) << 16 | uint32_t(p[2]) << 8 | p[3];
}
void put32(uint8_t *p, uint32_t n) {
    for (unsigned i = 0; i < 4; ++i)
        p[i] = uint8_t(n >> (24 - i * 8));
}
}  // namespace
const char *reasonName(Reason reason) {
    static const char *const names[] = {"none",          "local hang-up", "peer termination",
                                        "timeout",       "network lost",  "invalid settings",
                                        "queue overflow"};
    return unsigned(reason) < 7 ? names[unsigned(reason)] : "unknown";
}
uint16_t fcs16(const uint8_t *data, size_t length, uint16_t crc) {
    while (length--) {
        crc ^= *data++;
        for (unsigned i = 0; i < 8; ++i)
            crc = (crc >> 1) ^ ((crc & 1) ? 0x8408 : 0);
    }
    return crc;
}
bool validIpv4(const uint8_t *p, size_t n, uint32_t source) {
    if (n < 20 || n > kMtu || p[0] >> 4 != 4 || (p[0] & 15) < 5 || size_t(p[0] & 15) * 4 > n ||
        be16(p + 2) != n || be32(p + 12) != source || (be16(p + 6) & 0x3fff))
        return false;
    uint32_t sum = 0;
    for (unsigned i = 0; i < unsigned(p[0] & 15) * 4; i += 2)
        sum += be16(p + i);
    while (sum >> 16)
        sum = (sum & 0xffff) + (sum >> 16);
    return sum == 0xffff;
}
void Engine::closeNetwork() {
    if (networkOpen_)
        network_.end();
    networkOpen_ = ready_ = false;
}
void Engine::resetFrame() {
    rxLength_ = 0;
    escaped_ = discard_ = false;
}
void Engine::close() {
    closeNetwork();
    open_ = closing_ = false;
    tx_.clear();
    resetFrame();
    // PAP's receive buffer must not retain even discarded credentials.
    netcfg::wipe(reinterpret_cast<char *>(rx_), sizeof rx_);
}
void Engine::stop(Reason reason) {
    close();
    reason_ = reason;
}
bool Engine::begin(Protocol protocol, const Options &options, uint32_t now) {
    close();
    reason_ = Reason::None;
    if (!validOptions(options) || !options.dns) {
        reason_ = Reason::Invalid;
        return false;
    }
    protocol_ = protocol;
    options_ = options;
    now_ = started_ = retryAt_ = now;
    sent_ = received_ = dropped_ = 0;
    peerMru_ = kMtu;
    retries_ = 0;
    lcpId_ = ipcpId_ = 1;
    lcpLocal_ = lcpPeer_ = ipcpLocal_ = ipcpPeer_ = ipcpStarted_ = false;
    peerPfc_ = peerAcfc_ = false;
    txAccm_ = rxAccm_ = 0xffffffff;
    open_ = true;
    if (protocol == Protocol::Slip) {
        networkOpen_ = network_.begin(options_, kMtu);
        ready_ = networkOpen_;
        if (!ready_)
            stop(Reason::Network);
        return open_;
    }
    const uint8_t request[] = {1, 4, 5, 220, 2, 6, 0, 0, 0, 0, 5, 6, 0, 0, 0, 0};
    memcpy(lcpRequest_, request, sizeof request);
    put32(lcpRequest_ + 12, now ^ 0x55535231u);
    lcpLength_ = sizeof request;
    ipcpRequest_[0] = 3;
    ipcpRequest_[1] = 6;
    put32(ipcpRequest_ + 2, options_.local);
    this->request(false);
    return true;
}
bool Engine::frame(uint16_t protocol, const uint8_t *data, size_t length) {
    if (length > kMtu || tx_.space() < 2 * (length + 6) + 2) {
        ++dropped_;
        return false;
    }
    if (protocol_ == Protocol::Slip) {
        tx_.put(0xc0);
        for (size_t i = 0; i < length; ++i) {
            const uint8_t b = data[i];
            if (b == 0xc0 || b == 0xdb) {
                tx_.put(0xdb);
                tx_.put(b == 0xc0 ? 0xdc : 0xdd);
            } else
                tx_.put(b);
        }
        tx_.put(0xc0);
        return true;
    }
    uint16_t crc = 0xffff;
    auto emit = [&](uint8_t b) {
        if (b == 0x7e || b == 0x7d || (b < 32 && (txAccm_ & (1u << b)))) {
            tx_.put(0x7d);
            tx_.put(b ^ 0x20);
        } else
            tx_.put(b);
    };
    auto body = [&](uint8_t b) {
        crc = fcs16(&b, 1, crc);
        emit(b);
    };
    tx_.put(0x7e);
    body(0xff);
    body(3);
    body(uint8_t(protocol >> 8));
    body(uint8_t(protocol));
    for (size_t i = 0; i < length; ++i)
        body(data[i]);
    crc ^= 0xffff;
    emit(uint8_t(crc));
    emit(uint8_t(crc >> 8));
    tx_.put(0x7e);
    return true;
}
void Engine::control(uint16_t protocol, uint8_t code, uint8_t id, const uint8_t *data,
                     size_t length) {
    if (length > kMtu - 4) {
        ++dropped_;
        return;
    }
    uint8_t packet[kMtu];
    packet[0] = code;
    packet[1] = id;
    packet[2] = uint8_t((length + 4) >> 8);
    packet[3] = uint8_t(length + 4);
    if (length)
        memcpy(packet + 4, data, length);
    if (!frame(protocol, packet, length + 4))
        stop(Reason::Overflow);
}
void Engine::request(bool ipc) {
    control(ipc ? kIpcp : kLcp, 1, ipc ? ipcpId_ : lcpId_, ipc ? ipcpRequest_ : lcpRequest_,
            ipc ? sizeof ipcpRequest_ : lcpLength_);
    retryAt_ = now_;
}
void Engine::advance() {
    if (!open_ || closing_ || !lcpLocal_ || !lcpPeer_)
        return;
    if (!ipcpStarted_) {
        ipcpStarted_ = true;
        retries_ = 0;
        request(true);
    }
    if (!ready_ && ipcpLocal_ && ipcpPeer_) {
        networkOpen_ = network_.begin(options_, peerMru_);
        ready_ = networkOpen_;
        if (!ready_)
            stop(Reason::Network);
    }
}
void Engine::lcp(const uint8_t *p, size_t n) {
    const uint8_t code = p[0], id = p[1];
    const uint8_t *data = p + 4;
    const size_t length = n - 4;
    if (code == 1) {
        uint8_t reject[kMtu], nak[32];
        size_t rejected = 0, naks = 0;
        uint32_t accm = 0xffffffff;
        uint16_t mru = kMtu;
        bool pfc = false, acfc = false;
        for (size_t i = 0; i < length;) {
            if (length - i < 2 || data[i + 1] < 2 || data[i + 1] > length - i)
                return;
            const size_t size = data[i + 1];
            bool accept = true;
            switch (data[i]) {
                case 1:
                    accept = size == 4;
                    if (accept) {
                        mru = be16(data + i + 2);
                        if (mru < 128 || mru > kMtu) {
                            if (naks + 4 > sizeof nak)
                                return;
                            const uint8_t suggestion[] = {1, 4, 5, 220};
                            memcpy(nak + naks, suggestion, 4);
                            naks += 4;
                        }
                    }
                    break;
                case 2:
                    accept = size == 6;
                    if (accept)
                        accm = be32(data + i + 2);
                    break;
                case 5:
                    accept = size == 6;
                    break;
                case 7:
                    accept = size == 2;
                    pfc = accept;
                    break;
                case 8:
                    accept = size == 2;
                    acfc = accept;
                    break;
                default:
                    accept = false;
                    break;
            }
            if (!accept) {
                memcpy(reject + rejected, data + i, size);
                rejected += size;
            }
            i += size;
        }
        if (ready_) {
            closeNetwork();
            started_ = now_;
            retries_ = 0;
            lcpLocal_ = ipcpLocal_ = ipcpPeer_ = ipcpStarted_ = false;
            ++lcpId_;
            request(false);
        }
        lcpPeer_ = !rejected && !naks;
        control(kLcp,
                rejected ? 4
                : naks   ? 3
                         : 2,
                id,
                rejected ? reject
                : naks   ? nak
                         : data,
                rejected ? rejected
                : naks   ? naks
                         : length);
        if (lcpPeer_) {
            txAccm_ = accm;
            peerMru_ = mru;
            peerPfc_ = pfc;
            peerAcfc_ = acfc;
        }
    } else if (code == 2 && id == lcpId_ && length == lcpLength_ &&
               !memcmp(data, lcpRequest_, length)) {
        lcpLocal_ = true;
        rxAccm_ = 0xffffffff;
        for (size_t i = 0; i < lcpLength_; i += lcpRequest_[i + 1])
            if (lcpRequest_[i] == 2)
                rxAccm_ = be32(lcpRequest_ + i + 2);
    } else if ((code == 3 || code == 4) && id == lcpId_ && !lcpLocal_) {
        // Remove rejected options, or accept bounded MRU/ACCM suggestions only.
        for (size_t i = 0; i < length;) {
            if (length - i < 2 || data[i + 1] < 2 || data[i + 1] > length - i)
                return;
            for (size_t j = 0; j < lcpLength_; j += lcpRequest_[j + 1]) {
                if (lcpRequest_[j] != data[i])
                    continue;
                const size_t size = lcpRequest_[j + 1];
                if (code == 4 && size == data[i + 1] && !memcmp(lcpRequest_ + j, data + i, size)) {
                    memmove(lcpRequest_ + j, lcpRequest_ + j + size, lcpLength_ - j - size);
                    lcpLength_ -= size;
                } else if (code == 3 && size == data[i + 1]) {
                    if (data[i] == 1 && (be16(data + i + 2) < 128 || be16(data + i + 2) > kMtu))
                        return;
                    if (data[i] == 1 || data[i] == 2 || data[i] == 5)
                        memcpy(lcpRequest_ + j, data + i, size);
                }
                break;
            }
            i += data[i + 1];
        }
        if (++retries_ > 10) {
            stop(Reason::Timeout);
            return;
        }
        ++lcpId_;
        request(false);
    } else if (code == 5) {
        control(kLcp, 6, id, data, length);
        closeNetwork();
        closing_ = true;
        closeAt_ = now_;
        reason_ = Reason::Peer;
    } else if (code == 9 && lcpLocal_ && lcpPeer_ && length >= 4) {
        uint8_t echo[kMtu];
        memcpy(echo, data, length);
        memset(echo, 0, 4);
        for (size_t i = 0; i < lcpLength_; i += lcpRequest_[i + 1])
            if (lcpRequest_[i] == 5)
                memcpy(echo, lcpRequest_ + i + 2, 4);
        control(kLcp, 10, id, echo, length);
    } else if (code != 6 && code != 7 && code != 8 && code != 10 && code != 11 && code != 2 &&
               code != 3 && code != 4) {
        control(kLcp, 7, id, p, std::min(n, size_t(peerMru_ - 4)));
    }
    advance();
}
void Engine::ipcp(const uint8_t *p, size_t n) {
    if (!lcpLocal_ || !lcpPeer_)
        return;
    const uint8_t *data = p + 4;
    const size_t length = n - 4;
    if (p[0] == 1) {
        uint8_t reject[kMtu], nak[kMtu];
        size_t rejected = 0, naks = 0;
        bool address = false;
        for (size_t i = 0; i < length;) {
            if (length - i < 2 || data[i + 1] < 2 || data[i + 1] > length - i)
                return;
            const size_t size = data[i + 1];
            const uint8_t type = data[i];
            if (size == 6 && (type == 3 || type == 129 || type == 131)) {
                const uint32_t expected = type == 3 ? options_.peer : options_.dns;
                address |= type == 3;
                if (be32(data + i + 2) != expected) {
                    nak[naks++] = type;
                    nak[naks++] = 6;
                    put32(nak + naks, expected);
                    naks += 4;
                }
            } else {
                memcpy(reject + rejected, data + i, size);
                rejected += size;
            }
            i += size;
        }
        if (!address) {
            if (naks + 6 > sizeof nak)
                return;
            nak[naks++] = 3;
            nak[naks++] = 6;
            put32(nak + naks, options_.peer);
            naks += 4;
        }
        ipcpPeer_ = !rejected && !naks;
        control(kIpcp,
                rejected ? 4
                : naks   ? 3
                         : 2,
                p[1],
                rejected ? reject
                : naks   ? nak
                         : data,
                rejected ? rejected
                : naks   ? naks
                         : length);
    } else if (p[0] == 2 && p[1] == ipcpId_ && length == sizeof ipcpRequest_ &&
               !memcmp(data, ipcpRequest_, length)) {
        ipcpLocal_ = true;
    } else if ((p[0] == 3 || p[0] == 4) && p[1] == ipcpId_) {
        // Our local address is fixed; never adopt the client's proposed address.
        if (++retries_ > 10)
            stop(Reason::Timeout);
        else {
            ++ipcpId_;
            request(true);
        }
    } else if (p[0] == 5) {
        control(kIpcp, 6, p[1], data, length);
        // Only a link that was up gets a fresh deadline to renegotiate. Before that, the
        // 30 s run from the start, so a peer cannot keep a half-open link for ever (5.5-09).
        const bool wasUp = ready_;
        closeNetwork();
        if (wasUp) {
            started_ = now_;
            retries_ = 0;
        }
        ipcpLocal_ = ipcpPeer_ = false;
        ++ipcpId_;
        request(true);
    }
    advance();
}
void Engine::packet() {
    if (discard_ || escaped_ || !rxLength_) {
        if (discard_ || escaped_)
            ++dropped_;
        return;
    }
    const uint8_t *p = rx_;
    size_t length = rxLength_;
    uint16_t protocol = kIp;
    if (protocol_ == Protocol::Ppp) {
        if (length < 4 || fcs16(p, length) != 0xf0b8) {
            ++dropped_;
            return;
        }
        length -= 2;
        if (length >= 2 && p[0] == 0xff && p[1] == 3) {
            p += 2;
            length -= 2;
        } else if (!peerAcfc_) {
            ++dropped_;
            return;
        }
        if (!length)
            return;
        if (*p & 1) {
            if (!peerPfc_)
                return;
            protocol = *p++;
            --length;
        } else {
            if (length < 2)
                return;
            protocol = be16(p);
            p += 2;
            length -= 2;
        }
    }
    if (protocol == kIp) {
        if (ready_ && validIpv4(p, length, options_.peer) && network_.send(p, length))
            ++sent_;
        else
            ++dropped_;
        return;
    }
    if (length < 4 || be16(p + 2) < 4 || be16(p + 2) > length) {
        ++dropped_;
        return;
    }
    length = be16(p + 2);
    if (protocol == kLcp)
        lcp(p, length);
    else if (protocol == kIpcp)
        ipcp(p, length);
    else if (protocol == kPap && lcpLocal_ && lcpPeer_ && p[0] == 1) {
        // Validate both length-prefixed fields; do not copy or retain credentials.
        if (length >= 6 && size_t(p[4]) + 6 <= length &&
            size_t(p[4]) + size_t(p[5 + p[4]]) + 6 == length) {
            const uint8_t accepted[] = {0};
            control(kPap, 2, p[1], accepted, 1);
        } else
            ++dropped_;
    } else if (lcpLocal_ && lcpPeer_ && protocol != kPap) {
        uint8_t reject[kMtu];
        reject[0] = uint8_t(protocol >> 8);
        reject[1] = uint8_t(protocol);
        length = std::min(length, size_t(peerMru_ - 6));
        memcpy(reject + 2, p, length);
        control(kLcp, 8, lcpId_, reject, length + 2);
    }
}
size_t Engine::write(const uint8_t *data, size_t length) {
    size_t used = 0;
    while (used < length && space()) {
        uint8_t b = data[used++];
        const bool ppp = protocol_ == Protocol::Ppp;
        if (b == (ppp ? 0x7e : 0xc0)) {
            packet();
            // Erase PAP and all other received data once consumed.
            netcfg::wipe(reinterpret_cast<char *>(rx_), sizeof rx_);
            resetFrame();
            continue;
        }
        if (discard_)
            continue;
        if (escaped_) {
            escaped_ = false;
            if (ppp)
                b ^= 0x20;
            else if (b == 0xdc || b == 0xdd)
                b = b == 0xdc ? 0xc0 : 0xdb;
            else {
                discard_ = true;
                continue;
            }
        } else if (b == (ppp ? 0x7d : 0xdb)) {
            escaped_ = true;
            continue;
        } else if (ppp && b < 32 && (rxAccm_ & (1u << b)))
            continue;
        if (rxLength_ < sizeof rx_)
            rx_[rxLength_++] = b;
        else
            discard_ = true;
    }
    return used;
}
size_t Engine::read(uint8_t *data, size_t capacity) {
    return tx_.read(data, capacity);
}
void Engine::poll(uint32_t now) {
    if (!open_)
        return;
    // The main loop can sample time before an AT command calls begin(). Do not
    // treat that slightly older tick as an unsigned 49-day negotiation timeout.
    if (int32_t(now - now_) < 0)
        return;
    now_ = now;
    if (closing_) {
        if (!tx_.size() || now - closeAt_ >= 1000)
            stop(reason_);
        return;
    }
    if (ready_ && !network_.up()) {
        stop(Reason::Network);
        return;
    }
    if (!ready_ && now - started_ >= 30000) {
        stop(Reason::Timeout);
        return;
    }
    if (!ready_ && now - retryAt_ >= 3000) {
        if (++retries_ > 10)
            stop(Reason::Timeout);
        else if (!lcpLocal_ || !lcpPeer_)
            request(false);
        else
            request(true);
    }
    for (unsigned i = 0; ready_ && i < 4 && tx_.space() >= 4096; ++i) {
        const size_t n = network_.receive(ip_, sizeof ip_);
        if (!n)
            break;
        if (n <= peerMru_ && frame(kIp, ip_, n))
            ++received_;
        else
            ++dropped_;
    }
}
}  // namespace dialup
