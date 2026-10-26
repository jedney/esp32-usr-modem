#pragma once
#include <LittleFS.h>
#include <memory>
#include "outgoing.h"
#include "wav.h"

namespace hw {
class FileReader : public sounds::Reader {
public:
    bool open(const char *path) {
        file_ = LittleFS.open(path, "r");
        return bool(file_);
    }
    void close() { file_.close(); }
    size_t read(uint8_t *data, size_t len) override { return file_.read(data, len); }
    bool seek(uint32_t offset) override { return file_.seek(offset); }
    uint32_t size() const override { return file_.size(); }

private:
    File file_;
};
class CallAudio : public calls::CallSound {
public:
    bool start(calls::SoundStage stage, const char *digits, bool pulse, uint32_t speed,
               const modemcfg::Profile &profile) override;
    bool running() const override { return active_; }
    bool failed() const override { return failed_; }
    void stop() override;
    void poll(uint32_t now, const modemcfg::Profile &profile) override;
    bool preview(const char *name, const modemcfg::Profile &profile);

private:
    std::unique_ptr<tones::Source> source_;
    FileReader reader_;
    int16_t samples_[256] = {};
    size_t count_ = 0, sent_ = 0;
    uint32_t since_ = 0;
    bool active_ = false, draining_ = false, eof_ = false;
    bool failed_ = false;
    sounds::WavSource *wav_ = nullptr;
};
}  // namespace hw
