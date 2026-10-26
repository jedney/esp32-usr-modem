#include "image_check.h"
#include <cstring>
#include "web_policy.h"
namespace webinterface {
ImageCheck::ImageCheck(bool filesystem, const std::string &version, size_t length)
    : fs_(filesystem),
      valid_(compareVersions(version, version) == 0 && length >= 24 &&
             length <= (filesystem ? 0x7e0000u : 0x400000u)),
      length_(length),
      marker_(std::string("USRMODEM/1/") + (filesystem ? "LITTLEFS/" : "FIRMWARE/") + version +
              "\n") {}
bool ImageCheck::write(const uint8_t *data, size_t len) {
    if (!valid_ || len > length_ - received_) {
        valid_ = false;
        return false;
    }
    if (received_ < sizeof header_) {
        size_t n = sizeof header_ - received_;
        memcpy(header_ + received_, data, len < n ? len : n);
    }
    std::string text = tail_;
    text.append(reinterpret_cast<const char *>(data), len);
    found_ = found_ || text.find(marker_) != std::string::npos;
    tail_ = text.substr(text.size() > marker_.size() ? text.size() - marker_.size() : 0);
    received_ += len;
    return true;
}
bool ImageCheck::complete() const {
    return valid_ && received_ == length_ && found_ &&
           (fs_ ? length_ == 0x7e0000u
                : header_[0] == 0xe9 && header_[12] == 9 && header_[13] == 0);
}
}  // namespace webinterface
