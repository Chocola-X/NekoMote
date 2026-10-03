#pragma once

#include <nekomote/image.hpp>

#include <filesystem>
#include <string>

namespace nekomote::asset_io
{
struct ImageResult
{
    ImageRgba8 image;
    std::string error;
    bool ok{false};
};
ImageResult load_png(const std::filesystem::path& path);
// Diagnostic/export only. Returns an empty error string on success.
std::string write_png(const std::filesystem::path& path, const ImageRgba8& image);
} // namespace nekomote::asset_io
