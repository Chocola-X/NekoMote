#include <nekomote/application/controller.hpp>
#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>
static void require(bool b, const char* msg)
{
    if (!b)
        throw std::runtime_error(msg);
}
using namespace nekomote;
static void word(std::uint64_t& hash, std::uint32_t v, std::ofstream* dump)
{
    hash ^= v;
    hash *= 1099511628211ULL;
    if (dump)
        dump->write(reinterpret_cast<const char*>(&v), sizeof(v));
}
static void fingerprint(RenderFrame frame, std::uint64_t& hash, std::ofstream* dump)
{
    const auto put = [&](std::uint32_t v) { word(hash, v, dump); };
    put(static_cast<std::uint32_t>(frame.vertices.size()));
    put(static_cast<std::uint32_t>(frame.indices.size()));
    for (const auto& v : frame.vertices)
    {
        put(std::bit_cast<std::uint32_t>(v.x));
        put(std::bit_cast<std::uint32_t>(v.y));
        put(std::bit_cast<std::uint32_t>(v.u));
        put(std::bit_cast<std::uint32_t>(v.v));
        for (float c : v.color)
            put(std::bit_cast<std::uint32_t>(c));
    }
    for (auto i : frame.indices)
        put(i);
    const auto commands = [&](const auto& list)
    {
        put(static_cast<std::uint32_t>(list.size()));
        for (auto c : list)
        {
            put(c.texture.value);
            put(c.first_index);
            put(c.index_count);
            put(static_cast<std::uint32_t>(c.blend));
            put(static_cast<std::uint32_t>(c.protection));
            put(c.mask.value);
        }
    };
    commands(frame.commands);
    commands(frame.mask_commands);
    put(static_cast<std::uint32_t>(frame.composition));
    put(static_cast<std::uint32_t>(frame.masks.size()));
    for (auto m : frame.masks)
    {
        put(m.first_command);
        put(m.command_count);
    }
}
int main(int argc, char** argv)
try
{
    require(argc == 3 || argc == 4 || argc == 6,
            "assets steps [timelines|sessions [shard-index shard-count]]");
    const bool sessions = argc >= 4 && std::string(argv[3]) == "sessions";
    require(argc == 3 || sessions || std::string(argv[3]) == "timelines", "mode");
    const int shard = argc == 6 ? std::stoi(argv[4]) : 0;
    const int shards = argc == 6 ? std::stoi(argv[5]) : 1;
    require(shards > 0 && shard >= 0 && shard < shards, "shard");
    const int steps = std::stoi(argv[2]);
    require(steps > 0, "steps");
    std::vector<std::filesystem::path> models;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(
             std::filesystem::path(argv[1]) / "characters"))
        if (entry.path().filename() == "model.nkm")
            models.push_back(entry.path());
    std::sort(models.begin(), models.end());
    std::size_t model_index = 0;
    for (const auto& path : models)
    {
        if (model_index++ % static_cast<std::size_t>(shards) != static_cast<std::size_t>(shard))
            continue;
        const auto name = path.parent_path()
                              .lexically_relative(std::filesystem::path(argv[1]) / "characters")
                              .generic_string();
        if (sessions && name != "chocola/date-a" && name != "chocola/wintermaid" &&
            name != "chocola/casual-b-no-bell" && name != "vanilla/wintermaid" &&
            name != "maple/yukata" && name != "coconut/casual" && name != "azuki/casual")
            continue;
        auto asset = asset_io::load_asset(path);
        require(asset.ok, "asset");
        if (sessions)
        {
            const auto rules =
                asset_io::load_session_asset(path.parent_path() / "session.nks.json");
            require(rules.ok, "session asset");
            const auto compiled = application::compile_session(asset.asset, rules.asset);
            require(bool(compiled.program), "session compile");
            for (std::size_t reaction = 0; reaction <= rules.asset.reactions.size(); ++reaction)
            {
                application::SessionConfig config;
                config.animation.random_seed = config.reaction_seed = 12345;
                config.pointer = application::PointerScope::LocalSurface;
                application::SessionInstance session(*compiled.program, config);
                auto status = session.start();
                if (status && reaction != 0)
                {
                    const auto id =
                        compiled.program->find_reaction(rules.asset.reactions[reaction - 1].label);
                    require(id.valid(), "reaction ID");
                    status = session.react(id);
                }
                std::uint64_t hash = 1469598103934665603ULL;
                int tick = 0;
                for (; status && tick < steps; ++tick)
                {
                    while (session.pop_audio_command())
                    {
                    }
                    status = session.drag_origin({std::sin(static_cast<float>(tick) * .21f) * 120,
                                                  std::cos(static_cast<float>(tick) * .17f) * 80});
                    if (status)
                        status = session.pointer_move({static_cast<float>(tick % 80 - 40),
                                                       static_cast<float>(tick % 120 - 60)});
                    if (status)
                        status = session.set_scale(tick < steps / 2 ? .75f : 1.25f);
                    if (status)
                        status = session.set_audio_level(static_cast<float>(tick % 10) / 10);
                    if (status && (tick == steps / 2 || tick == steps / 2 + 1))
                        status = session.set_playback_state(tick == steps / 2
                                                                ? runtime::PlaybackState::Paused
                                                                : runtime::PlaybackState::Running);
                    if (status)
                        status = session.advance(tick % 7 == 0 ? .5 : 1.0 / 240);
                    if (status)
                        status = session.prepare_frame();
                    if (!status)
                        break;
                    fingerprint(session.frame(), hash, nullptr);
                    for (auto value : session.playback().values())
                        word(hash, std::bit_cast<std::uint32_t>(value.value), nullptr);
                    word(hash, static_cast<std::uint32_t>(session.reaction_phase()), nullptr);
                }
                const nlohmann::json row{
                    {"model", name},
                    {"reaction",
                     reaction == 0 ? "idle" : rules.asset.reactions[reaction - 1].label},
                    {"ticks", tick},
                    {"error", static_cast<int>(status.error)},
                    {"playback", static_cast<int>(status.playback.error)},
                    {"geometry", static_cast<int>(status.playback.geometry.error)},
                    {"node", status.playback.geometry.node.value},
                    {"hash", hash}};
                std::puts(row.dump().c_str());
                std::fflush(stdout);
            }
            continue;
        }
        auto program = runtime::compile_playback(asset.asset);
        require(bool(program.program), "compile");
        const auto run = [&](const std::string& label, TimelineId timeline, int work)
        {
            runtime::PlaybackInstance player(*program.program, {12345});
            auto status =
                timeline.valid()
                    ? player.play(timeline, work == 2 ? runtime::TimelinePlayMode::AdditiveParallel
                                                      : runtime::TimelinePlayMode::Replace)
                    : runtime::PlaybackStatus{};
            std::uint64_t hash = 1469598103934665603ULL;
            int tick = 0;
            for (; status && tick < steps; ++tick)
            {
                if (tick == steps / 2)
                {
                    player.set_playback_state(runtime::PlaybackState::Paused);
                }
                if (tick == steps / 2 + 1)
                {
                    player.set_playback_state(runtime::PlaybackState::Running);
                }
                if (work != 0)
                {
                    status = player.drag_origin({std::sin(static_cast<float>(tick) * .21f) * 120,
                                                 std::cos(static_cast<float>(tick) * .17f) * 80});
                    if (!status)
                        break;
                    status = player.set_scale(tick < steps / 2 ? .75f : 1.25f);
                    if (!status)
                        break;
                }
                status = player.advance(work == 2 && tick % 7 == 0 ? .1 : 1.0 / 60);
                if (status)
                    status = player.prepare_frame();
                if (!status)
                    break;
                fingerprint(player.frame(), hash, nullptr);
                for (auto v : player.values())
                    word(hash, std::bit_cast<std::uint32_t>(v.value), nullptr);
            }
            char hex[17];
            std::snprintf(hex, sizeof(hex), "%016llx", static_cast<unsigned long long>(hash));
            const nlohmann::json row{{"model", name},
                                     {"case", label},
                                     {"work", work},
                                     {"ticks", tick},
                                     {"error", static_cast<int>(status.error)},
                                     {"geometry", static_cast<int>(status.geometry.error)},
                                     {"node", status.geometry.node.value},
                                     {"physics", static_cast<int>(status.physics.error)},
                                     {"hash", hex}};
            std::puts(row.dump().c_str());
            std::fflush(stdout);
        };
        run("initial", {}, 0);
        run("drag-resize-pause", {}, 1);
        for (const auto& timeline : asset.asset.timelines)
        {
            const auto id = program.program->animation().find_timeline(timeline.label);
            require(id.valid(), "timeline ID");
            run(timeline.label, id, timeline.diff ? 2 : 1);
        }
    }
    if (shard != 0)
        return 0;
    // Exercise the shared controller's actual two-session switch/fade lifecycle.
    application::WallpaperController controller;
    controller.set_no_save(true);
    require(controller.scan_assets(argv[1]), "scan");
    application::ControllerConfig config;
    config.seed = 12345;
    config.mute = true;
    const auto& entries = controller.catalog().characters;
    std::uint64_t hash = 1469598103934665603ULL;
    std::size_t fading_frames = 0;
    require(!entries.empty(), "empty catalog");
    const auto count = std::min<std::size_t>(entries.size(), 12);
    for (std::size_t i = 0; i < count; ++i)
    {
        const auto index = sessions && count > 1 ? i * (entries.size() - 1) / (count - 1) : i;
        auto candidate = controller.load_candidate(entries[index], config);
        require(candidate.ok, "switch load");
        if (i == 0)
            controller.install_initial(std::move(candidate));
        else
            controller.confirm_candidate(std::move(candidate));
        for (int tick = 0; tick < steps; ++tick)
        {
            if (sessions)
            {
                controller.set_output_size(tick < steps / 2 ? 1920 : 3840,
                                           tick < steps / 2 ? 1080 : 2160);
                if (tick == 3)
                    controller.set_paused(true);
                if (tick == 4)
                    controller.set_paused(false);
                controller.set_output_unavailable(tick == 6);
                if (tick == 5)
                {
                    auto next =
                        controller.load_candidate(entries[(index + 1) % entries.size()], config);
                    require(next.ok, "interrupted switch load");
                    controller.confirm_candidate(std::move(next));
                }
                controller.pointer_move({static_cast<float>(tick % 100 - 50), 0});
                const auto now = application::WallpaperController::PointerTime{} +
                                 std::chrono::milliseconds(tick * 17);
                if (tick == 8)
                    controller.pointer_press({0, 0}, now);
                if (tick == 9)
                    controller.pointer_move({100, 80}, now);
                if (tick == 10)
                    controller.pointer_release({100, 80}, now);
                if (tick == steps - 1)
                    controller.reset();
            }
            controller.advance(tick % 11 == 0 ? .2 : 1.0 / 60);
            controller.prepare_frame();
            require(bool(controller.last_error()), "switch advance");
            require(controller.current_instance() != nullptr, "current");
            fingerprint(controller.current_instance()->frame(), hash, nullptr);
            if (auto* old = controller.previous_instance())
            {
                ++fading_frames;
                fingerprint(old->frame(), hash, nullptr);
            }
            word(hash, std::bit_cast<std::uint32_t>(controller.fade_alpha()), nullptr);
        }
    }
    std::printf("{\"switch_hash\":\"%016llx\"}\n", static_cast<unsigned long long>(hash));
    if (sessions)
    {
        require(fading_frames != 0, "fade coverage");
        std::printf("{\"switches\":%zu,\"fading_frames\":%zu}\n", count * 2, fading_frames);
    }
}
catch (const std::exception& error)
{
    std::fprintf(stderr, "%s\n", error.what());
    return 1;
}
