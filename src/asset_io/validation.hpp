#pragma once

#include <nekomote/model/asset.hpp>

namespace nekomote::asset_io
{
// Load-time structural validation; never invoked on the steady frame path.
void validate_asset(const model::ModelAsset& asset);
} // namespace nekomote::asset_io
