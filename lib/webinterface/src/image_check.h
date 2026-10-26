#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
namespace webinterface {
// Identity is an embedded project marker, not a signature. Admin may install
// unsigned project releases. Integrity is verified by the adapter's SHA-256.
class ImageCheck {
public:
    ImageCheck(bool filesystem, const std::string &version, size_t length);
    bool write(const uint8_t *data, size_t len);
    bool complete() const;

private:
    bool fs_, valid_;
    size_t length_, received_ = 0;
    std::string marker_, tail_;
    bool found_ = false;
    uint8_t header_[24] = {};
};
}  // namespace webinterface
