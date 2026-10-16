// Offline structural importer and current asset bundle compiler.

#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/import_emote.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
namespace fs = std::filesystem;
using Json = nlohmann::json;

void print_usage()
{
    std::fprintf(stderr, "usage: nekomote_import <model.json> <model.resx.json> "
                         "--output <new-dir> [--resource-root <dir>]\n");
    std::fprintf(stderr, "       nekomote_import migrate-layout --assets <assets_dir>\n");
}

// One display name per supported language. Keep the language keys and group
// titles in sync with the runtime tables in src/application/catalog.cpp.
struct LocalizedText
{
    const char* en;
    const char* zh_cn;
    const char* zh_tw;
    const char* ja;
};

struct GroupDef
{
    const char* id;
    LocalizedText title;
};

const std::vector<GroupDef> kGroupDefs = {
    {"chocola", {"Chocola", "巧克力", "巧克力", "チョコラ"}},
    {"vanilla", {"Vanilla", "香草", "香草", "バニラ"}},
    {"azuki", {"Azuki", "红豆", "紅豆", "あずき"}},
    {"coconut", {"Coconut", "椰子", "椰子", "ココナッツ"}},
    {"maple", {"Maple", "枫", "楓", "メイプル"}},
    {"cinnamon", {"Cinnamon", "桂", "桂", "シナモン"}},
    {"milk", {"Milk", "牛奶", "牛奶", "ミルク"}},
    {"fraise", {"Fraise", "草莓", "草莓", "フレーズ"}},
};

struct DisplayNameSet
{
    std::string en;
    std::string zh_cn;
    std::string zh_tw;
    std::string ja;
};

Json make_name_json(const DisplayNameSet& names)
{
    return Json::object(
        {{"en", names.en}, {"zh-CN", names.zh_cn}, {"zh-TW", names.zh_tw}, {"ja", names.ja}});
}

DisplayNameSet join_display_name(const DisplayNameSet& costume,
                                 const std::vector<DisplayNameSet>& parts)
{
    if (parts.empty())
        return {"Original", "原版", "原版", "オリジナル"};
    const char* sep[4] = {" · ", "·", "·", "・"};
    DisplayNameSet result = costume;
    std::string* fields[4] = {&result.en, &result.zh_cn, &result.zh_tw, &result.ja};
    for (std::size_t i = 0; i < 4; ++i)
    {
        for (const auto& part : parts)
        {
            const std::string* part_fields[4] = {&part.en, &part.zh_cn, &part.zh_tw, &part.ja};
            *fields[i] += sep[i];
            *fields[i] += *part_fields[i];
        }
    }
    return result;
}

DisplayNameSet verbatim_name(const std::string& id)
{
    return {id, id, id, id};
}

DisplayNameSet parse_display_names(const std::string& char_id, const std::string& model_id)
{
    std::string rem = model_id;
    if (rem.rfind(char_id + "-", 0) == 0)
        rem = rem.substr(char_id.size() + 1);
    else if (rem == char_id)
        return {"Original", "原版", "原版", "オリジナル"};

    const std::vector<std::pair<std::string, LocalizedText>> costumes = {
        {"france-maid", {"French maid", "法国女仆", "法國女僕", "フレンチメイド"}},
        {"wintermaid", {"Winter maid", "冬季女仆", "冬季女僕", "ウィンターメイド"}},
        {"casual", {"Casual", "日常", "日常", "私服"}},
        {"dress", {"Dress", "礼服", "禮服", "ドレス"}},
        {"lolita", {"Lolita", "洛丽塔", "洛麗塔", "ロリータ"}},
        {"maid", {"Maid", "女仆", "女僕", "メイド"}},
        {"pajama", {"Pajamas", "睡衣", "睡衣", "パジャマ"}},
        {"santa", {"Santa", "圣诞", "聖誕", "サンタ"}},
        {"winter", {"Winter", "冬季", "冬季", "ウインター"}},
        {"yukata", {"Yukata", "浴衣", "浴衣", "浴衣"}},
        {"teenage", {"Teenage", "少女", "少女", "ティーンエイジ"}},
        {"koneko", {"Kitten", "小猫", "小貓", "子猫"}},
        {"hawaii", {"Hawaii", "夏威夷", "夏威夷", "ハワイ"}},
        {"swimsuit", {"Swimsuit", "泳装", "泳裝", "水着"}},
        {"date", {"Date", "约会", "約會", "デート"}},
        {"shirt", {"Shirt", "衬衫", "襯衫", "シャツ"}},
        {"summer", {"Summer", "夏装", "夏裝", "夏服"}},
    };

    const std::vector<std::pair<std::string, LocalizedText>> variants = {
        {"no-bell-tail-pin",
         {"No bell / tail pin", "无铃铛 / 尾部固定", "無鈴鐺 / 尾部固定", "鈴なし・尾部固定"}},
        {"no-bell", {"No bell", "无铃铛", "無鈴鐺", "鈴なし"}},
        {"with-bell", {"With bell", "有铃铛", "有鈴鐺", "鈴付き"}},
        {"ohtoro", {"Ohtoro", "Ohtoro", "Ohtoro", "Ohtoro"}},
        {"custard", {"Custard", "卡仕达", "卡仕達", "カスタード"}},
        {"4yo", {"4yo", "4yo 版本", "4yo 版本", "4yo"}},
        {"v2", {"Version 2", "版本 2", "版本 2", "バージョン2"}},
        {"a", {"Pose A", "站姿 A", "站姿 A", "立ちポーズA"}},
        {"b", {"Pose B", "站姿 B", "站姿 B", "立ちポーズB"}},
        {"original", {"Original", "原版", "原版", "オリジナル"}},
    };

    auto localized_to_set = [](const LocalizedText& text) -> DisplayNameSet
    { return {text.en, text.zh_cn, text.zh_tw, text.ja}; };

    DisplayNameSet costume;
    bool costume_matched = false;
    for (const auto& [key, name] : costumes)
    {
        if (rem == key)
        {
            costume = localized_to_set(name);
            rem.clear();
            costume_matched = true;
            break;
        }
        if (rem.rfind(key + "-", 0) == 0)
        {
            costume = localized_to_set(name);
            rem = rem.substr(key.size() + 1);
            costume_matched = true;
            break;
        }
    }
    if (!costume_matched)
    {
        // Unknown costume ids are kept verbatim in every language.
        const auto dash = rem.find('-');
        if (dash != std::string::npos)
        {
            costume = verbatim_name(rem.substr(0, dash));
            rem = rem.substr(dash + 1);
        }
        else
        {
            costume = verbatim_name(rem);
            rem.clear();
        }
    }

    std::vector<DisplayNameSet> var_parts;
    while (!rem.empty())
    {
        bool matched = false;
        for (const auto& [key, name] : variants)
        {
            if (rem == key)
            {
                var_parts.push_back(localized_to_set(name));
                rem.clear();
                matched = true;
                break;
            }
            if (rem.rfind(key + "-", 0) == 0)
            {
                var_parts.push_back(localized_to_set(name));
                rem = rem.substr(key.size() + 1);
                matched = true;
                break;
            }
        }
        if (!matched)
        {
            const auto dash = rem.find('-');
            if (dash != std::string::npos)
            {
                var_parts.push_back(verbatim_name(rem.substr(0, dash)));
                rem = rem.substr(dash + 1);
            }
            else
            {
                var_parts.push_back(verbatim_name(rem));
                rem.clear();
            }
        }
    }

    if (var_parts.empty())
    {
        // A costume without variants is the "original" look of that costume.
        var_parts.push_back({"Original", "原版", "原版", "オリジナル"});
    }
    return join_display_name(costume, var_parts);
}

std::string find_char_id(const std::string& flat_name)
{
    for (const auto& [gid, _] : kGroupDefs)
    {
        if (flat_name == gid || flat_name.rfind(std::string(gid) + "-", 0) == 0)
            return gid;
    }
    const auto dash = flat_name.find('-');
    return (dash != std::string::npos) ? flat_name.substr(0, dash) : flat_name;
}

int migrate_layout(const fs::path& assets_dir)
{
    if (!fs::exists(assets_dir))
    {
        std::fprintf(stderr, "assets directory does not exist: %s\n", assets_dir.string().c_str());
        return 1;
    }

    const fs::path backup = assets_dir / ".backup-pre-migrate";
    if (!fs::exists(backup))
        fs::create_directories(backup);

    const fs::path characters_dir = assets_dir / "characters";
    if (fs::exists(characters_dir))
    {
        Json char_names_json = Json::object();
        for (const auto& entry : fs::directory_iterator(characters_dir))
        {
            if (!entry.is_directory())
                continue;
            const auto flat_dir = entry.path();
            const std::string flat_name = flat_dir.filename().string();
            if (flat_name.find('/') != std::string::npos)
                continue;
            if (flat_name.find('-') == std::string::npos)
            {
                const auto nested = characters_dir / flat_name / "default";
                if (!fs::exists(nested))
                    fs::create_directories(nested);
                for (const auto& f : fs::directory_iterator(flat_dir))
                {
                    const auto dest = nested / f.path().filename();
                    if (!fs::exists(dest))
                        fs::rename(f.path(), dest);
                }
                const std::string two_layer = flat_name + "/default";
                char_names_json[two_layer] =
                    make_name_json({"Original", "原版", "原版", "オリジナル"});
                continue;
            }
            const std::string char_id = find_char_id(flat_name);
            const std::string variant_id = flat_name.substr(char_id.size() + 1);
            const auto new_dir = characters_dir / char_id / variant_id;
            if (!fs::exists(new_dir.parent_path()))
                fs::create_directories(new_dir.parent_path());
            if (!fs::exists(new_dir))
                fs::rename(flat_dir, new_dir);
            const std::string two_layer = char_id + "/" + variant_id;
            char_names_json[two_layer] = make_name_json(parse_display_names(char_id, flat_name));
        }
        {
            std::ofstream f(characters_dir / "characters.json");
            f << char_names_json.dump(2) << '\n';
        }
    }

    const fs::path backgrounds_dir = assets_dir / "backgrounds";
    if (fs::exists(backgrounds_dir))
    {
        const auto old_names = backgrounds_dir / "background-names.json";
        const auto new_names = backgrounds_dir / "background.json";
        if (fs::exists(old_names) && !fs::exists(new_names))
            fs::rename(old_names, new_names);

        const auto dynamic_dir = backgrounds_dir / "dynamic";
        if (fs::exists(dynamic_dir))
        {
            std::unordered_map<std::string, std::array<fs::path, 3>> groups;
            for (const auto& entry : fs::directory_iterator(dynamic_dir))
            {
                if (!entry.is_regular_file())
                    continue;
                const auto path = entry.path();
                const auto ext = path.extension().string();
                if (ext != ".png" && ext != ".PNG")
                    continue;
                const auto stem = path.stem().string();
                if (stem.empty())
                    continue;
                const auto phase = stem.back();
                if (phase < 'A' || phase > 'C')
                    continue;
                const auto group = stem.substr(0, stem.size() - 1);
                groups[group][static_cast<std::size_t>(phase - 'A')] = path;
            }
            Json dyn_names_json = Json::object();
            for (const auto& [group, paths] : groups)
            {
                if (paths[0].empty() || paths[1].empty() || paths[2].empty())
                    continue;
                const auto group_dir = dynamic_dir / group;
                if (!fs::exists(group_dir))
                    fs::create_directories(group_dir);
                const char* phase_names[3] = {"A.png", "B.png", "C.png"};
                for (std::size_t i = 0; i < 3; ++i)
                {
                    const auto dest = group_dir / phase_names[i];
                    if (!fs::exists(dest))
                        fs::rename(paths[i], dest);
                }
                const auto config_path = group_dir / "config.json";
                if (!fs::exists(config_path))
                {
                    Json config = Json::object();
                    config["phases"] = Json::array({
                        Json::object({{"image", "A.png"}, {"start", "06:00"}}),
                        Json::object({{"image", "B.png"}, {"start", "16:00"}}),
                        Json::object({{"image", "C.png"}, {"start", "18:00"}}),
                    });
                    config["transition_seconds"] = 1.8;
                    std::ofstream cf(config_path);
                    cf << config.dump(2) << '\n';
                }
                dyn_names_json[group] = group;
            }
            if (!dyn_names_json.empty())
            {
                std::ofstream f(dynamic_dir / "background.json");
                f << dyn_names_json.dump(2) << '\n';
            }
        }
    }

    const auto old_icon = assets_dir / "icons" / "neko.png";
    const auto new_icon = assets_dir / "icons" / "toggle.png";
    if (fs::exists(old_icon) && !fs::exists(new_icon))
        fs::rename(old_icon, new_icon);

    const auto config_path = assets_dir / "config.json";
    if (fs::exists(config_path))
    {
        std::ifstream cf(config_path);
        if (cf)
        {
            try
            {
                Json config;
                cf >> config;
                cf.close();
                if (config.contains("character") && config["character"].is_string())
                {
                    const std::string flat = config["character"].get<std::string>();
                    if (flat.find('/') == std::string::npos)
                    {
                        const std::string char_id = find_char_id(flat);
                        if (flat == char_id)
                            config["character"] = char_id + "/default";
                        else
                            config["character"] = char_id + "/" + flat.substr(char_id.size() + 1);
                    }
                }
                std::ofstream of(config_path);
                of << config.dump(2) << '\n';
            }
            catch (...)
            {
                std::fprintf(stderr, "warning: failed to migrate config.json\n");
            }
        }
    }

    std::printf("migration complete: %s\n", assets_dir.string().c_str());
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc >= 2 && std::string(argv[1]) == "migrate-layout")
    {
        fs::path assets_dir;
        for (int i = 2; i < argc; ++i)
        {
            std::string a = argv[i];
            if (a == "--assets" && i + 1 < argc)
                assets_dir = argv[++i];
            else
            {
                std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
                print_usage();
                return 1;
            }
        }
        if (assets_dir.empty())
        {
            print_usage();
            return 1;
        }
        return migrate_layout(assets_dir);
    }

    if (argc < 3)
    {
        print_usage();
        return 1;
    }

    nekomote::asset_io::ImportOptions opts;
    opts.json_path = argv[1];
    opts.resx_path = argv[2];
    opts.resource_root = opts.json_path.parent_path();
    std::filesystem::path output, audit_report;

    for (int i = 3; i < argc; ++i)
    {
        std::string a = argv[i];
        if (a == "--texture" && i + 1 < argc)
        {
            const std::string mapping = argv[++i];
            const auto eq = mapping.find('=');
            if (eq == std::string::npos || eq == 0 || eq + 1 == mapping.size())
            {
                print_usage();
                return 1;
            }
            opts.external_textures.emplace(mapping.substr(0, eq), mapping.substr(eq + 1));
        }
        else if (a == "--audit-report" && i + 1 < argc)
        {
            audit_report = argv[++i];
        }
        else if (a == "--output" && i + 1 < argc)
        {
            output = argv[++i];
        }
        else if (a == "--resource-root" && i + 1 < argc)
        {
            opts.resource_root = argv[++i];
        }
        else
        {
            std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
            print_usage();
            return 1;
        }
    }

    if (output.empty() && audit_report.empty())
    {
        print_usage();
        return 1;
    }

    nekomote::asset_io::ImportResult result = nekomote::asset_io::import_emote(opts);
    const auto& r = result.report;

    std::printf("=== NekoMote import report ===\n");
    std::printf("ok: %s\n", result.ok ? "yes" : "no");
    std::printf("spec: %s  version: %.2f\n", result.asset.spec.c_str(),
                static_cast<double>(result.asset.version));
    std::printf("screen: %dx%d origin (%d, %d)\n", result.asset.screen_width,
                result.asset.screen_height, result.asset.screen_origin_x,
                result.asset.screen_origin_y);
    std::printf("parts: %d  layers: %d  frames: %d\n", r.parts_count, r.layers_count,
                r.frames_count);
    std::printf("icons: %d  textures: %d  variables: %d\n", r.icons_count, r.textures_count,
                r.variables_count);
    std::printf("bp_nonnull: %d  cc_nonnull: %d\n", result.asset.bp_nonnull,
                result.asset.cc_nonnull);
    std::printf("parts_order: [");
    for (std::size_t i = 0; i < result.asset.parts_order.size(); ++i)
    {
        std::printf("%s%s", i ? "," : "", result.asset.parts_order[i].c_str());
    }
    std::printf("]\n");
    if (!r.texture_path.empty())
    {
        std::printf("texture_path: %s\n", r.texture_path.c_str());
    }
    for (const std::string& w : r.warnings)
    {
        std::printf("warning: %s\n", w.c_str());
    }
    for (const std::string& e : r.errors)
    {
        std::printf("error: %s\n", e.c_str());
    }

    if (!audit_report.empty())
    {
        nlohmann::json report{{"ok", result.ok},
                              {"errors", r.errors},
                              {"unsupported_fields", r.unsupported_fields},
                              {"warnings", r.warnings},
                              {"parts", r.parts_count},
                              {"layers", r.layers_count},
                              {"frames", r.frames_count},
                              {"icons", r.icons_count},
                              {"textures", r.textures_count},
                              {"variables", r.variables_count},
                              {"bp", result.asset.bp_nonnull},
                              {"cc", result.asset.cc_nonnull}};
        std::ofstream file(audit_report);
        file << report.dump(2) << '\n';
        file.close();
        if (!file)
        {
            std::fprintf(stderr, "cannot write audit report\n");
            return 2;
        }
    }
    if (!result.ok)
        return 2;
    if (output.empty())
        return 0;
    const auto errors = nekomote::asset_io::write_bundle(result, opts.resource_root, output);
    for (const auto& e : errors)
        std::fprintf(stderr, "error: %s\n", e.c_str());
    return errors.empty() ? 0 : 2;
}
