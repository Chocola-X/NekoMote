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
} // namespace

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
            catalog.characters.push_back({dir.filename().string(), model, session, dir});
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

    const std::vector<std::pair<std::string, std::string>> group_defs = {
        {"chocola", "巧克力"}, {"vanilla", "香草"}, {"azuki", "红豆"}, {"coconut", "椰子"},
        {"maple", "枫"},       {"cinnamon", "桂"},  {"milk", "牛奶"},  {"fraise", "草莓"}};

    auto parse_display_name = [](const std::string& char_id,
                                 const std::string& model_id) -> std::string
    {
        std::string rem = model_id;
        if (rem.rfind(char_id + "-", 0) == 0)
            rem = rem.substr(char_id.size() + 1);
        else if (rem == char_id)
            return "原版";

        const std::vector<std::pair<std::string, std::string>> costumes = {
            {"france-maid", "法国女仆"}, {"wintermaid", "冬季女仆"}, {"casual", "日常"},
            {"dress", "礼服"},           {"lolita", "洛丽塔"},       {"maid", "女仆"},
            {"pajama", "睡衣"},          {"santa", "圣诞"},          {"winter", "冬季"},
            {"yukata", "浴衣"},          {"teenage", "少女"},        {"koneko", "小猫"},
            {"hawaii", "夏威夷"},        {"swimsuit", "泳装"},       {"date", "约会"},
            {"shirt", "衬衫"},           {"summer", "夏装"}};

        const std::vector<std::pair<std::string, std::string>> variants = {
            {"no-bell-tail-pin", "无铃铛 / 尾部固定"},
            {"no-bell", "无铃铛"},
            {"with-bell", "有铃铛"},
            {"ohtoro", "Ohtoro"},
            {"custard", "卡仕达"},
            {"4yo", "4yo 版本"},
            {"v2", "版本 2"},
            {"a", "站姿 A"},
            {"b", "站姿 B"},
            {"original", "原版"}};

        std::string costume_name;
        for (const auto& [key, name] : costumes)
        {
            if (rem == key)
            {
                costume_name = name;
                rem.clear();
                break;
            }
            if (rem.rfind(key + "-", 0) == 0)
            {
                costume_name = name;
                rem = rem.substr(key.size() + 1);
                break;
            }
        }
        if (costume_name.empty())
        {
            auto dash = rem.find('-');
            if (dash != std::string::npos)
            {
                costume_name = rem.substr(0, dash);
                rem = rem.substr(dash + 1);
            }
            else
            {
                costume_name = rem;
                rem.clear();
            }
        }

        std::vector<std::string> var_names;
        while (!rem.empty())
        {
            bool matched = false;
            for (const auto& [key, name] : variants)
            {
                if (rem == key)
                {
                    var_names.push_back(name);
                    rem.clear();
                    matched = true;
                    break;
                }
                if (rem.rfind(key + "-", 0) == 0)
                {
                    var_names.push_back(name);
                    rem = rem.substr(key.size() + 1);
                    matched = true;
                    break;
                }
            }
            if (!matched)
            {
                auto dash = rem.find('-');
                if (dash != std::string::npos)
                {
                    var_names.push_back(rem.substr(0, dash));
                    rem = rem.substr(dash + 1);
                }
                else
                {
                    var_names.push_back(rem);
                    rem.clear();
                }
            }
        }

        if (var_names.empty())
            return costume_name + "·原版";

        std::string result = costume_name;
        for (const auto& v : var_names)
            result += "·" + v;
        return result;
    };

    for (const auto& [gid, gtitle] : group_defs)
    {
        CharacterGroup group;
        group.id = gid;
        group.title = gtitle;
        for (std::size_t i = 0; i < catalog.characters.size(); ++i)
        {
            auto& ch = catalog.characters[i];
            if (ch.name == gid || ch.name.rfind(gid + "-", 0) == 0)
            {
                ch.display_name = parse_display_name(gid, ch.name);
                group.items.push_back({static_cast<int>(i), ch.name, ch.display_name});
            }
        }
        catalog.character_groups.push_back(std::move(group));
    }

    const std::unordered_map<std::string, std::string> known_bg_names = {
        {"BG_07A", "海滨餐厅露台"},
        {"BG_13A", "粉色浴室"},
        {"BG_22A", "暖色灯光的卧室"},
        {"BG_29A", "游乐园入口广场"},
        {"BG_30A", "粉色装饰的卧室"}};

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
            const auto stem = path.stem().string();
            std::string disp = stem;
            if (auto it = known_bg_names.find(stem); it != known_bg_names.end())
                disp = it->second;
            catalog.backgrounds.push_back({stem, disp, path});
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
