#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/runtime/playback.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using Json = nlohmann::json;
using namespace nekomote;
void check_status(runtime::PlaybackStatus status, std::string_view node_label)
{
    if (!status)
        throw std::runtime_error(
            "playback error=" + std::to_string(static_cast<int>(status.error)) +
            " animation=" + std::to_string(static_cast<int>(status.animation.error)) +
            " geometry=" + std::to_string(static_cast<int>(status.geometry.error)) + " node=" +
            std::to_string(status.geometry.node.value) + " node_path=" + std::string(node_label) +
            " physics=" + std::to_string(static_cast<int>(status.physics.error)) +
            " controller=" + std::to_string(status.physics.controller));
}
runtime::TimelinePlayMode play_mode(int flags)
{
    switch (flags)
    {
        case 0:
            return runtime::TimelinePlayMode::Replace;
        case 1:
            return runtime::TimelinePlayMode::Parallel;
        case 3:
            return runtime::TimelinePlayMode::AdditiveParallel;
        default:
            throw std::runtime_error("play flags must be 0, 1, 3");
    }
}
int run(int argc, char** argv)
{
    if (argc != 4)
        throw std::runtime_error("usage: nekomote_playback_probe ASSET INPUT_JSON OUTPUT_JSON");
    std::ifstream input(argv[2]);
    if (!input)
        throw std::runtime_error("cannot open playback input");
    const auto request = Json::parse(input);
    const int steps = request.at("steps").get<int>();
    const int benchmark = request.value("benchmark", 0);
    if (steps < 0 || steps > 100000 || benchmark < 0 || benchmark > 100000)
        throw std::runtime_error("invalid step/sample count");
    auto loaded = asset_io::load_asset(argv[1]);
    if (!loaded.ok)
        throw std::runtime_error(loaded.errors.front());
    auto compiled = runtime::compile_playback(loaded.asset);
    if (!compiled.program)
        throw std::runtime_error(compiled.errors.front());
    loaded = {}; // program must retain everything the source asset provided
    const auto& program = *compiled.program;
    const auto& scene = program.scene();
    const auto check = [&](runtime::PlaybackStatus status)
    { check_status(status, scene.node_label(status.geometry.node)); };
    const auto seed = request.value("random_seed", Json(std::uint32_t{0}));
    if (!seed.is_number_integer() || seed.get<std::int64_t>() < 0 ||
        seed.get<std::uint64_t>() > std::numeric_limits<std::uint32_t>::max())
        throw std::runtime_error("random seed must be an unsigned 32-bit integer");
    runtime::PlaybackInstance player(program, runtime::AnimationConfig{seed.get<std::uint32_t>()});
    const auto origin = request.value("origin", Json::array({0, 0}));
    if (!origin.is_array() || origin.size() != 2)
        throw std::runtime_error("origin must contain exactly two author-space coordinates");
    check(player.set_origin({origin[0].get<float>(), origin[1].get<float>()}));
    const float scale = request.value("scale", 1.0f);
    check(player.set_scale(scale));
    const auto variables = request.value("variables", Json::object());
    const auto transitions = request.value("transitions", Json::object());
    for (const auto& [name, value] : variables.items())
        check(player.transition(scene.find_variable(name), value.get<float>()));
    for (const auto& [name, value] : transitions.items())
        check(player.transition(scene.find_variable(name), value.at("value").get<float>(),
                                value.at("frames").get<float>(), value.at("easing").get<float>()));
    for (const auto& play : request.value("plays", Json::array()))
        check(player.play(program.animation().find_timeline(play.at("label").get<std::string>()),
                          play_mode(play.at("flags").get<int>())));
    runtime::PhysicsInputs physics;
    const auto amplitudes = request.value("amplitudes", Json::object());
    physics.bust_amplitude = amplitudes.value("bust", 1.0f);
    physics.hair_amplitude = amplitudes.value("hair", 1.0f);
    physics.parts_amplitude = amplitudes.value("parts", 1.0f);
    const auto displacement = request.value("displacement", Json::object());
    const auto force = [&](const char* name)
    {
        const auto v = displacement.value(name, Json::array({0, 0}));
        if (!v.is_array() || v.size() != 2)
            throw std::runtime_error("displacement must contain exactly two coordinates");
        return Vec2{v[0].get<float>(), v[1].get<float>()};
    };
    physics.bust_displacement = force("bust");
    physics.hair_displacement = force("hair");
    physics.parts_displacement = force("parts");
    Json samples = Json::array();
    const auto values = [&]()
    {
        Json result = Json::object();
        for (const auto v : player.values())
            result[scene.variables()[v.id.value - 1].name] = v.value;
        return result;
    };
    samples.push_back({{"step", 0}, {"values", values()}});
    for (int i = 0; i < steps; ++i)
    {
        check(player.step(physics));
        samples.push_back({{"step", i + 1}, {"values", values()}});
    }
    check(player.prepare_frame());
    Json report{{"production_playback_certified", false},
                {"physics_enabled", true},
                {"origin", origin},
                {"scale", scale},
                {"steps", player.tick_count()},
                {"samples", samples},
                {"variables", values()},
                {"frame_vertices", player.frame().vertices.size()},
                {"frame_indices", player.frame().indices.size()}};
    if (request.value("dump_frame", false))
    {
        Json vertices = Json::array(), commands = Json::array(), textures = Json::array();
        for (const auto& v : player.frame().vertices)
            vertices.push_back({v.x, v.y, v.u, v.v});
        for (const auto& command : player.frame().commands)
            commands.push_back({{"texture", command.texture.value},
                                {"first_index", command.first_index},
                                {"index_count", command.index_count}});
        for (const auto& texture : scene.textures())
            textures.push_back(
                {{"id", texture.id.value}, {"width", texture.width}, {"height", texture.height}});
        report["frame"] = {{"vertices", vertices},
                           {"indices", std::vector<std::uint32_t>(player.frame().indices.begin(),
                                                                  player.frame().indices.end())},
                           {"commands", commands},
                           {"textures", textures}};
    }
    if (benchmark)
    {
        for (int i = 0; i < 120; ++i)
        {
            check(player.step(physics));
            check(player.prepare_frame());
        }
        std::vector<double> times(static_cast<std::size_t>(benchmark));
        for (auto& time : times)
        {
            const auto begin = std::chrono::steady_clock::now();
            const auto stepped = player.step(physics);
            const auto prepared = stepped ? player.prepare_frame() : stepped;
            time =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - begin)
                    .count();
            check(stepped);
            check(prepared);
        }
        std::sort(times.begin(), times.end());
        const auto percentile = [&](std::size_t p) { return times[(times.size() - 1) * p / 100]; };
        report["cpu_benchmark"] = {{"p50_ms", percentile(50)},
                                   {"p95_ms", percentile(95)},
                                   {"p99_ms", percentile(99)},
                                   {"warmup_ticks", 120},
                                   {"samples", benchmark},
                                   {"scope",
                                    "author animation+anchors+physics+final geometry+frame; "
                                    "no JSON samples, GPU, presentation or readback"}};
    }
    std::ofstream output(argv[3]);
    if (!output)
        throw std::runtime_error("cannot create output report");
    output << report.dump(2) << '\n';
    output.close();
    if (!output)
        throw std::runtime_error("failed to write output report");
    return 0;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        return run(argc, argv);
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "nekomote_playback_probe: %s\n", e.what());
        return 1;
    }
}
