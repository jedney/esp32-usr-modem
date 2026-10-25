#pragma once
#include "tones.h"

namespace sounds {
class Reader {
public:
    virtual ~Reader() = default;
    virtual size_t read(uint8_t *data, size_t len) = 0;
    virtual bool seek(uint32_t offset) = 0;
    virtual uint32_t size() const = 0;
};
struct WavInfo {
    uint32_t offset = 0, bytes = 0;
};
bool inspectWav(Reader &reader, WavInfo &info);
uint32_t handshakeSpeed(uint32_t speed);
// 8 kHz PCM input, doubled to the common 16 kHz I2S rate. Volume is applied by hw.
class WavSource : public tones::Source {
public:
    explicit WavSource(Reader &reader) : reader_(reader) {}
    bool begin();
    size_t fill(int16_t *out, size_t n) override;
    bool failed() const { return failed_; }

private:
    Reader &reader_;
    uint32_t left_ = 0;
    uint8_t cache_[256] = {};
    size_t pos_ = 0, count_ = 0;
    int16_t last_ = 0;
    bool repeat_ = false, failed_ = false;
};
}  // namespace sounds
