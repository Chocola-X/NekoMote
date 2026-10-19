#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/asset_migration.hpp>
#include <nekomote/asset_io/image.hpp>

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <system_error>

#include "model_container.hpp"
#include "model_stream.hpp"
#include "validation.hpp"

namespace nekomote::asset_io
{
namespace
{
std::filesystem::path temporary_directory(const std::filesystem::path& destination)
{
    if (!destination.parent_path().empty())
        std::filesystem::create_directories(destination.parent_path());
    auto pattern = destination.string() + ".tmp-XXXXXX";
    if (!::mkdtemp(pattern.data()))
        throw std::system_error(errno, std::generic_category(), "cannot create import staging");
    return pattern;
}
void publish(const std::filesystem::path& temporary, const std::filesystem::path& destination)
{
    if (::renameat2(AT_FDCWD, temporary.c_str(), AT_FDCWD, destination.c_str(), RENAME_NOREPLACE) !=
        0)
        throw std::system_error(errno, std::generic_category(), "cannot publish new asset");
}
void write_json(const std::filesystem::path& path, const Json& document)
{
    std::ofstream stream(path, std::ios::binary);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    stream << document << '\n';
    stream.close();
}
void write_model(const std::filesystem::path& path, const Json& document)
{
    ModelOutput buffer(path);
    std::ostream stream(&buffer);
    stream.exceptions(std::ios::failbit | std::ios::badbit);
    Json::to_msgpack(document, stream);
    buffer.finish();
}
} // namespace

std::vector<std::string> write_bundle(const ImportResult& result,
                                      const std::filesystem::path& resource_root,
                                      const std::filesystem::path& destination)
{
    if (!result.ok)
        return {"refusing to write failed import"};
    std::filesystem::path temporary;
    try
    {
        if (destination.empty() || std::filesystem::exists(destination) ||
            std::filesystem::is_symlink(destination))
            throw std::runtime_error("bundle destination must be a new directory: " +
                                     destination.string());
        temporary = temporary_directory(destination);
        std::filesystem::create_directory(temporary / "textures");
        // Copy only the small texture table, not every frame and physics record.
        auto textures = result.asset.textures;
        for (std::size_t i = 0; i < textures.size(); ++i)
        {
            auto& texture = textures[i];
            const auto path = "textures/" + std::to_string(i) + ".png";
            // Re-encode at import time so rebuilt trees keep optimal PNG
            // encoding; the source tooling's default zlib level never enters
            // the asset tree. Pixels are verified byte-exact before publish.
            if (const auto error = recompress_png(resource_root / texture.path, temporary / path);
                !error.empty())
                throw std::runtime_error(error);
            texture.path = path;
        }
        {
            Json document{{"schema", kAssetSchema}, {"model", result.asset}};
            document["model"]["textures"] = textures;
            // Encoding/compression stream directly to disk: no full MessagePack
            // vector or compressed document is retained next to the offline DOM.
            write_model(temporary / "model.nkm", document);
        }
        {
            // This archive was already validated by the source importer.
            std::ofstream archive(temporary / "source.json", std::ios::binary);
            archive.exceptions(std::ios::failbit | std::ios::badbit);
            archive << result.source_archive << '\n';
            archive.close();
        }
        write_json(temporary / "report.json",
                   Json{{"structural_import", true},
                        {"playback_certified", false},
                        {"unsupported_fields", result.report.unsupported_fields},
                        {"warnings", result.report.warnings}});
        publish(temporary, destination);
        temporary.clear();
        return {};
    }
    catch (const std::exception& error)
    {
        if (!temporary.empty())
        {
            std::error_code ignored;
            std::filesystem::remove_all(temporary, ignored);
        }
        return {error.what()};
    }
}

std::string convert_model_json(const std::filesystem::path& source,
                               const std::filesystem::path& destination)
{
    std::filesystem::path temporary;
    try
    {
        if (destination.empty() || std::filesystem::exists(destination) ||
            std::filesystem::is_symlink(destination))
            throw std::runtime_error("model destination must be a new file");
        if (!std::filesystem::is_regular_file(source) ||
            std::filesystem::file_size(source) > kModelDecodedLimit)
            throw std::runtime_error("source model JSON size exceeds limit or is not regular");
        std::ifstream input(source, std::ios::binary);
        if (!input)
            throw std::runtime_error("cannot open source model JSON");
        const auto asset = ModelStream{}.read(input, Json::input_format_t::json);
        validate_asset(asset);
        temporary = temporary_directory(destination);
        const auto candidate = temporary / "model.nkm";
        write_model(candidate, Json{{"schema", kAssetSchema}, {"model", asset}});
        const auto verified = load_asset(candidate);
        if (!verified.ok)
            throw std::runtime_error(verified.errors.front());
        if (Json(verified.asset) != Json(asset))
            throw std::runtime_error("model conversion changed typed author data");
        publish(candidate, destination);
        std::error_code ignored;
        std::filesystem::remove(temporary, ignored);
        temporary.clear();
        return {};
    }
    catch (const std::exception& error)
    {
        if (!temporary.empty())
        {
            std::error_code ignored;
            std::filesystem::remove_all(temporary, ignored);
        }
        return source.string() + ": " + error.what();
    }
}
} // namespace nekomote::asset_io
