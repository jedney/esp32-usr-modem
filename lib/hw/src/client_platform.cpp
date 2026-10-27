#include "client_platform.h"
#include <algorithm>
#include <cstring>
#include "catalog.h"
namespace hw {
namespace {
constexpr unsigned kGenerated = sounds::kAssetCount + 1;
constexpr unsigned kUnknown = kGenerated + 1;
}  // namespace
clienttest::Source *ClientPlatform::source(const char *selection, uint32_t generatedSize) {
    release();
    selected_ = kUnknown;
    if (!strcmp(selection, "ALL")) {
        selected_ = 0;
        count_ = kGenerated;
    } else {
        if (!strcmp(selection, "LOG"))
            selected_ = 0;
        else if (!strcmp(selection, "TEST"))
            selected_ = kGenerated;
        else if (!strcmp(selection, "USRMODEM.LOG"))
            selected_ = 0;
        else
            for (unsigned i = 0; i < sounds::kAssetCount; ++i)
                if (!strcmp(selection, sounds::kAssets[i].name))
                    selected_ = i + 1;
        count_ = 1;
    }
    if (selected_ == kUnknown)
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
        else if (index == kGenerated)
            sizes_[i] = generatedSize_;
        else {
            File file = LittleFS.open(sounds::kAssets[index - 1].path, "r");
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
    if (index == kGenerated)
        return generatedSize_ == 1048576 ? "TEST1M.BIN"
               : generatedSize_ == 65536 ? "TEST64K.BIN"
                                         : "TEST.BIN";
    return !index               ? "USRMODEM.LOG"
           : index < kGenerated ? sounds::kAssets[index - 1].name
                                : "TEST.BIN";
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
    else if (index == kGenerated)
        for (size_t i = 0; i < cap; ++i)
            out[i] = (offset + i) & 255;
    else {
        File input = LittleFS.open(sounds::kAssets[index - 1].path, "r");
        if (!input || !input.seek(offset))
            return 0;
        return input.read(out, cap);
    }
    return cap;
}
}  // namespace hw
