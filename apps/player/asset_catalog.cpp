#include "asset_catalog.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace nekomote::player
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;
}

AssetCatalog AssetCatalog::scan(const fs::path& assets_dir)
{
    AssetCatalog catalog;
    catalog.assets_root = assets_dir;
    const fs::path characters_dir = assets_dir / "characters";
    if (fs::exists(characters_dir))
        for (const auto& entry : fs::directory_iterator(characters_dir))
        {
            if (!entry.is_directory())
                continue;
            const auto dir = entry.path();
            const auto model = dir / "model.nkm.json";
            const auto session = dir / "session.nks.json";
            if (!fs::exists(model) || !fs::exists(session))
                continue;
            catalog.characters.push_back(
                {dir.filename().string(), model, session, dir});
            auto& ch = catalog.characters.back();
            const fs::path meta = dir / "meta.json";
            if (fs::exists(meta))
            {
                std::ifstream mf(meta);
                if (mf)
                {
                    Json mj;
                    try
                    {
                        mf >> mj;
                        if (mj.contains("cutoff_y"))
                        {
                            ch.cutoff_y = mj["cutoff_y"].get<float>();
                            ch.has_cutoff = true;
                        }
                    }
                    catch (...)
                    {
                    }
                }
            }
        }
    std::sort(catalog.characters.begin(), catalog.characters.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    const fs::path backgrounds_dir = assets_dir / "backgrounds";
    if (fs::exists(backgrounds_dir))
        for (const auto& entry : fs::directory_iterator(backgrounds_dir))
        {
            if (!entry.is_regular_file())
                continue;
            const auto path = entry.path();
            const auto ext = path.extension().string();
            if (ext != ".png" && ext != ".PNG")
                continue;
            catalog.backgrounds.push_back({path.stem().string(), path});
        }
    std::sort(catalog.backgrounds.begin(), catalog.backgrounds.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
    return catalog;
}

const CharacterEntry* AssetCatalog::find_character(const std::string& name) const
{
    for (const auto& c : characters)
        if (c.name == name)
            return &c;
    return nullptr;
}

const BackgroundEntry* AssetCatalog::find_background(const std::string& name) const
{
    for (const auto& b : backgrounds)
        if (b.name == name)
            return &b;
    return nullptr;
}

WallpaperConfig WallpaperConfig::load(const fs::path& path)
{
    WallpaperConfig config;
    if (!fs::exists(path))
        return config;
    std::ifstream file(path);
    if (!file)
        return config;
    Json json;
    try
    {
        file >> json;
    }
    catch (...)
    {
        return config;
    }
    if (json.contains("character"))
        config.character = json["character"].get<std::string>();
    if (json.contains("background"))
        config.background = json["background"].get<std::string>();
    if (json.contains("scale"))
        config.scale = json["scale"].get<float>();
    if (json.contains("origin_x"))
        config.origin_x = json["origin_x"].get<float>();
    if (json.contains("origin_y"))
        config.origin_y = json["origin_y"].get<float>();
    if (json.contains("width"))
        config.width = json["width"].get<int>();
    if (json.contains("height"))
        config.height = json["height"].get<int>();
    if (json.contains("fps"))
        config.fps = json["fps"].get<int>();
    if (json.contains("mute"))
        config.mute = json["mute"].get<bool>();
    if (json.contains("fit"))
        config.fit = json["fit"].get<bool>();
    return config;
}

void WallpaperConfig::save(const fs::path& path) const
{
    fs::create_directories(path.parent_path());
    Json json = Json::object();
    json["character"] = character;
    json["background"] = background;
    json["scale"] = scale;
    json["origin_x"] = origin_x;
    json["origin_y"] = origin_y;
    json["width"] = width;
    json["height"] = height;
    json["fps"] = fps;
    json["mute"] = mute;
    json["fit"] = fit;
    std::ofstream file(path);
    if (!file)
        throw std::runtime_error("cannot write config " + path.string());
    file << json.dump(2);
}
}
