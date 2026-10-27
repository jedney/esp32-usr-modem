// The six packaged recordings; intermediate playback rates use the lower entry.
#pragma once
#include <cstddef>
#include <cstdint>

namespace sounds {
struct Asset {
    uint32_t speed;
    const char *name;
    const char *path;
};
inline constexpr Asset kAssets[] = {
    {1200, "HS1200.WAV", "/sounds/handshake-1200.wav"},
    {2400, "HS2400.WAV", "/sounds/handshake-2400.wav"},
    {14400, "HS14400.WAV", "/sounds/handshake-14400.wav"},
    {28800, "HS28800.WAV", "/sounds/handshake-28800.wav"},
    {33600, "HS33600.WAV", "/sounds/handshake-33600.wav"},
    {56000, "HS56000.WAV", "/sounds/handshake-56000.wav"},
};
inline constexpr size_t kAssetCount = sizeof kAssets / sizeof kAssets[0];
inline const Asset &handshakeAsset(uint32_t speed) {
    size_t index = 0;
    for (size_t i = 1; i < kAssetCount && speed >= kAssets[i].speed; ++i)
        index = i;
    return kAssets[index];
}
}  // namespace sounds
