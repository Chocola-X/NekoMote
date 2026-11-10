#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/runtime/geometry_scene.hpp>

#include <cstdio>
#include <filesystem>
#include <vector>
int main(int argc, char** argv)
{
    if (argc != 2)
        return 1;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             std::filesystem::path(argv[1]) / "characters"))
    {
        if (entry.path().filename() != "model.nkm")
            continue;
        auto asset = nekomote::asset_io::load_asset(entry.path());
        if (!asset.ok)
            return 2;
        auto scene = nekomote::runtime::compile_geometry_scene(asset.asset);
        if (!scene.scene)
            return 3;
        nekomote::runtime::GeometryInstance partial(*scene.scene);
        if (!partial.evaluate(nekomote::runtime::GeometryEvaluation::Locators))
            return 4;
        std::size_t needed = 0;
        for (std::size_t i = 1; i <= scene.scene->node_count(); ++i)
            needed +=
                partial.observe_node(nekomote::NodeId{static_cast<std::uint32_t>(i)}).has_value();
        std::printf("{\"model\":\"%s\",\"nodes\":%zu,\"closure\":%zu,\"locators\":%zu,\"bitmap_"
                    "bytes\":%zu,\"vector_header_bytes\":%zu}\n",
                    entry.path()
                        .parent_path()
                        .lexically_relative(std::filesystem::path(argv[1]) / "characters")
                        .c_str(),
                    scene.scene->node_count(), needed, scene.scene->locators().size(),
                    ((scene.scene->node_count() + 1 + 63) / 64) * 8, sizeof(std::vector<bool>));
    }
}
