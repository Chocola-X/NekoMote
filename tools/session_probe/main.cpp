#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include "frame_json.hpp"

namespace
{
using Json = nlohmann::ordered_json;
using namespace nekomote;
void check(application::SessionStatus status)
{
    if (!status)
        throw std::runtime_error(
            "session=" + std::to_string(static_cast<int>(status.error)) +
            " playback=" + std::to_string(static_cast<int>(status.playback.error)) +
            " animation=" + std::to_string(static_cast<int>(status.playback.animation.error)) +
            " geometry=" + std::to_string(static_cast<int>(status.playback.geometry.error)) +
            " physics=" + std::to_string(static_cast<int>(status.playback.physics.error)));
}
Vec2 point(const Json& value)
{
    if (!value.is_array() || value.size() != 2)
        throw std::runtime_error("point requires x and y");
    return {value[0].get<float>(), value[1].get<float>()};
}
std::uint32_t seed(const Json& request, const char* name)
{
    const auto value = request.value(name, Json(0));
    if (!value.is_number_integer() || value.get<double>() < 0 ||
        value.get<double>() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error(std::string(name) + " requires an unsigned 32-bit integer");
    return value.get<std::uint32_t>();
}
int run(int argc, char** argv)
{
    if (argc != 5)
        throw std::runtime_error(
            "usage: nekomote_session_probe MODEL SESSION INPUT_JSON OUTPUT_JSON");
    std::ifstream input(argv[3]);
    const auto request = Json::parse(input);
    const auto steps = request.at("steps").get<int>();
    if (steps < 0 || steps > 100000)
        throw std::runtime_error("invalid step count");
    auto model = asset_io::load_asset(argv[1]);
    if (!model.ok)
        throw std::runtime_error(model.errors.front());
    auto definition = asset_io::load_session_asset(argv[2]);
    if (!definition.ok)
        throw std::runtime_error(definition.errors.front());
    auto compiled = application::compile_session(model.asset, definition.asset);
    if (!compiled.program)
        throw std::runtime_error(compiled.errors.front());
    model = {};
    definition = {};
    const auto& program = *compiled.program;
    const auto& scene = program.playback().scene();
    application::SessionInstance session(program, {{seed(request, "random_seed")},
                                                   seed(request, "reaction_seed"),
                                                   application::PointerScope::LocalSurface,
                                                   request.value("audio", false)
                                                       ? application::AudioMode::Device
                                                       : application::AudioMode::Disabled});
    check(session.set_origin(point(request.value("origin", Json::array({0, 400})))));
    check(session.set_scale(request.value("scale", 1.0f)));
    check(session.start());
    const auto events = request.value("events", Json::array());
    if (!events.is_array())
        throw std::runtime_error("events must be an array");
    for (const auto& e : events)
        if (!e.is_object() || !e.at("step").is_number_integer() || e.at("step").get<int>() < 0 ||
            e.at("step").get<int>() > steps)
            throw std::runtime_error("event step must be in 0..steps");
    Json samples = Json::array(), commands = Json::array();
    const auto dispatch = [&](int step)
    {
        for (const auto& e : events)
            if (e.at("step").get<int>() == step)
            {
                const auto kind = e.at("kind").get<std::string>();
                if (kind == "move")
                    check(session.pointer_move(point(e.at("point"))));
                else if (kind == "enter")
                    check(session.pointer_enter(point(e.at("point"))));
                else if (kind == "leave")
                    check(session.pointer_leave());
                else if (kind == "touch")
                    check(session.touch(point(e.at("point"))));
                else if (kind == "react")
                    check(session.react(program.find_reaction(e.at("label").get<std::string>())));
                else if (kind == "audio_level")
                    check(session.set_audio_level(e.at("rms").get<float>()));
                else
                    throw std::runtime_error("invalid session event kind");
            }
    };
    const auto sample = [&](int step)
    {
        Json values = Json::object(), differences = Json::object();
        for (const auto v : session.playback().values())
            values[scene.variables()[v.id.value - 1].name] = v.value;
        for (const auto* name : {"eyetrack", "lipSync"})
        {
            const auto source = program.playback().animation().find_variable_source(name);
            if (!source.valid())
                continue;
            for (const auto& variable : scene.variables())
                differences[name][variable.name] =
                    session.playback().difference_value(source, variable.id);
        }
        samples.push_back({{"step", step},
                           {"values", values},
                           {"differences", differences},
                           {"phase", static_cast<int>(session.reaction_phase())},
                           {"reaction", program.reaction_label(session.active_reaction())}});
        while (const auto command = session.pop_audio_command())
            commands.push_back({{"step", step},
                                {"kind", static_cast<int>(command->kind)},
                                {"clip", command->clip.value}});
    };
    dispatch(0);
    sample(0);
    for (int step = 1; step <= steps; ++step)
    {
        dispatch(step);
        check(session.advance(1.0 / 60));
        sample(step);
    }
    check(session.prepare_frame());
    Json output{{"production_playback_certified", false},
                {"presentation_mirror", program.mirror()},
                {"steps", session.playback().tick_count()},
                {"samples", samples},
                {"audio_commands", commands},
                {"frame", diagnostics::frame_json(session.frame(), scene.textures())}};
    constexpr std::array<const char*, nekomote::model::kTouchZoneCount> zones{"bust", "eye", "face",
                                                                              "head", "pant"};
    const auto regions = session.touch_regions();
    for (std::size_t i = 0; i < regions.size(); ++i)
    {
        if (!regions[i])
            output["touch_regions"][zones[i]] = nullptr;
        else
            output["touch_regions"][zones[i]] = {
                {"center", {regions[i]->center.x, regions[i]->center.y}},
                {"radius", {regions[i]->radius.x, regions[i]->radius.y}}};
    }
    std::ofstream file(argv[4]);
    if (!file)
        throw std::runtime_error("cannot open output");
    file << output.dump(2) << '\n';
    if (!file)
        throw std::runtime_error("cannot write output");
    return 0;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "nekomote_session_probe: %s\n", error.what());
        return 1;
    }
}
