#pragma once

#include <cstdint>
#include <vector>

namespace nekomote
{
// Row-major, top-left origin, RGBA8 straight alpha, encoded colour values.
struct ImageRgba8
{
    int width{0}, height{0};
    std::vector<std::uint8_t> pixels;
};
} // namespace nekomote
