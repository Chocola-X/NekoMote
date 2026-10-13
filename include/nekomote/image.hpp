#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace nekomote
{
// Borrows RGBA bytes for the duration of a synchronous upload/export call.
struct ImageViewRgba8
{
    int width{0}, height{0};
    std::span<const std::uint8_t> pixels;
};
// Row-major, top-left origin, RGBA8 straight alpha, encoded colour values.
struct ImageRgba8
{
    int width{0}, height{0};
    std::vector<std::uint8_t> pixels;
    operator ImageViewRgba8() const noexcept
    {
        return {width, height, pixels};
    }
};

// Uploaded image is a rectangle of the original atlas. RenderFrame UVs remain
// in that original atlas space. Zero dimensions mean an uncropped image.
// Cropping must retain a zero RGBA texel around discarded edges for GL_LINEAR
// / Vulkan linear filtering with CLAMP_TO_EDGE (no mipmaps or anisotropy).
struct TextureRegion
{
    int source_width{0}, source_height{0}, left{0}, top{0};

    bool valid_for(int width, int height) const noexcept
    {
        if (source_width == 0 && source_height == 0)
            return left == 0 && top == 0;
        return source_width > 0 && source_width <= 32768 && source_height > 0 &&
               source_height <= 32768 && width > 0 && height > 0 && left >= 0 && top >= 0 &&
               width <= source_width && height <= source_height && left <= source_width - width &&
               top <= source_height - height;
    }
    // Original normalized UV -> uploaded texel coordinates. Pixel-space
    // sampling avoids crop-size-dependent normalized coordinate rounding.
    // Call only after image dimensions and valid_for() have been checked.
    std::array<float, 4> texel_transform(int width, int height) const noexcept
    {
        if (source_width == 0 && source_height == 0)
            return {static_cast<float>(width), static_cast<float>(height), 0, 0};
        return {static_cast<float>(source_width), static_cast<float>(source_height),
                -static_cast<float>(left), -static_cast<float>(top)};
    }
};
} // namespace nekomote
