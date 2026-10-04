#include <nekomote/asset_io/session_asset.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>
#include <string_view>

namespace nekomote::asset_io
{
namespace
{
using Json = nlohmann::json;
void require(bool condition, const std::string& path)
{
    if (!condition)
        throw std::runtime_error(path);
}
float finite(const Json& j, const std::string& path)
{
    require(j.is_number(), path + ": expected number");
    const auto value = j.get<float>();
    require(std::isfinite(value), path + ": nonfinite number");
    return value;
}
std::uint32_t ticks(const Json& j, const std::string& path)
{
    require(j.is_number_unsigned() || j.is_number_integer(), path + ": expected author ticks");
    const auto value = j.get<std::int64_t>();
    require(value >= 0 && value <= 3600000, path + ": ticks outside 0..3600000");
    return static_cast<std::uint32_t>(value);
}
std::string label(const Json& j, const std::string& path)
{
    require(j.is_string(), path + ": expected label");
    auto value = j.get<std::string>();
    require(value.size() <= 1024 && value.find('\0') == std::string::npos,
            path + ": invalid label");
    return value;
}
model::SessionAction action(const Json& j, const std::string& path)
{
    model::SessionAction result;
    result.main_timeline = label(j.at("main_timeline"), path + "/main_timeline");
    const auto& slots = j.at("difference_slots");
    require(slots.is_array() && slots.size() == result.difference_slots.size(),
            path + ": six slots required");
    for (std::size_t i = 0; i < slots.size(); ++i)
        result.difference_slots[i] = label(slots[i], path + "/difference_slots");
    const auto& variables = j.at("variables");
    require(variables.is_array() && variables.size() <= 256, path + ": invalid variable requests");
    for (const auto& v : variables)
    {
        model::SessionVariable variable;
        variable.label = label(v.at("label"), path + "/variable/label");
        require(!variable.label.empty(), path + ": empty variable label");
        variable.target = finite(v.at("target"), path + "/variable/target");
        variable.author_frames = finite(v.at("author_frames"), path + "/variable/author_frames");
        require(variable.author_frames >= 0, path + ": negative transition duration");
        variable.easing = finite(v.at("easing"), path + "/variable/easing");
        result.variables.push_back(std::move(variable));
    }
    return result;
}
constexpr std::array<std::string_view, model::kTouchZoneCount> kZones{"bust", "eye", "face", "head",
                                                                      "pant"};
} // namespace
SessionLoadResult load_session_asset(const std::filesystem::path& path)
{
    SessionLoadResult result;
    try
    {
        std::ifstream input(path, std::ios::binary);
        require(static_cast<bool>(input), path.string() + ": cannot open session asset");
        input.seekg(0, std::ios::end);
        const auto size = input.tellg();
        require(size >= 0 && size <= 4 * 1024 * 1024, "session asset exceeds 4 MiB");
        input.seekg(0);
        const auto document = Json::parse(input);
        require(document.at("schema") == "nekomote.session" &&
                    document.at("version") == model::kSessionAssetVersion,
                "unsupported session schema; rebuild with tools/import_session/import_session.py");
        const auto& j = document.at("session");
        auto& asset = result.asset;
        asset.model = label(j.at("model"), "session/model");
        asset.idle_timeline = label(j.at("idle_timeline"), "session/idle_timeline");
        asset.talk_max = finite(j.at("talk_max"), "session/talk_max");
        require(asset.talk_max > 0 && asset.talk_max <= 100, "session/talk_max outside 0..100");
        const auto& interaction = j.at("interaction");
        if (!interaction.at("eye").is_null())
            asset.interaction.eye = finite(interaction.at("eye"), "session/interaction/eye");
        for (std::size_t i = 0; i < kZones.size(); ++i)
        {
            const auto& region = interaction.at("regions").at(kZones[i]);
            if (region.is_null())
                continue;
            const auto& center = region.at("center");
            const auto& radius = region.at("radius");
            require(center.is_array() && center.size() == 2 && radius.is_array() &&
                        radius.size() == 2,
                    "session/interaction: region requires two center/radius coordinates");
            model::TouchEllipse ellipse{
                {finite(center[0], "region/center/x"), finite(center[1], "region/center/y")},
                {finite(radius[0], "region/radius/x"), finite(radius[1], "region/radius/y")}};
            require(ellipse.radius.x > 0 && ellipse.radius.y > 0, "region radius must be positive");
            asset.interaction.regions[i] = ellipse;
        }
        const auto& reactions = j.at("reactions");
        require(reactions.is_array() && reactions.size() <= 4096, "invalid reaction count");
        for (const auto& r : reactions)
        {
            model::ReactionAsset reaction;
            reaction.label = label(r.at("label"), "reaction/label");
            require(!reaction.label.empty(), "empty reaction label");
            require(std::none_of(asset.reactions.begin(), asset.reactions.end(),
                                 [&](const auto& previous)
                                 { return previous.label == reaction.label; }),
                    reaction.label + ": duplicate reaction label");
            const auto& zones = r.at("zones");
            require(zones.is_array() && !zones.empty() && zones.size() <= kZones.size(),
                    reaction.label + ": invalid zones");
            for (const auto& zone : zones)
            {
                const auto z = label(zone, reaction.label + "/zone");
                const auto found = std::find(kZones.begin(), kZones.end(), z);
                require(found != kZones.end(), reaction.label + ": unknown zone");
                const auto id = static_cast<model::TouchZone>(found - kZones.begin());
                require(std::find(reaction.zones.begin(), reaction.zones.end(), id) ==
                            reaction.zones.end(),
                        reaction.label + ": duplicate zone");
                reaction.zones.push_back(id);
            }
            reaction.action = action(r.at("action"), reaction.label + "/action");
            reaction.recovery = action(r.at("recovery"), reaction.label + "/recovery");
            reaction.audio_path = label(r.at("audio_path"), reaction.label + "/audio_path");
            const std::filesystem::path audio(reaction.audio_path);
            require(!audio.is_absolute(), reaction.label + ": absolute audio reference");
            for (const auto& component : audio)
                require(component != "..",
                        reaction.label + ": audio reference escapes session directory");
            reaction.duration_ticks =
                ticks(r.at("duration_ticks"), reaction.label + "/duration_ticks");
            require(reaction.duration_ticks > 0, reaction.label + ": empty duration");
            reaction.recovery_ticks =
                ticks(r.at("recovery_ticks"), reaction.label + "/recovery_ticks");
            const auto& beats = r.at("beats");
            require(beats.is_array() && beats.size() <= 256, reaction.label + ": invalid beats");
            for (const auto& b : beats)
            {
                model::ReactionBeat beat;
                beat.author_tick = ticks(b.at("author_tick"), reaction.label + "/beat/tick");
                require(beat.author_tick < reaction.duration_ticks &&
                            (reaction.beats.empty() ||
                             beat.author_tick > reaction.beats.back().author_tick),
                        reaction.label + ": unordered/unreachable beat");
                beat.action = action(b.at("action"), reaction.label + "/beat/action");
                reaction.beats.push_back(std::move(beat));
            }
            asset.reactions.push_back(std::move(reaction));
        }
        result.ok = true;
    }
    catch (const std::exception& error)
    {
        result.asset = {};
        result.errors.push_back(path.string() + ": " + error.what());
    }
    return result;
}
} // namespace nekomote::asset_io
