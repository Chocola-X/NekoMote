#pragma once

#include <nekomote/asset_io/import_emote.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace nekomote::asset_io
{
inline constexpr int kAssetSchema = 1;
struct AssetResult
{
    model::ModelAsset asset;
    std::vector<std::string> errors;
    bool ok{false};
};
// Current schema only. Rebuild unsupported assets using nekomote_import.
AssetResult load_asset(const std::filesystem::path& file);
// Creates a new bundle directory including only referenced textures and offline
// evidence. Refuses existing destinations; failed writes remove only our temp dir.
std::vector<std::string> write_bundle(const ImportResult& result,
                                      const std::filesystem::path& resource_root,
                                      const std::filesystem::path& destination);
} // namespace nekomote::asset_io
