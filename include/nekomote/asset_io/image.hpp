#pragma once

#include <nekomote/image.hpp>

#include <cstdint>
#include <filesystem>
#include <memory_resource>
#include <string>
#include <vector>

namespace nekomote::asset_io
{
// Owns decoded PNG bytes. Allocation/release belongs to asset_io; large
// temporary pixels must return to the OS after upload rather than remain in
// malloc arenas. Move this owner between loader and graphics threads; views
// borrow it only while it remains alive. No graphics objects are owned here.
struct DecodedImageRgba8
{
    DecodedImageRgba8();
    DecodedImageRgba8(DecodedImageRgba8&&) noexcept = default;
    DecodedImageRgba8& operator=(DecodedImageRgba8&&) noexcept = default;
    DecodedImageRgba8(const DecodedImageRgba8&) = delete;
    DecodedImageRgba8& operator=(const DecodedImageRgba8&) = delete;
    int width{0}, height{0};
    std::pmr::vector<std::uint8_t> pixels;
    operator ImageViewRgba8() const noexcept
    {
        return {width, height, pixels};
    }
};
struct ImageResult
{
    DecodedImageRgba8 image;
    TextureRegion region;
    std::string error;
    bool ok{false};
};
ImageResult load_png(const std::filesystem::path& path);
// Model atlases only: exact nonzero-RGBA bounds plus one filtering texel.
// Preserves transparent RGB, original UV space and source files. Noninterlaced
// PNGs use two streaming passes, retaining only the crop and one decoded row.
ImageResult load_cropped_png(const std::filesystem::path& path);
// Diagnostic/export only. Returns an empty error string on success.
std::string write_png(const std::filesystem::path& path, ImageViewRgba8 image);
} // namespace nekomote::asset_io
