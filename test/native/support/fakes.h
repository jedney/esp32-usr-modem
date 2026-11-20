// Fakes shared by the PC unit tests. One of each, so a suite cannot drift from the
// others, and every one can fail on demand so the failure paths are testable.
//
//   fakes::MemoryStore    modemcfg::Store held in memory      failSave, failLoad, failErase,
//                                                              failNextSaves
//   fakes::TextSink       atcmd::Sink that collects the text   take(), text; full (unsolicited
//                                                              output dropped), unsolicited
//   fakes::RecordingSound calls::CallSound                     fail (start refuses), broken
//                                                              (failed() reports a fault)
//   fakes::Radio          netcfg::Radio                        up(), fail(), scanOk, scanFail,
//                                                              connectsAtOnce
//   fakes::Socket         calls::Socket held in memory         chunk, room (0: the far end
//                                                              stopped reading), openOnly,
//                                                              initial, lookupResult
//
// Include it from a suite as "../support/fakes.h". Each fake is behind its own macro, so a
// suite only pulls in the library headers it needs: define FAKES_STORE, FAKES_SINK,
// FAKES_SOUND, FAKES_RADIO and/or FAKES_SOCKET before the include.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

#ifdef FAKES_STORE
#include "modemcfg.h"
namespace fakes {
class MemoryStore : public modemcfg::Store {
public:
    std::vector<uint8_t> bytes;
    bool failSave = false;       // save() refuses
    bool failLoad = false;       // load() reports an unreadable store (kLoadError)
    bool failErase = false;      // erase() refuses
    unsigned failNextSaves = 0;  // the next n save() calls refuse, then saving works again
    unsigned loads = 0, saves = 0, erases = 0;
    size_t load(uint8_t *out, size_t cap) override {
        ++loads;
        if (failLoad) {
            return modemcfg::kLoadError;
        }
        const size_t n = std::min(bytes.size(), cap);
        if (n) {
            memcpy(out, bytes.data(), n);
        }
        return n;
    }
    bool save(const uint8_t *in, size_t n) override {
        if (failSave || failNextSaves) {
            if (failNextSaves) {
                --failNextSaves;
            }
            return false;
        }
        bytes.assign(in, in + n);
        ++saves;
        return true;
    }
    bool erase() override {
        ++erases;
        if (failErase) {
            return false;
        }
        bytes.clear();
        return true;
    }
    bool contains(const char *text) const {
        return std::string(bytes.begin(), bytes.end()).find(text) != std::string::npos;
    }
};
}  // namespace fakes
#endif

#ifdef FAKES_SINK
#include "atcmd.h"
namespace fakes {
class TextSink : public atcmd::Sink {
public:
    std::string text;
    std::string unsolicited;  // what came through writeUnsolicited() (RING)
    bool full = false;        // a port nobody reads: unsolicited output is dropped
    void write(const char *data, size_t n) override { text.append(data, n); }
    void writeUnsolicited(const char *data, size_t n) override {
        if (!full) {
            unsolicited.append(data, n);
            text.append(data, n);
        }
    }
    std::string take() {
        std::string result = text;
        text.clear();
        return result;
    }
};
}  // namespace fakes
#endif

#ifdef FAKES_SOUND
#include "outgoing.h"
namespace fakes {
class RecordingSound : public calls::CallSound {
public:
    std::vector<calls::SoundStage> stages;
    std::string digits;
    bool pulse = false;
    bool active = false;
    bool fail = false;    // start() refuses: the sound cannot be played
    bool broken = false;  // failed() reports a fault while a sound runs
    unsigned starts = 0, stops = 0;
    bool start(calls::SoundStage stage, const char *d, bool p, uint32_t,
               const modemcfg::Profile &) override {
        ++starts;
        stages.push_back(stage);
        digits = d ? d : "";
        pulse = p;
        active = !fail;
        return !fail;
    }
    bool running() const override { return active; }
    bool failed() const override { return broken; }
    void stop() override {
        ++stops;
        active = false;
    }
    void poll(uint32_t, const modemcfg::Profile &) override {}
};
}  // namespace fakes
#endif

#ifdef FAKES_RADIO
#include "netcfg.h"
namespace fakes {
// The Wi-Fi radio. connect() starts a join that stays "connecting" until the test calls up()
// or fail(), unless connectsAtOnce. A scan finishes when the test sets scanDone.
class Radio : public netcfg::Radio {
public:
    netcfg::LinkInfo info = {};
    netcfg::Settings last = {};  // the settings of the last connect()
    int connects = 0, leaves = 0, offs = 0, scans = 0, services = 0;
    bool connectsAtOnce = false;  // connect() is up at once
    bool scanOk = true;           // scanStart() is accepted
    bool scanDone = false;        // the scan started last has finished
    bool scanFail = false;        // the finished scan reports an error
    std::vector<netcfg::ScanEntry> found;
    std::string host, ntp;  // what startServices() was given
    int64_t utc = 0, sync = 0;
    void connect(const netcfg::Settings &s) override {
        last = s;
        ++connects;
        if (connectsAtOnce) {
            up();
            return;
        }
        info.state = netcfg::LinkState::kConnecting;
        info.reason = netcfg::Reason::kNone;
    }
    void leave() override {
        ++leaves;
        info.state = netcfg::LinkState::kDown;
        info.reason = netcfg::Reason::kNone;
    }
    void off() override {
        ++offs;
        info.state = netcfg::LinkState::kOff;
    }
    netcfg::LinkInfo link() override { return info; }
    bool scanStart() override {
        ++scans;
        scanDone = false;
        return scanOk;
    }
    int scanResult(netcfg::ScanEntry *out, int max) override {
        if (!scanDone) {
            return -1;
        }
        if (scanFail) {
            return -2;
        }
        int n = 0;
        for (const netcfg::ScanEntry &e : found) {
            if (n < max) {
                out[n++] = e;
            }
        }
        return n;
    }
    void startServices(const netcfg::Settings &s) override {
        ++services;
        host = s.host;
        ntp = s.ntp;
    }
    int64_t utcNow() override { return utc; }
    int64_t lastSync() override { return sync; }
    void mac(uint8_t out[6]) override {
        const uint8_t m[6] = {0x24, 0x6F, 0x28, 0x01, 0x02, 0x03};
        memcpy(out, m, 6);
    }
    void up(uint32_t ip = 0xC0A8012A) {  // 192.168.1.42
        info.state = netcfg::LinkState::kUp;
        info.reason = netcfg::Reason::kNone;
        info.rssi = -54;
        info.channel = 6;
        info.ip = ip;
        info.mask = 0xFFFFFF00;
        info.gateway = 0xC0A80101;
        info.dns = 0xC0A80101;
    }
    void fail(netcfg::Reason r) {
        info.state = netcfg::LinkState::kDown;
        info.reason = r;
    }
};
}  // namespace fakes
#endif

#ifdef FAKES_SOCKET
#include "tcp_connection.h"
namespace fakes {
// A TCP socket in memory. input is what the far end has sent and the modem not yet read;
// output is what the modem has written.
class Socket : public calls::Socket {
public:
    static constexpr size_t kUnlimited = std::numeric_limits<size_t>::max();
    calls::SocketState status = calls::SocketState::kClosed;
    calls::SocketState initial = calls::SocketState::kPending;        // begin() goes here
    calls::SocketState lookupResult = calls::SocketState::kResolved;  // resolve() goes here
    std::string input, output;
    size_t chunk = 256;        // the most one read() or write() moves
    size_t room = kUnlimited;  // what write() still takes; 0: the far end stopped reading
    bool openOnly = false;     // read() and write() move nothing unless status is kOpen
    std::string host, looked;  // what begin() and resolve() were given
    std::string resolvedAddress = "203.0.113.47", peerAddress = "unknown";
    uint16_t port = 0;
    unsigned begins = 0, resolves = 0, closes = 0;
    void begin(const char *h, uint16_t p) override {
        host = h;
        port = p;
        ++begins;
        status = initial;
    }
    void resolve(const char *h) override {
        looked = h;
        ++resolves;
        status = lookupResult;
    }
    const char *address() const override { return resolvedAddress.c_str(); }
    void poll() override {}
    calls::SocketState state() const override { return status; }
    size_t write(const uint8_t *data, size_t n) override {
        if (openOnly && status != calls::SocketState::kOpen) {
            return 0;
        }
        n = std::min({n, chunk, room});
        if (room != kUnlimited) {
            room -= n;
        }
        output.append(reinterpret_cast<const char *>(data), n);
        return n;
    }
    size_t read(uint8_t *data, size_t cap) override {
        if (openOnly && status != calls::SocketState::kOpen) {
            return 0;
        }
        const size_t n = std::min({cap, chunk, input.size()});
        memcpy(data, input.data(), n);
        input.erase(0, n);
        return n;
    }
    void close() override {
        status = calls::SocketState::kClosed;
        input.clear();
        ++closes;
    }
    const char *peer() const override { return peerAddress.c_str(); }
    std::string take() {
        std::string result = output;
        output.clear();
        return result;
    }
};
}  // namespace fakes
#endif
