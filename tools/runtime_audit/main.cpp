// Explicit offline CPU coverage, never a substitute for WebGL image validation.
#include <nekomote/asset_io/import_emote.hpp>
#include <nekomote/runtime/playback.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{
using namespace nekomote;
using Json = nlohmann::json;

Json describe(runtime::PlaybackStatus status, const runtime::GeometryScene& scene)
{
    return {{"playback_error", static_cast<int>(status.error)},
            {"animation_error", static_cast<int>(status.animation.error)},
            {"geometry_error", static_cast<int>(status.geometry.error)},
            {"physics_error", static_cast<int>(status.physics.error)},
            {"node", status.geometry.node.value},
            {"node_path", scene.node_label(status.geometry.node)},
            {"parameter", status.geometry.parameter.value},
            {"physics_controller", status.physics.controller}};
}

Json sample(runtime::PlaybackInstance& instance, const runtime::GeometryScene& scene,
            unsigned steps)
{
    Json result{{"ok", true},
                {"sampled_author_ticks", 0},
                {"maximum_commands", 0},
                {"maximum_vertices", 0},
                {"maximum_masks", 0}};
    for (unsigned tick = 0; tick <= steps; ++tick)
    {
        auto status = tick == 0 ? runtime::PlaybackStatus{} : instance.step();
        if (status)
            status = instance.prepare_frame();
        if (!status)
        {
            result["ok"] = false;
            result["failed_author_tick"] = tick;
            result["failure"] = describe(status, scene);
            break;
        }
        const auto frame = instance.frame();
        result["sampled_author_ticks"] = tick;
        result["maximum_commands"] =
            std::max(result["maximum_commands"].get<std::size_t>(), frame.commands.size());
        result["maximum_vertices"] =
            std::max(result["maximum_vertices"].get<std::size_t>(), frame.vertices.size());
        result["maximum_masks"] =
            std::max(result["maximum_masks"].get<std::size_t>(), frame.masks.size());
    }
    return result;
}

Json audit(const asset_io::ImportOptions& options, unsigned steps)
{
    auto imported = asset_io::import_emote(options);
    Json report{{"structural_import_ok", imported.ok},
                {"compiled", false},
                {"ok", false},
                {"visual_certified", false},
                {"author_steps_per_case", steps},
                {"seed", 1},
                {"import_errors", imported.report.errors},
                {"import_warnings", imported.report.warnings},
                {"unsupported_fields", imported.report.unsupported_fields}};
    if (!imported.ok)
        return report;
    auto compiled = runtime::compile_playback(imported.asset);
    report["compile_errors"] = compiled.errors;
    if (!compiled.program)
        return report;
    report["compiled"] = true;
    const auto& program = *compiled.program;
    const auto& scene = program.scene();
    report["geometry_diagnostics"] = scene.diagnostics();
    report["animation_diagnostics"] = program.animation().diagnostics();
    runtime::PlaybackInstance instance(program, {1});
    report["initial_pose"] = sample(instance, scene, steps);
    bool ok = report["initial_pose"]["ok"].get<bool>();
    report["timelines"] = Json::array();
    for (const auto& timeline : imported.asset.timelines)
    {
        const auto id = program.animation().find_timeline(timeline.label);
        Json entry{{"label", timeline.label},
                   {"difference", timeline.diff != 0},
                   {"supported", program.animation().timeline_supported(id)}};
        instance.reset();
        const auto mode = timeline.diff != 0 ? runtime::TimelinePlayMode::AdditiveParallel
                                             : runtime::TimelinePlayMode::Replace;
        const auto status = instance.play(id, mode);
        if (!status)
        {
            entry["ok"] = false;
            entry["failure"] = describe(status, scene);
        }
        else
            entry.update(sample(instance, scene, steps));
        ok = ok && entry["ok"].get<bool>();
        report["timelines"].push_back(std::move(entry));
    }
    report["ok"] = ok;
    return report;
}
} // namespace

int main(int argc, char** argv)
{
    try
    {
        if (argc < 3)
            throw std::runtime_error("expected model.json and model.resx.json");
        nekomote::asset_io::ImportOptions options;
        options.json_path = argv[1];
        options.resx_path = argv[2];
        options.resource_root = options.json_path.parent_path();
        std::filesystem::path output;
        unsigned steps = 120;
        for (int i = 3; i < argc; ++i)
        {
            const std::string argument = argv[i];
            if (i + 1 >= argc)
                throw std::runtime_error("missing argument value: " + argument);
            const std::string value = argv[++i];
            if (argument == "--output")
                output = value;
            else if (argument == "--resource-root")
                options.resource_root = value;
            else if (argument == "--texture")
            {
                const auto equals = value.find('=');
                if (equals == 0 || equals == std::string::npos || equals + 1 == value.size())
                    throw std::runtime_error("expected --texture name=relative-path");
                if (!options.external_textures
                         .emplace(value.substr(0, equals), value.substr(equals + 1))
                         .second)
                    throw std::runtime_error("duplicate texture mapping");
            }
            else if (argument == "--steps")
            {
                const auto parsed =
                    std::from_chars(value.data(), value.data() + value.size(), steps);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() ||
                    steps == 0 || steps > 36000)
                    throw std::runtime_error("steps must be an integer in 1..36000");
            }
            else
                throw std::runtime_error("unknown argument: " + argument);
        }
        if (output.empty())
            throw std::runtime_error("--output is required");
        const auto report = audit(options, steps);
        std::ofstream file(output);
        file << report.dump(2) << '\n';
        file.close();
        if (!file)
            throw std::runtime_error("cannot write CPU audit report");
        std::printf("compiled=%s cpu_cases_ok=%s visual_certified=no\n",
                    report["compiled"].get<bool>() ? "yes" : "no",
                    report["ok"].get<bool>() ? "yes" : "no");
        return report["ok"].get<bool>() ? 0 : 2;
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "runtime audit: %s\n", error.what());
        return 1;
    }
}
