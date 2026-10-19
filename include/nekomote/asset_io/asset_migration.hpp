#pragma once

#include <filesystem>
#include <string>

namespace nekomote::asset_io
{
// Explicit offline migration of schema-6 JSON, never used by playback. Creates
// a new binary model without replacing an existing file; checks typed semantics
// before publication. Returns an empty string on success.
std::string convert_model_json(const std::filesystem::path& source,
                               const std::filesystem::path& destination);
} // namespace nekomote::asset_io
