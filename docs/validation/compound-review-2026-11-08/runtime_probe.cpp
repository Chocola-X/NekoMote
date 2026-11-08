#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/session_asset.hpp>

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <vector>
thread_local bool track = false;
thread_local std::size_t allocations = 0;
void* operator new(std::size_t n)
{
    if (track)
        ++allocations;
    if (auto* p = std::malloc(n ? n : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n)
{
    return ::operator new(n);
}
void operator delete(void* p) noexcept
{
    std::free(p);
}
void operator delete[](void* p) noexcept
{
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept
{
    std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept
{
    std::free(p);
}
void require(bool b, const char* msg)
{
    if (!b)
        throw std::runtime_error(msg);
}
using Clock = std::chrono::steady_clock;
using namespace nekomote;
void word(std::uint64_t& hash, std::uint32_t v, std::ofstream* dump)
{
    hash ^= v;
    hash *= 1099511628211ULL;
    if (dump)
        dump->write(reinterpret_cast<const char*>(&v), sizeof(v));
}
void fingerprint(RenderFrame frame, std::uint64_t& hash, std::ofstream* dump)
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
void stats(const char* name, std::vector<double> values)
{
    double sum = 0;
    for (double v : values)
        sum += v;
    std::sort(values.begin(), values.end());
    const auto n = values.size();
    std::printf("\"%s\":{\"mean\":%.6f,\"p50\":%.6f,\"p95\":%.6f,\"p99\":%.6f}", name,
                sum / static_cast<double>(n), values[n / 2], values[n * 95 / 100],
                values[n * 99 / 100]);
}
int main(int argc, char** argv)
{
    require(argc >= 3, "assets samples [dump-directory]");
    const int samples = std::stoi(argv[2]);
    require(samples > 0, "samples");
    if (argc > 3)
        std::filesystem::create_directories(argv[3]);
    for (const char* name :
         {"chocola/date-a", "chocola/wintermaid", "chocola/casual-b-no-bell", "vanilla/wintermaid",
          "maple/yukata", "coconut/casual", "azuki/casual"})
    {
        auto bundle = std::filesystem::path(argv[1]) / "characters" / name;
        auto model = asset_io::load_asset(bundle / "model.nkm");
        require(model.ok, "model");
        auto asset = asset_io::load_session_asset(bundle / "session.nks.json");
        require(asset.ok, "session");
        auto compiled = application::compile_session(model.asset, asset.asset);
        require(bool(compiled.program), "compile");
        for (bool moving : {false, true})
        {
            application::SessionConfig config;
            config.animation.random_seed = 12345;
            config.reaction_seed = 12345;
            config.pointer = application::PointerScope::LocalSurface;
            application::SessionInstance session(*compiled.program, config);
            require(bool(session.start()), "start");
            runtime::GeometryInstance geometry(compiled.program->playback().scene());
            std::vector<double> advances(samples), prepares(samples), evaluates(samples),
                builds(samples), totals(samples);
            std::uint64_t hash = 1469598103934665603ULL;
            std::ofstream dump;
            if (argc > 3)
            {
                std::string label = name;
                std::replace(label.begin(), label.end(), '/', '-');
                dump.open(std::filesystem::path(argv[3]) /
                              (label + (moving ? "-moving.bin" : "-idle.bin")),
                          std::ios::binary);
                require(bool(dump), "dump");
            }
            allocations = 0;
            std::size_t vertices = 0, indices = 0;
            for (int i = -120; i < samples; ++i)
            {
                Vec2 origin{};
                if (moving)
                {
                    const float t = static_cast<float>(i + 120);
                    origin = {std::sin(t * .07f) * 80, std::cos(t * .05f) * 40};
                    require(bool(session.drag_origin(origin)), "drag");
                    require(bool(session.pointer_move(
                                {std::sin(t * .09f) * 100, std::cos(t * .08f) * 100})),
                            "pointer");
                }
                track = i >= 0;
                auto a = Clock::now();
                require(bool(session.advance(1.0 / 60)), "advance");
                auto b = Clock::now();
                require(bool(session.prepare_frame()), "prepare");
                auto c = Clock::now();
                track = false;
                if (i >= 0)
                {
                    advances[i] = std::chrono::duration<double, std::micro>(b - a).count();
                    prepares[i] = std::chrono::duration<double, std::micro>(c - b).count();
                    totals[i] = advances[i] + prepares[i];
                    fingerprint(session.frame(), hash, dump && i % 60 == 0 ? &dump : nullptr);
                }
                for (const auto value : session.playback().values())
                    require(bool(geometry.set_parameter(value.id, value.value)), "parameter");
                require(bool(geometry.set_origin(origin)), "geometry origin");
                track = i >= 0;
                a = Clock::now();
                require(bool(geometry.evaluate()), "evaluate");
                b = Clock::now();
                require(bool(geometry.build_frame()), "build");
                c = Clock::now();
                track = false;
                if (i >= 0)
                {
                    evaluates[i] = std::chrono::duration<double, std::micro>(b - a).count();
                    builds[i] = std::chrono::duration<double, std::micro>(c - b).count();
                }
                vertices = session.frame().vertices.size();
                indices = session.frame().indices.size();
            }
            std::printf(
                "{\"model\":\"%s\",\"moving\":%s,\"samples\":%d,\"vertices\":%zu,\"indices\":%zu,"
                "\"nodes\":%zu,\"allocations\":%zu,\"fingerprint\":\"%016llx\",",
                name, moving ? "true" : "false", samples, vertices, indices,
                compiled.program->playback().scene().node_count(), allocations,
                static_cast<unsigned long long>(hash));
            stats("advance_us", advances);
            std::printf(",");
            stats("prepare_us", prepares);
            std::printf(",");
            stats("total_us", totals);
            std::printf(",");
            stats("geometry_evaluate_us", evaluates);
            std::printf(",");
            stats("geometry_build_us", builds);
            std::puts("}");
            std::fflush(stdout);
        }
    }
}
