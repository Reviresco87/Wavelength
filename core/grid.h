#pragma once

#include <array>
#include <cstdint>

namespace wave {

constexpr int kGridSize = 64;

struct RGB8 {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
};

template <typename T>
class Grid {
public:
    T& at(int x, int y) { return data_[static_cast<size_t>(y) * kGridSize + x]; }
    const T& at(int x, int y) const { return data_[static_cast<size_t>(y) * kGridSize + x]; }

    static constexpr int width() { return kGridSize; }
    static constexpr int height() { return kGridSize; }

private:
    std::array<T, kGridSize * kGridSize> data_{};
};

} // namespace wave
