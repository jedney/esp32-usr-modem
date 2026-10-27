#include "client_platform.h"
#include <algorithm>
#include <cstring>
namespace hw {
namespace {
const char *const names[] = {"USRMODEM.LOG", "HS1200.WAV",  "HS2400.WAV",  "HS9600.WAV",
                             "HS14400.WAV",  "HS28800.WAV", "HS33600.WAV", "TEST.BIN"};
const char *const paths[] = {"",
                             "/sounds/handshake-1200.wav",
                             "/sounds/handshake-2400.wav",
                             "/sounds/handshake-9600.wav",
                             "/sounds/handshake-14400.wav",
                             "/sounds/handshake-28800.wav",
                             "/sounds/handshake-33600.wav"};
}  // namespace
clienttest::Source *ClientPlatform::source(const char *selection, uint32_t generatedSize) {
    release();
    selected_ = 8;
    if (!strcmp(selection, "ALL")) {
        selected_ = 0;
        count_ = 7;
    } else {
        if (!strcmp(selection, "LOG"))
            selected_ = 0;
        else if (!strcmp(selection, "TEST"))
            selected_ = 7;
        else
            for (unsigned i = 0; i < 7; ++i)
                if (!strcmp(selection, names[i]))
                    selected_ = i;
        count_ = 1;
    }
    if (selected_ == 8)
        return nullptr;
    generatedSize_ = generatedSize;
    if (!selected_) {
        snapshotSize_ = log_.size();
        snapshot_ = static_cast<char *>(ps_malloc(snapshotSize_ ? snapshotSize_ : 1));
        if (!snapshot_)
            return nullptr;
        for (size_t i = 0; i < snapshotSize_; ++i)
            snapshot_[i] = log_.at(i);
    }
    for (unsigned i = 0; i < count_; ++i) {
        unsigned index = selected_ + i;
        if (!index)
            sizes_[i] = snapshotSize_;
        else if (index == 7)
            sizes_[i] = generatedSize_;
        else {
            File file = LittleFS.open(paths[index], "r");
            if (!file) {
                release();
                return nullptr;
            }
            sizes_[i] = file.size();
        }
    }
    return this;
}
void ClientPlatform::release() {
    free(snapshot_);
    snapshot_ = nullptr;
    count_ = snapshotSize_ = 0;
}
const char *ClientPlatform::name(unsigned file) const {
    unsigned index = selected_ + file;
    if (index == 7)
        return generatedSize_ == 1048576 ? "TEST1M.BIN"
               : generatedSize_ == 65536 ? "TEST64K.BIN"
                                         : "TEST.BIN";
    return index < 7 ? names[index] : "TEST.BIN";
}
uint32_t ClientPlatform::size(unsigned file) const {
    return file < count_ ? sizes_[file] : 0;
}
size_t ClientPlatform::read(unsigned file, uint32_t offset, uint8_t *out, size_t cap) {
    if (file >= count_ || offset > sizes_[file])
        return 0;
    cap = std::min<size_t>(cap, sizes_[file] - offset);
    unsigned index = selected_ + file;
    if (!index)
        memcpy(out, snapshot_ + offset, cap);
    else if (index == 7)
        for (size_t i = 0; i < cap; ++i)
            out[i] = (offset + i) & 255;
    else {
        File input = LittleFS.open(paths[index], "r");
        if (!input || !input.seek(offset))
            return 0;
        return input.read(out, cap);
    }
    return cap;
}
}  // namespace hw
