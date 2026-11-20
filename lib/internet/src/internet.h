// PPP/SLIP framing adapted from Zimodem, Copyright 2022-2026 Bo Zimmerman
// (revision a534e5129d88b8b09aa01fe17498d5cd5368569e, zpppmode/zslipmode).
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy at http://www.apache.org/licenses/LICENSE-2.0
// Unless required by applicable law or agreed to in writing, software distributed
// under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
// CONDITIONS OF ANY KIND, either express or implied. See the License for the
// specific language governing permissions and limitations under the License.
// Modified for USR Modem: bounded portable codecs, RFC negotiation and lifecycle.
#pragma once
#include "byte_queue.h"
#include "connection.h"
#include "internet_settings.h"

namespace dialup {
constexpr size_t kMtu = 1500;
enum class Protocol : uint8_t { Ppp, Slip };
enum class Reason : uint8_t { None, Local, Peer, Timeout, Network, Invalid, Overflow };
const char *reasonName(Reason reason);
class Network {
public:
    virtual ~Network() = default;
    virtual bool begin(const Options &options, uint16_t mtu) = 0;
    virtual void end() = 0;
    virtual bool send(const uint8_t *packet, size_t length) = 0;
    virtual size_t receive(uint8_t *packet, size_t capacity) = 0;
    virtual bool up() const = 0;
};
uint16_t fcs16(const uint8_t *data, size_t length, uint16_t initial = 0xffff);
bool validIpv4(const uint8_t *packet, size_t length, uint32_t source);

class Engine : public hostport::Connection {
public:
    explicit Engine(Network &network) : network_(network) {}
    bool begin(Protocol protocol, const Options &options, uint32_t now);
    void stop(Reason reason);
    bool isOpen() const override { return open_; }
    bool binaryService() const override { return true; }
    // cppcheck follows one fresh queue; its space changes as bytes come and go
    // cppcheck-suppress knownConditionTrueFalse
    size_t space() const override { return open_ && !closing_ && tx_.space() >= 4096 ? 256 : 0; }
    size_t write(const uint8_t *data, size_t length) override;
    size_t read(uint8_t *data, size_t capacity) override;
    void close() override;
    void poll(uint32_t now) override;
    bool ready() const { return ready_; }
    Protocol protocol() const { return protocol_; }
    Reason reason() const { return reason_; }
    uint32_t sent() const { return sent_; }
    uint32_t received() const { return received_; }
    uint32_t dropped() const { return dropped_; }
    uint32_t startedAt() const { return started_; }

private:
    bool frame(uint16_t protocol, const uint8_t *data, size_t length);
    void control(uint16_t protocol, uint8_t code, uint8_t id, const uint8_t *data, size_t length);
    void packet();
    void lcp(const uint8_t *data, size_t length);
    void ipcp(const uint8_t *data, size_t length);
    void request(bool ipcp);
    void advance();
    void resetFrame();
    void closeNetwork();
    Network &network_;
    Options options_;
    calls::ByteQueue<16384> tx_;
    uint8_t rx_[kMtu + 8] = {}, ip_[kMtu] = {};
    // Replies are built here, not on the stack: the Arduino loop task, which runs the engine,
    // had 420 bytes of its 8 KB left after the PPP tests. control() copies
    // reply_ or nak_ into packet_, so the three never overlap.
    uint8_t packet_[kMtu] = {}, reply_[kMtu] = {}, nak_[kMtu] = {};
    size_t rxLength_ = 0;
    uint8_t lcpRequest_[32] = {}, ipcpRequest_[6] = {};
    size_t lcpLength_ = 0;
    uint32_t now_ = 0, started_ = 0, retryAt_ = 0, closeAt_ = 0;
    uint32_t txAccm_ = 0xffffffff, rxAccm_ = 0xffffffff;
    uint32_t sent_ = 0, received_ = 0, dropped_ = 0;
    uint16_t peerMru_ = kMtu;
    uint8_t lcpId_ = 1, ipcpId_ = 1, retries_ = 0;
    Protocol protocol_ = Protocol::Ppp;
    Reason reason_ = Reason::None;
    bool open_ = false, ready_ = false, closing_ = false, networkOpen_ = false;
    bool escaped_ = false, discard_ = false, lcpLocal_ = false, lcpPeer_ = false;
    bool ipcpLocal_ = false, ipcpPeer_ = false, ipcpStarted_ = false;
    bool peerPfc_ = false, peerAcfc_ = false;
};
}  // namespace dialup
