// Linux stdio peer for independent pppd tests; no Arduino or network SDK.
#include <poll.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <vector>
#include "internet.h"

uint16_t checksum(const uint8_t *bytes, size_t length) {
    uint32_t sum = 0;
    for (size_t i = 0; i < length; i += 2)
        sum += uint32_t(bytes[i]) * 256 + (i + 1 < length ? bytes[i + 1] : 0);
    while (sum >> 16)
        sum = (sum & 65535) + (sum >> 16);
    return uint16_t(~sum);
}
class EchoNetwork : public dialup::Network {
public:
    bool begin(const dialup::Options &, uint16_t) override {
        live_ = true;
        return true;
    }
    void end() override {
        live_ = false;
        packets_.clear();
    }
    bool up() const override { return live_; }
    bool send(const uint8_t *bytes, size_t length) override {
        if (!live_ || packets_.size() >= 4 || length < 28)
            return false;
        std::vector<uint8_t> packet(bytes, bytes + length);
        size_t header = (packet[0] & 15) * 4;
        if (header != 20 || length < header + 8)
            return false;
        if (packet[9] == 17) {
            std::swap(packet[header], packet[header + 2]);
            std::swap(packet[header + 1], packet[header + 3]);
            packet[header + 6] = packet[header + 7] = 0;  // IPv4 UDP permits no checksum.
        } else if (packet[9] == 1 && packet[header] == 8) {
            packet[header] = 0;
            packet[header + 2] = packet[header + 3] = 0;
            uint16_t sum = checksum(packet.data() + header, length - header);
            packet[header + 2] = sum >> 8;
            packet[header + 3] = sum;
        } else
            return false;
        for (unsigned i = 0; i < 4; ++i)
            std::swap(packet[12 + i], packet[16 + i]);
        packet[10] = packet[11] = 0;
        uint16_t sum = checksum(packet.data(), header);
        packet[10] = sum >> 8;
        packet[11] = sum;
        packets_.push_back(packet);
        return true;
    }
    size_t receive(uint8_t *bytes, size_t capacity) override {
        if (packets_.empty() || packets_.front().size() > capacity)
            return 0;
        auto packet = packets_.front();
        packets_.pop_front();
        memcpy(bytes, packet.data(), packet.size());
        return packet.size();
    }

private:
    bool live_ = false;
    std::deque<std::vector<uint8_t>> packets_;
};
int main() {
    EchoNetwork network;
    dialup::Engine engine(network);
    dialup::Options options;
    options.dns = 0x08080808;
    engine.begin(dialup::Protocol::Ppp, options, 0);
    const auto start = std::chrono::steady_clock::now();
    uint8_t buffer[512];
    while (engine.isOpen()) {
        uint32_t elapsed = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                                        std::chrono::steady_clock::now() - start)
                                        .count());
        if (elapsed > 60000)
            return 2;
        size_t length;
        while ((length = engine.read(buffer, sizeof buffer))) {
            size_t offset = 0;
            while (offset < length) {
                ssize_t sent = ::write(STDOUT_FILENO, buffer + offset, length - offset);
                if (sent <= 0)
                    return 3;
                offset += size_t(sent);
            }
        }
        pollfd input{STDIN_FILENO, POLLIN, 0};
        if (::poll(&input, 1, 5) > 0 && (input.revents & POLLIN) && engine.space()) {
            ssize_t received =
                ::read(STDIN_FILENO, buffer, std::min(sizeof buffer, engine.space()));
            if (received <= 0)
                return 4;
            if (engine.write(buffer, size_t(received)) != size_t(received))
                return 5;
        }
        engine.poll(elapsed);
    }
    return 0;
}
