#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/render_vk/renderer.hpp>

#include "vk_host.hpp"
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>
using namespace nekomote;
using Clock = std::chrono::steady_clock;
void require(bool v, const char* m)
{
    if (!v)
        throw std::runtime_error(m);
}
void stats(const char* label, std::vector<double> data)
{
    double sum = 0;
    for (auto x : data)
        sum += x;
    std::sort(data.begin(), data.end());
    auto n = data.size();
    std::printf("\"%s\":{\"mean\":%.3f,\"p50\":%.3f,\"p95\":%.3f,\"p99\":%.3f}", label,
                sum / static_cast<double>(n), data[n / 2], data[n * 95 / 100], data[n * 99 / 100]);
}
int main(int argc, char** argv)
{
    require(argc >= 5, "assets samples image-directory reuse|upload");
    bool reuse = std::string(argv[4]) == "reuse";
    int samples = std::stoi(argv[2]);
    require(glfwInit(), "glfw");
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto window = glfwCreateWindow(32, 32, "NekoMote Vulkan performance", nullptr, nullptr);
    require(window, "window");
    {
        player::VkHost host;
        std::uint32_t count = 0;
        auto extensions = glfwGetRequiredInstanceExtensions(&count);
        require(extensions && count, "extensions");
        host.create_instance({extensions, extensions + count}, player::ValidationPolicy::Disabled);
        VkSurfaceKHR surface;
        render_vk::check_vk(glfwCreateWindowSurface(host.instance, window, nullptr, &surface),
                            "surface");
        host.select_physical_device(surface);
        host.create_device();
        render_vk::Renderer shared(host.context());
        std::fprintf(stderr, "Vulkan %s\n", host.device_name.c_str());
        std::filesystem::create_directories(argv[3]);
        for (const char* name : {"chocola/date-a", "chocola/wintermaid", "maple/yukata"})
        {
            auto bundle = std::filesystem::path(argv[1]) / "characters" / name;
            auto model = asset_io::load_asset(bundle / "model.nkm");
            require(model.ok, "model");
            auto asset = asset_io::load_session_asset(bundle / "session.nks.json");
            require(asset.ok, "session");
            auto compiled = application::compile_session(model.asset, asset.asset);
            require(bool(compiled.program), "compile");
            for (int work = 0; work < 4; ++work)
            {
                const bool moving = work != 0;
                application::SessionConfig options;
                options.pointer = application::PointerScope::LocalSurface;
                options.animation.random_seed = 12345;
                options.reaction_seed = 12345;
                application::SessionInstance session(*compiled.program, options);
                require(bool(session.start()), "start");
                if (work >= 2)
                {
                    require(!asset.asset.reactions.empty(), "reactions");
                    const auto index = work == 2 ? 0 : asset.asset.reactions.size() - 1;
                    require(bool(session.react(compiled.program->find_reaction(
                                asset.asset.reactions[index].label))),
                            "react");
                }
                auto renderer = shared.create_peer();
                const auto& scene = compiled.program->playback().scene();
                auto cap = scene.frame_capacity();
                renderer.reserve({cap.vertices, cap.indices, cap.commands, scene.textures().size(),
                                  cap.mask_commands, cap.masks});
                for (auto t : scene.textures())
                {
                    auto img = asset_io::load_cropped_png(bundle / t.path);
                    require(img.ok, "image");
                    renderer.upload_texture(t.id, img.image, img.region);
                }
                renderer.resize_output(3840, 2160);
                std::vector<double> prepares(samples), cpu(samples), complete(samples);
                for (int i = -120; i < samples; ++i)
                {
                    if (moving)
                        require(bool(session.drag_origin(
                                    {float((i + 120) % 80 - 40), float((i + 120) % 40 - 20)})),
                                "drag");
                    while (session.pop_audio_command())
                    {
                    }
                    if (moving)
                    {
                        require(bool(session.pointer_move({float(i % 100), float(i % 80)})),
                                "pointer");
                        require(bool(session.set_scale(i < -40 ? .75f : 1.25f)), "scale");
                        if (i == -60 || i == -59)
                            require(bool(session.set_playback_state(
                                        i == -60 ? runtime::PlaybackState::Paused
                                                 : runtime::PlaybackState::Running)),
                                    "pause");
                    }
                    auto a = Clock::now();
                    require(bool(session.advance(moving && i % 13 == 0 ? .2 : 1.0 / 60)),
                            "advance");
                    require(bool(session.prepare_frame()), "frame");
                    auto b = Clock::now();
                    auto frame = session.frame();
                    if (!reuse)
                        frame.index_revision = 0;
                    renderer.prepare(frame);
                    renderer.resize_output(3840, 2160);
                    auto c = Clock::now();
                    auto target = host.begin_frame(3840, 2160, false);
                    renderer.render(target, {1920, 1200, .65f, {0, 0, 0, 0}});
                    host.present();
                    auto d = Clock::now();
                    host.wait_ready();
                    auto e = Clock::now();
                    if (i == -91 || i == -31)
                    {
                        std::string label = name;
                        std::replace(label.begin(), label.end(), '/', '-');
                        auto shot = host.capture(3840, 2160);
                        require(
                            asset_io::write_png(std::filesystem::path(argv[3]) /
                                                    (label + "-work" + std::to_string(work) +
                                                     "-tick" + std::to_string(i + 120) + ".png"),
                                                shot)
                                .empty(),
                            "snapshot");
                    }

                    if (i >= 0)
                    {
                        prepares[i] = std::chrono::duration<double, std::micro>(c - b).count();
                        cpu[i] = std::chrono::duration<double, std::micro>(d - a).count();
                        complete[i] = std::chrono::duration<double, std::micro>(e - a).count();
                    }
                }
                std::string label = name;
                std::replace(label.begin(), label.end(), '/', '-');
                auto image = host.capture(3840, 2160);
                require(
                    asset_io::write_png(std::filesystem::path(argv[3]) /
                                            (label + "-work" + std::to_string(work) + "-final.png"),
                                        image)
                        .empty(),
                    "png");
                std::printf("{\"model\":\"%s\",\"moving\":%s,\"work\":%d,\"api\":\"vulkan\","
                            "\"samples\":%d,",
                            name, moving ? "true" : "false", work, samples);
                stats("prepare_us", prepares);
                std::printf(",");
                stats("cpu_us", cpu);
                std::printf(",");
                stats("complete_us", complete);
                std::puts("}");
                std::fflush(stdout);
            }
        }
    }
    glfwDestroyWindow(window);
    glfwTerminate();
}
