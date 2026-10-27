#pragma once
#include "client_settings.h"
#include "connection.h"
#include "transfer.h"

namespace clienttest {
class Platform {
public:
    virtual ~Platform() = default;
    virtual Source *source(const char *selection, uint32_t generatedSize) = 0;
    virtual void release() = 0;
    virtual uint32_t baud() const = 0;
    virtual bool rts() const = 0;
    virtual bool dtr() const = 0;
    virtual void signal(unsigned which, bool on) = 0;  // DCD, RI, CTS
    virtual void restore() = 0;
    virtual bool drained() const = 0;
};
class Service : public hostport::Connection {
public:
    Service(Settings &settings, unsigned port, Platform &platform)
        : settings_(settings), port_(port), platform_(platform), findings_(settings.get(port)) {}
    bool start(const char *command, const char *args, uint32_t now);
    void hold(bool value) { hold_ = value; }
    bool isOpen() const override { return open_; }
    bool localService() const override { return true; }
    bool binaryService() const override { return binary(); }
    void close() override;
    void poll(uint32_t now) override;
    size_t space() const override;
    size_t write(const uint8_t *data, size_t len) override;
    size_t read(uint8_t *out, size_t cap) override;
    bool changed() {
        bool result = changed_;
        changed_ = false;
        return result;
    }
    bool cable() const { return mode_ == Mode::Cable && open_; }
    bool binary() const { return mode_ == Mode::Transfer && !finished_; }
    const ClientSettings &findings() const { return findings_; }
    const Statistics &statistics() const { return transfer_.statistics(); }

private:
    enum class Mode : uint8_t {
        Output,
        Observe,
        Size,
        ManualSize,
        Keys,
        Id,
        Echo,
        Tx,
        Rx,
        Cable,
        Confirm,
        Transfer
    };
    void text(const char *text);
    void format(const char *format, ...) __attribute__((format(printf, 2, 3)));
    void complete(const char *result);
    void summary();
    void byte(uint8_t b);
    void transferResult();
    Settings &settings_;
    unsigned port_;
    Platform &platform_;
    ClientSettings findings_;
    Charset beforeDisplay_ = Charset::Ansi;
    unsigned displaySlot_ = 4;
    Transfer transfer_;
    calls::ByteQueue<16384> output_;
    Mode mode_ = Mode::Output;
    bool open_ = false, finished_ = false, changed_ = false, restore_ = false;
    bool hold_ = false;
    bool protocolEnded_ = false;
    uint32_t protocolEndedAt_ = 0;
    bool lastRts_ = true, lastDtr_ = true, observed_ = false;
    uint32_t now_ = 0, start_ = 0, last_ = 0, finishedAt_ = 0, duration_ = 5000, bytes_ = 0,
             pauses_ = 0, pausedMs_ = 0;
    unsigned cableStep_ = 0, length_ = 0;
    char input_[96] = {};
    char observations_[12][64] = {};
};
}  // namespace clienttest
