#include <nekomote/application/catalog.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <unordered_map>

namespace nekomote::application
{

std::string_view language_code(Language lang) noexcept
{
    switch (lang)
    {
        case Language::English:
            return "en";
        case Language::ChineseSimplified:
            return "zh-CN";
        case Language::ChineseTraditional:
            return "zh-TW";
        case Language::Japanese:
            return "ja";
    }
    return "en";
}

std::optional<Language> language_from_code(std::string_view code) noexcept
{
    if (code == "en")
        return Language::English;
    if (code == "zh-CN")
        return Language::ChineseSimplified;
    if (code == "zh-TW")
        return Language::ChineseTraditional;
    if (code == "ja")
        return Language::Japanese;
    return std::nullopt;
}

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

// Keep in sync with the importer group table in tools/import_emote/main.cpp.
struct GroupTitleEntry
{
    const char* id;
    const char* en;
    const char* zh_cn;
    const char* zh_tw;
    const char* ja;
};

const GroupTitleEntry kGroupTitles[] = {
    {"chocola", "Chocola", "巧克力", "巧克力", "チョコラ"},
    {"vanilla", "Vanilla", "香草", "香草", "バニラ"},
    {"azuki", "Azuki", "红豆", "紅豆", "あずき"},
    {"coconut", "Coconut", "椰子", "椰子", "ココナッツ"},
    {"maple", "Maple", "枫", "楓", "メイプル"},
    {"cinnamon", "Cinnamon", "桂", "桂", "シナモン"},
    {"milk", "Milk", "牛奶", "牛奶", "ミルク"},
    {"fraise", "Fraise", "草莓", "草莓", "フレーズ"},
};

LocalizedName group_title(const std::string& id)
{
    for (const auto& entry : kGroupTitles)
        if (id == entry.id)
            return LocalizedName{entry.en, entry.zh_cn, entry.zh_tw, entry.ja};
    return {};
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

// Reads one localized name-dictionary value. A plain string names all
// languages; an object maps language codes ("en"/"zh-CN"/"zh-TW"/"ja") to
// translations. Unknown keys are ignored.
LocalizedName parse_localized_name(const Json& value)
{
    LocalizedName name;
    if (value.is_string())
    {
        const std::string text = value.get<std::string>();
        name.en = name.zh_cn = name.zh_tw = name.ja = text;
        return name;
    }
    if (!value.is_object())
        return name;
    for (auto it = value.begin(); it != value.end(); ++it)
    {
        if (!it.value().is_string())
            continue;
        const std::string text = it.value().get<std::string>();
        if (it.key() == "en")
            name.en = text;
        else if (it.key() == "zh-CN")
            name.zh_cn = text;
        else if (it.key() == "zh-TW")
            name.zh_tw = text;
        else if (it.key() == "ja")
            name.ja = text;
    }
    return name;
}

std::unordered_map<std::string, LocalizedName> load_localized_dict(const fs::path& path)
{
    std::unordered_map<std::string, LocalizedName> dict;
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
            LocalizedName name = parse_localized_name(it.value());
            if (name.empty())
                continue;
            dict.emplace(it.key(), std::move(name));
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

    const auto char_names = load_localized_dict(characters_dir / "characters.json");

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
                if (variant_id == "audio")
                    continue;
                const auto model = variant_dir / "model.nkm";
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
                            if (mj.contains("display_name"))
                                ch.names = parse_localized_name(mj["display_name"]);
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

                if (ch.names.empty())
                {
                    if (auto it = char_names.find(two_layer_path); it != char_names.end())
                        ch.names = it->second;
                }
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
        const std::string variant_id =
            (slash != std::string::npos) ? ch.name.substr(slash + 1) : ch.name;
        group.items.push_back({static_cast<int>(i), ch.name, ch.names, variant_id});
    }

    const fs::path backgrounds_dir = assets_dir / "backgrounds";
    if (!fs::exists(backgrounds_dir))
        return catalog;

    const auto bg_names = load_localized_dict(backgrounds_dir / "background.json");
    const auto lookup_name = [&](const std::string& filename) -> LocalizedName
    {
        if (auto it = bg_names.find(filename); it != bg_names.end())
            return it->second;
        return {};
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
        catalog.backgrounds.push_back({stem, lookup_name(stem + ".png"), path});
    }

    const auto dynamic_dir = backgrounds_dir / "dynamic";
    if (fs::exists(dynamic_dir))
    {
        const auto dyn_names = load_localized_dict(dynamic_dir / "background.json");
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
                bg.names = it->second;
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
    if (!json.is_object())
        return config;
    if (json.contains("character") && json["character"].is_string())
        config.character = json["character"].get<std::string>();
    if (json.contains("background") && json["background"].is_string())
        config.background = json["background"].get<std::string>();
    if (json.contains("language") && json["language"].is_string())
    {
        if (auto lang = language_from_code(json["language"].get<std::string>()))
            config.language = *lang;
    }
    if (json.contains("scale") && json["scale"].is_number())
        config.scale = json["scale"].get<float>();
    if (json.contains("origin_x") && json["origin_x"].is_number())
        config.origin_x = json["origin_x"].get<float>();
    if (json.contains("origin_y") && json["origin_y"].is_number())
        config.origin_y = json["origin_y"].get<float>();
    if (json.contains("icon_x") && json["icon_x"].is_number())
        config.icon_x = json["icon_x"].get<float>();
    if (json.contains("icon_y") && json["icon_y"].is_number())
        config.icon_y = json["icon_y"].get<float>();
    if (json.contains("sidebar_x") && json["sidebar_x"].is_number())
        config.sidebar_x = json["sidebar_x"].get<float>();
    if (json.contains("sidebar_y") && json["sidebar_y"].is_number())
        config.sidebar_y = json["sidebar_y"].get<float>();
    if (json.contains("background_fade_seconds") && json["background_fade_seconds"].is_number())
        config.background_fade_seconds = json["background_fade_seconds"].get<float>();
    if (json.contains("width") && json["width"].is_number_integer())
    {
        const double value = json["width"].get<double>();
        if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max())
            config.width = static_cast<int>(value);
    }
    if (json.contains("height") && json["height"].is_number_integer())
    {
        const double value = json["height"].get<double>();
        if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max())
            config.height = static_cast<int>(value);
    }
    if (json.contains("fps") && json["fps"].is_number_integer())
    {
        const double value = json["fps"].get<double>();
        if (value >= std::numeric_limits<int>::min() && value <= std::numeric_limits<int>::max())
            config.fps = static_cast<int>(value);
    }
    if (json.contains("mute") && json["mute"].is_boolean())
        config.mute = json["mute"].get<bool>();
    if (json.contains("fit") && json["fit"].is_boolean())
        config.fit = json["fit"].get<bool>();
    if (json.contains("transform_saved") && json["transform_saved"].is_boolean())
        config.transform_saved = json["transform_saved"].get<bool>();
    if (json.contains("icon_position_saved") && json["icon_position_saved"].is_boolean())
        config.icon_position_saved = json["icon_position_saved"].get<bool>();
    if (json.contains("sidebar_position_saved") && json["sidebar_position_saved"].is_boolean())
        config.sidebar_position_saved = json["sidebar_position_saved"].get<bool>();
    if (json.contains("sidebar_free") && json["sidebar_free"].is_boolean())
        config.sidebar_free = json["sidebar_free"].get<bool>();
    if (config.fps != 0 && (config.fps < 5 || config.fps > 240))
        config.fps = 60;
    if (!std::isfinite(config.background_fade_seconds) || config.background_fade_seconds < 0)
        config.background_fade_seconds = 1.0f;
    // A saved transform needs all three numeric values. Incomplete or corrupt
    // layout data must not prevent valid character/background/FPS preferences loading.
    for (const auto* key : {"scale", "origin_x", "origin_y"})
        if (!json.contains(key) || !json[key].is_number())
            config.transform_saved = false;
    for (const auto* key : {"icon_x", "icon_y"})
        if (!json.contains(key) || !json[key].is_number())
            config.icon_position_saved = false;
    for (const auto* key : {"sidebar_x", "sidebar_y"})
        if (!json.contains(key) || !json[key].is_number())
            config.sidebar_position_saved = false;
    return config;
}

void WallpaperConfig::save(const fs::path& path) const
{
    if (!path.parent_path().empty())
        fs::create_directories(path.parent_path());
    Json json = Json::object();
    json["character"] = character;
    json["background"] = background;
    json["language"] = std::string(language_code(language));
    json["scale"] = scale;
    json["origin_x"] = origin_x;
    json["origin_y"] = origin_y;
    json["width"] = width;
    json["height"] = height;
    json["fps"] = fps;
    json["mute"] = mute;
    json["fit"] = fit;
    json["transform_saved"] = transform_saved;
    json["icon_position_saved"] = icon_position_saved;
    json["icon_x"] = icon_x;
    json["icon_y"] = icon_y;
    json["sidebar_position_saved"] = sidebar_position_saved;
    json["sidebar_x"] = sidebar_x;
    json["sidebar_y"] = sidebar_y;
    json["sidebar_free"] = sidebar_free;
    json["background_fade_seconds"] = background_fade_seconds;
    // Each config path is owned by its serial application controller. Replace
    // only after a complete write, so interruption cannot truncate preferences.
    const auto temporary = fs::path(path.string() + ".tmp");
    struct PendingFile
    {
        fs::path path;
        ~PendingFile()
        {
            std::error_code ec;
            fs::remove(path, ec);
        }
    } pending{temporary};
    std::ofstream file(temporary);
    if (!file)
        throw std::runtime_error("cannot write config " + temporary.string());
    file << json.dump(2);
    file.close();
    if (!file)
        throw std::runtime_error("cannot finish config " + temporary.string());
    fs::rename(temporary, path);
}
} // namespace nekomote::application
