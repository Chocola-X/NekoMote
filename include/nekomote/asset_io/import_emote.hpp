#pragma once

#include <nekomote/model/asset.hpp>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace nekomote::asset_io
{
struct ImportOptions
{
    std::filesystem::path json_path, resx_path, resource_root;
    std::unordered_map<std::string, std::string> external_textures;
};
struct ImportReport
{
    std::vector<std::string> warnings, errors, unsupported_fields;
    int parts_count{0}, layers_count{0}, frames_count{0}, icons_count{0};
    int textures_count{0}, variables_count{0};
    std::string texture_path;
};
struct ImportResult
{
    model::ModelAsset asset;
    ImportReport report;
    // Offline audit archive, never consumed by runtime/render_gl.
    std::string source_archive;
    bool ok{false};
};
// Expected input and I/O errors return ok=false with a JSON pointer/file path.
// Allocation failures are not converted to a successful or partial asset.
ImportResult import_emote(const ImportOptions& opts);
} // namespace nekomote::asset_io
