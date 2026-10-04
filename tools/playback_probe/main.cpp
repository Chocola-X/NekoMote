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

#include "frame_json.hpp"

namespace
{
using Json = nlohmann::ordered_json;
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
// Fixed-step diagnostic dispatch only; JSON sampling/replay is excluded from
// the production author loop and benchmarks. Preserve frozen request order.
void timeline_requests(nekomote::runtime::PlaybackInstance& player,
                       const nekomote::runtime::AnimationProgram& program,
                       const nlohmann::ordered_json& requests)
{
    using namespace nekomote::runtime;
    if (!requests.is_array())
        throw std::runtime_error("timeline requests must be an array");
    for (const auto& request : requests)
    {
        const auto id = program.find_timeline(request.at("label").get<std::string>());
        const auto kind = request.at("kind").get<std::string>();
        const std::size_t expected_fields = kind == "blend"  ? 6
                                            : kind == "play" ? 3
                                            : kind == "stop" ? 2
                                                             : 4;
        if (!request.is_object() || request.size() != expected_fields)
            throw std::runtime_error("unexpected or missing timeline request fields");
        PlaybackStatus status;
        if (kind == "play")
        {
            const int flags = request.at("flags").get<int>();
            if (flags != 0 && flags != 1 && flags != 3)
                throw std::runtime_error("invalid play flags");
            status = player.play(id, flags == 0   ? TimelinePlayMode::Replace
                                     : flags == 1 ? TimelinePlayMode::Parallel
                                                  : TimelinePlayMode::AdditiveParallel);
        }
        else if (kind == "stop")
            status = player.stop(id);
        else if (kind == "fade_in")
            status = player.fade_in(id, request.at("frames").get<float>(),
                                    request.at("easing").get<float>());
        else if (kind == "fade_out")
            status = player.fade_out(id, request.at("frames").get<float>(),
                                     request.at("easing").get<float>());
        else if (kind == "blend")
        {
            const auto completion = request.at("completion").get<std::string>();
            if (completion != "keep" && completion != "stop")
                throw std::runtime_error("invalid weight completion");
            status = player.set_timeline_weight(
                id, request.at("value").get<float>(), request.at("frames").get<float>(),
                request.at("easing").get<float>(),
                completion == "stop" ? TimelineWeightCompletion::Stop
                                     : TimelineWeightCompletion::KeepPlaying);
        }
        else
            throw std::runtime_error("invalid timeline request kind");
        if (!status)
            throw std::runtime_error("timeline request failed animation=" +
                                     std::to_string(static_cast<int>(status.animation.error)));
    }
}
std::vector<nekomote::runtime::VariableSourceDefinition>
variable_sources(const nlohmann::ordered_json& script)
{
    std::vector<nekomote::runtime::VariableSourceDefinition> result;
    const auto collect = [&](const nlohmann::ordered_json& sources)
    {
        if (!sources.is_object())
            throw std::runtime_error("differences must be an object");
        for (const auto& [label, parameters] : sources.items())
        {
            if (!parameters.is_object())
                throw std::runtime_error("source parameters must be an object");
            auto found = std::find_if(result.begin(), result.end(),
                                      [&](const auto& s) { return s.label == label; });
            if (found == result.end())
            {
                result.push_back({label, {}});
                found = result.end() - 1;
            }
            for (const auto& [name, r] : parameters.items())
            {
                static_cast<void>(r);
                if (std::find(found->variables.begin(), found->variables.end(), name) ==
                    found->variables.end())
                    found->variables.push_back(name);
            }
        }
    };
    collect(script.value("differences", nlohmann::ordered_json::object()));
    for (const auto& e : script.value("events", nlohmann::ordered_json::array()))
        collect(e.value("differences", nlohmann::ordered_json::object()));
    return result;
}
void difference_requests(nekomote::runtime::PlaybackInstance& player,
                         const nekomote::runtime::PlaybackProgram& program,
                         const nlohmann::ordered_json& sources)
{
    if (!sources.is_object())
        throw std::runtime_error("differences must be an object");
    for (const auto& [label, parameters] : sources.items())
        for (const auto& [name, r] : parameters.items())
        {
            if (!r.is_object() || r.size() != 3)
                throw std::runtime_error("difference request requires value, frames and easing");
            const auto status = player.transition_difference(
                program.animation().find_variable_source(label),
                program.scene().find_variable(name), r.at("value").get<float>(),
                r.at("frames").get<float>(), r.at("easing").get<float>());
            if (!status)
                throw std::runtime_error("difference request failed animation=" +
                                         std::to_string(static_cast<int>(status.animation.error)));
        }
}
void validate_events(const nlohmann::ordered_json& events, int steps)
{
    if (!events.is_array())
        throw std::runtime_error("events must be an array");
    for (const auto& event : events)
    {
        if (!event.is_object() || !event.contains("step") ||
            !event.at("step").is_number_integer() || event.at("step").get<std::int64_t>() < 1 ||
            event.at("step").get<std::int64_t>() > steps)
            throw std::runtime_error("diagnostic events require an author step in 1..steps");
        for (const auto& [key, value] : event.items())
        {
            if (key != "step" && key != "timeline_requests" && key != "differences")
                throw std::runtime_error("unsupported diagnostic event field");
            if ((key == "timeline_requests" && !value.is_array()) ||
                (key == "differences" && !value.is_object()))
                throw std::runtime_error("invalid diagnostic event payload");
        }
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
    validate_events(request.value("events", Json::array()), steps);
    auto loaded = asset_io::load_asset(argv[1]);
    if (!loaded.ok)
        throw std::runtime_error(loaded.errors.front());
    const auto sources = variable_sources(request);
    auto compiled = runtime::compile_playback(loaded.asset, runtime::PhysicsMode::Enabled, sources);
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
    timeline_requests(player, program.animation(),
                      request.value("timeline_requests", Json::array()));
    difference_requests(player, program, request.value("differences", Json::object()));
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
    const auto timeline_states = [&]()
    {
        Json weights = Json::object(), playing = Json::object();
        const auto observe = [&](const Json& list)
        {
            for (const auto& r : list)
            {
                const auto name = r.at("label").get<std::string>();
                const auto id = program.animation().find_timeline(name);
                weights[name] = player.timeline_weight(id);
                playing[name] = player.is_playing(id);
            }
        };
        observe(request.value("plays", Json::array()));
        observe(request.value("timeline_requests", Json::array()));
        for (const auto& e : request.value("events", Json::array()))
            observe(e.value("timeline_requests", Json::array()));
        return std::pair{weights, playing};
    };
    const auto sample = [&](int step)
    {
        const auto [weights, playing] = timeline_states();
        Json differences = Json::object();
        for (const auto& source : sources)
            for (const auto& variable : source.variables)
                differences[source.label][variable] =
                    player.difference_value(program.animation().find_variable_source(source.label),
                                            scene.find_variable(variable));
        samples.push_back({{"differences", differences},
                           {"step", step},
                           {"values", values()},
                           {"blend_ratios", weights},
                           {"playing", playing}});
        if (request.value("observe_springs", false))
        {
            auto& list = samples.back()["springs"];
            list = Json::array();
            for (std::size_t i = 0;; ++i)
            {
                const auto s = player.observe_spring(i);
                if (!s)
                    break;
                list.push_back(
                    {{"position", s->state.position},
                     {"velocity", s->state.velocity},
                     {"origin_offset", {s->state.origin_offset.x, s->state.origin_offset.y}},
                     {"anchor_offset", {s->state.anchor_offset.x, s->state.anchor_offset.y}},
                     {"anchor", {s->anchor.x, s->anchor.y}}});
            }
        }
    };
    sample(0);
    for (int i = 0; i < steps; ++i)
    {
        for (const auto& event : request.value("events", Json::array()))
            if (event.at("step").get<int>() == i + 1)
            {
                timeline_requests(player, program.animation(),
                                  event.value("timeline_requests", Json::array()));
                difference_requests(player, program, event.value("differences", Json::object()));
            }
        check(player.step(physics));
        sample(i + 1);
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
        report["frame"] = diagnostics::frame_json(player.frame(), scene.textures());
        report["primitives"] = Json::array();
        for (const auto& primitive : player.frame_primitives())
            report["primitives"].push_back({{"node", primitive.node.value},
                                            {"label", primitive.label},
                                            {"source", primitive.source},
                                            {"texture", primitive.texture.value},
                                            {"first_index", primitive.draw.first_index},
                                            {"index_count", primitive.draw.index_count},
                                            {"opacity", primitive.opacity},
                                            {"mesh_division", primitive.mesh_division}});
    }
    if (request.value("observe_nodes", false))
    {
        report["nodes"] = Json::array();
        for (std::uint32_t i = 1; i <= scene.node_count(); ++i)
        {
            const auto value = player.observe_node(NodeId{i});
            if (!value)
                throw std::runtime_error("invalid node observation");
            const auto& n = *value;
            Json unit = Json::array(), world = Json::array();
            for (const auto p : n.unit_patch)
                unit.push_back({p.x, p.y});
            for (const auto p : n.world_patch)
                world.push_back({p.x, p.y});
            report["nodes"].push_back({{"id", i},
                                       {"label", scene.node_label(NodeId{i})},
                                       {"parent", n.parent.value},
                                       {"transform_parent", n.transform_parent.value},
                                       {"curve_parent", n.curve_parent.value},
                                       {"active", n.active},
                                       {"mesh", n.mesh},
                                       {"world", n.world},
                                       {"domain_corner", {n.domain_corner.x, n.domain_corner.y}},
                                       {"domain_size", {n.domain_size.x, n.domain_size.y}},
                                       {"domain_origin", {n.domain_origin.x, n.domain_origin.y}},
                                       {"domain_inverse", n.domain_inverse},
                                       {"unit_patch", unit},
                                       {"world_patch", world}});
        }
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
