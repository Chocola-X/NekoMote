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
// Offline import/migration only. Losslessly re-encodes the source PNG with
// per-row greedy filter selection and zlib level 9, dropping every non-pixel
// chunk. Accepts 8-bit RGB/RGBA, interlaced or not; the output is always
// non-interlaced. Decodes the result and compares every pixel byte before
// publishing. Returns an empty error string on success.
std::string recompress_png(const std::filesystem::path& source,
                           const std::filesystem::path& destination);
// Offline import/migration only. Strips metadata chunks (iTXt/tEXt/zTXt/iCCP/
// gAMA/sRGB/pHYs and similar); IHDR/PLTE/tRNS/IDAT/IEND bytes are copied
// verbatim, so pixel data is untouched. Validates chunk CRCs and re-reads the
// output before publishing. Returns an empty error string on success.
std::string strip_png_metadata(const std::filesystem::path& source,
                               const std::filesystem::path& destination);
} // namespace nekomote::asset_io
