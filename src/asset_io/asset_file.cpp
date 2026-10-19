#include <nekomote/asset_io/asset_file.hpp>

#include "model_container.hpp"
#include "model_stream.hpp"
#include "validation.hpp"

namespace nekomote::asset_io
{
AssetResult load_asset(const std::filesystem::path& file)
{
    AssetResult result;
    try
    {
        ModelInput buffer(file);
        std::istream stream(&buffer);
        result.asset = ModelStream{}.read(stream, Json::input_format_t::msgpack);
        validate_asset(result.asset);
        result.ok = true;
    }
    catch (const std::exception& error)
    {
        result.asset = {};
        result.errors.push_back(file.string() + ": " + error.what());
    }
    return result;
}
} // namespace nekomote::asset_io
