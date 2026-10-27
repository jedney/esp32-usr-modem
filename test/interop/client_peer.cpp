// POSIX test adapter for the production portable transfer core. No protocol logic here.
#include <sys/select.h>
#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include "transfer.h"
class Generated : public clienttest::Source {
public:
    unsigned files = 1;
    uint32_t length = 8193;
    unsigned count() const override { return files; }
    const char *name(unsigned file) const override { return file ? "SECOND.BIN" : "TEST.BIN"; }
    uint32_t size(unsigned) const override { return length; }
    size_t read(unsigned, uint32_t pos, uint8_t *out, size_t cap) override {
        cap = std::min<uint32_t>(cap, length - pos);
        for (size_t i = 0; i < cap; ++i)
            out[i] = (pos + i) & 255;
        return cap;
    }
};
int main(int argc, char **argv) {
    if (argc != 5)
        return 2;
    clienttest::Transfer transfer;
    Generated source;
    source.length = strtoul(argv[3], nullptr, 10);
    source.files = strtoul(argv[4], nullptr, 10);
    bool sending = !strcmp(argv[1], "send");
    auto start = std::chrono::steady_clock::now();
    if (!transfer.start(clienttest::Protocol(atoi(argv[2])), sending, &source, 0))
        return 2;
    while (transfer.running() || transfer.pending()) {
        uint32_t ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::steady_clock::now() - start)
                          .count();
        transfer.poll(ms);
        uint8_t data[4096];
        size_t n = transfer.read(data, sizeof data);
        for (size_t sent = 0; sent < n;) {
            ssize_t count = ::write(1, data + sent, n - sent);
            if (count <= 0)
                return 3;
            sent += count;
        }
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(0, &fds);
        timeval wait = {0, 1000};
        if (transfer.space() && select(1, &fds, nullptr, nullptr, &wait) > 0) {
            ssize_t got = ::read(0, data, std::min(sizeof data, transfer.space()));
            if (got > 0)
                transfer.write(data, got);
        }
    }
    const auto &s = transfer.statistics();
    fprintf(stderr, "outcome=%u bytes=%u files=%u crc=%08X retries=%u errors=%u\n",
            unsigned(s.outcome), s.bytes, s.files, ~s.crc, s.retries, s.errors);
    return s.outcome == clienttest::Outcome::Success ? 0 : 1;
}
