#pragma once

#include <nekomote/model/asset.hpp>

#include <stdexcept>

namespace nekomote::asset_io
{
class AssetValidationError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};
// Load-time structural validation; never invoked on the steady frame path.
void validate_asset(const model::ModelAsset& asset);
void validate_physics(const model::ModelAsset& asset);
} // namespace nekomote::asset_io
