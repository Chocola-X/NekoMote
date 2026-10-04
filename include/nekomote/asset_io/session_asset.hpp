#pragma once

#include <nekomote/model/session_asset.hpp>

#include <filesystem>
#include <string>
#include <vector>

namespace nekomote::asset_io
{
struct SessionLoadResult
{
    bool ok{false};
    model::SessionAsset asset;
    std::vector<std::string> errors;
};
// Loads only the current session schema. Validate once; frame code never parses
// JSON or resolves resource paths. Relative audio references cannot escape it.
SessionLoadResult load_session_asset(const std::filesystem::path& path);
} // namespace nekomote::asset_io
