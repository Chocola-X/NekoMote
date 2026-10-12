#include <nekomote/application/catalog.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <fstream>
#include <stdexcept>
#include <unordered_map>

namespace nekomote::application
{
namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

const std::vector<std::pair<std::string, std::string>> kGroupTitles = {
    {"chocola", "巧克力"}, {"vanilla", "香草"}, {"azuki", "红豆"}, {"coconut", "椰子"},
    {"maple", "枫"},       {"cinnamon", "桂"},  {"milk", "牛奶"},  {"fraise", "草莓"}};

std::string group_title(const std::string& id)
{
    for (const auto& [gid, title] : kGroupTitles)
        if (gid == id)
            return title;
    return id;
}

int parse_hhmm(const std::string& s)
{
    const auto colon = s.find(':');
    if (colon == std::string::npos)
        return 0;
    try
    {
        const int hh = std::stoi(s.substr(0, colon));
        const int mm = std::stoi(s.substr(colon + 1));
        return hh * 60 + mm;
    }
    catch (...)
    {
        return 0;
    }
}

std::unordered_map<std::string, std::string> load_name_dict(const fs::path& path)
{
    std::unordered_map<std::string, std::string> dict;
    if (!fs::exists(path))
        return dict;
    std::ifstream f(path);
    if (!f)
        return dict;
    try
    {
        Json j;
        f >> j;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.value().is_string())
                dict.emplace(it.key(), it.value().get<std::string>());
        }
    }
    catch (...)
    {
    }
    return dict;
}

std::unordered_map<std::string, std::string> load_bg_name_dict(const fs::path& path)
{
    std::unordered_map<std::string, std::string> dict;
    if (!fs::exists(path))
        return dict;
    std::ifstream f(path);
    if (!f)
        return dict;
    try
    {
        Json j;
        f >> j;
        for (auto it = j.begin(); it != j.end(); ++it)
        {
            if (it.value().is_object() && it.value().contains("zh-CN"))
                dict.emplace(it.key(), it.value()["zh-CN"].get<std::string>());
            else if (it.value().is_string())
                dict.emplace(it.key(), it.value().get<std::string>());
        }
    }
    catch (...)
    {
    }
    return dict;
}
} // namespace

const std::filesystem::path& BackgroundEntry::path() const noexcept
{
    if (!phases.empty())
        return phases[0].image;
    static const std::filesystem::path empty;
    return empty;
}

std::size_t BackgroundEntry::phase_index_at(int minutes_since_midnight) const noexcept
{
    if (phases.size() <= 1)
        return 0;
    for (std::size_t i = phases.size() - 1; i > 0; --i)
        if (minutes_since_midnight >= phases[i].start_minutes)
            return i;
    if (minutes_since_midnight >= phases[0].start_minutes)
        return 0;
    return phases.size() - 1;
}

const std::filesystem::path& BackgroundEntry::path_for_phase(std::size_t index) const noexcept
{
    if (index < phases.size())
        return phases[index].image;
    static const std::filesystem::path empty;
    return empty;
}

AssetCatalog AssetCatalog::scan(const fs::path& assets_dir)
{
    AssetCatalog catalog;
    catalog.assets_root = assets_dir;
    const fs::path characters_dir = assets_dir / "characters";

    const auto char_names = load_name_dict(characters_dir / "characters.json");

    if (fs::exists(characters_dir))
        for (const auto& char_entry : fs::directory_iterator(characters_dir))
        {
            if (!char_entry.is_directory())
                continue;
            const auto char_dir = char_entry.path();
            const std::string char_id = char_dir.filename().string();
            for (const auto& variant_entry : fs::directory_iterator(char_dir))
            {
                if (!variant_entry.is_directory())
                    continue;
                const auto variant_dir = variant_entry.path();
                const std::string variant_id = variant_dir.filename().string();
                const auto model = variant_dir / "model.nkm.json";
                const auto session = variant_dir / "session.nks.json";
                if (!fs::exists(model) || !fs::exists(session))
                    continue;
                const std::string two_layer_path = char_id + "/" + variant_id;
                catalog.characters.push_back({two_layer_path, model, session, variant_dir});
                auto& ch = catalog.characters.back();

                const fs::path meta = variant_dir / "meta.json";
                if (fs::exists(meta))
                {
                    std::ifstream mf(meta);
                    if (mf)
                    {
                        try
                        {
                            Json mj;
                            mf >> mj;
                            if (mj.contains("cutoff_y"))
                            {
                                ch.cutoff_y = mj["cutoff_y"].get<float>();
                                ch.has_cutoff = true;
                            }
                            if (mj.contains("display_name") && mj["display_name"].is_string())
                                ch.display_name = mj["display_name"].get<std::string>();
                            if (mj.contains("hidden") && mj["hidden"].is_boolean())
                                ch.hidden = mj["hidden"].get<bool>();
                            if (mj.contains("order") && mj["order"].is_number_integer())
                                ch.order = mj["order"].get<int>();
                        }
                        catch (...)
                        {
                        }
                    }
                }

                if (ch.display_name.empty())
                {
                    if (auto it = char_names.find(two_layer_path); it != char_names.end())
                        ch.display_name = it->second;
                }
                if (ch.display_name.empty())
                    ch.display_name = variant_id;
            }
        }

    std::sort(catalog.characters.begin(), catalog.characters.end(),
              [](const auto& a, const auto& b)
              {
                  const auto a_group = a.name.substr(0, a.name.find('/'));
                  const auto b_group = b.name.substr(0, b.name.find('/'));
                  if (a_group != b_group)
                      return a_group < b_group;
                  if (a.order != b.order)
                      return a.order < b.order;
                  return a.name < b.name;
              });

    std::unordered_map<std::string, std::size_t> group_index;
    for (std::size_t i = 0; i < catalog.characters.size(); ++i)
    {
        const auto& ch = catalog.characters[i];
        if (ch.hidden)
            continue;
        const auto slash = ch.name.find('/');
        const std::string group_id =
            (slash != std::string::npos) ? ch.name.substr(0, slash) : ch.name;
        if (group_index.find(group_id) == group_index.end())
        {
            group_index[group_id] = catalog.character_groups.size();
            CharacterGroup group;
            group.id = group_id;
            group.title = group_title(group_id);
            catalog.character_groups.push_back(std::move(group));
        }
        auto& group = catalog.character_groups[group_index[group_id]];
        group.items.push_back({static_cast<int>(i), ch.name, ch.display_name});
    }

    const fs::path backgrounds_dir = assets_dir / "backgrounds";
    if (!fs::exists(backgrounds_dir))
        return catalog;

    const auto bg_names = load_bg_name_dict(backgrounds_dir / "background.json");
    const auto lookup_name = [&](const std::string& filename,
                                 const std::string& fallback) -> std::string
    {
        if (auto it = bg_names.find(filename); it != bg_names.end())
            return it->second;
        return fallback;
    };

    for (const auto& entry : fs::directory_iterator(backgrounds_dir))
    {
        if (!entry.is_regular_file())
            continue;
        const auto path = entry.path();
        const auto ext = path.extension().string();
        if (ext != ".png" && ext != ".PNG")
            continue;
        const auto stem = path.stem().string();
        const auto disp = lookup_name(stem + ".png", stem);
        catalog.backgrounds.push_back({stem, disp, path});
    }

    const auto dynamic_dir = backgrounds_dir / "dynamic";
    if (fs::exists(dynamic_dir))
    {
        const auto dyn_names = load_name_dict(dynamic_dir / "background.json");
        for (const auto& group_entry : fs::directory_iterator(dynamic_dir))
        {
            if (!group_entry.is_directory())
                continue;
            const auto group_dir = group_entry.path();
            const std::string group_id = group_dir.filename().string();
            const auto config_path = group_dir / "config.json";
            if (!fs::exists(config_path))
                continue;
            std::ifstream cf(config_path);
            if (!cf)
                continue;
            Json cj;
            try
            {
                cf >> cj;
            }
            catch (...)
            {
                continue;
            }
            if (!cj.contains("phases") || !cj["phases"].is_array())
                continue;
            BackgroundEntry bg;
            bg.name = group_id;
            bg.type = BackgroundType::Dynamic;
            if (auto it = dyn_names.find(group_id); it != dyn_names.end())
                bg.display_name = it->second;
            else
                bg.display_name = group_id;
            if (cj.contains("transition_seconds") && cj["transition_seconds"].is_number())
                bg.transition_seconds = cj["transition_seconds"].get<float>();
            for (const auto& phase : cj["phases"])
            {
                if (!phase.contains("image") || !phase["image"].is_string())
                    continue;
                BackgroundPhase bp;
                bp.image = group_dir / phase["image"].get<std::string>();
                if (phase.contains("start") && phase["start"].is_string())
                    bp.start_minutes = parse_hhmm(phase["start"].get<std::string>());
                bg.phases.push_back(std::move(bp));
            }
            if (bg.phases.empty())
                continue;
            std::sort(bg.phases.begin(), bg.phases.end(), [](const auto& a, const auto& b)
                      { return a.start_minutes < b.start_minutes; });
            catalog.backgrounds.push_back(std::move(bg));
        }
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
} // namespace nekomote::application
