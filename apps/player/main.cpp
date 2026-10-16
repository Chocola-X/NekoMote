#include <nekomote/application/controller.hpp>
#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/audio.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/audio/device.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_gl/ui_renderer.hpp>
#include <nekomote/ui/layout.hpp>
#include <nekomote/ui/menu.hpp>

#include <cstdlib>
#include <cstring>

#include "backend.hpp"
#include "touch_overlay.hpp"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <glad/gl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{
using namespace nekomote;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;

struct Options
{
    fs::path model, session, background, capture, check_window, assets, capture_ui;
    std::string reaction, character, background_name;
    int width{1920}, height{1080}, steps{0}, measure{0}, fps{0}, ui_drawer{-1};
    bool mute{false}, transparent{false}, fit{false}, no_save{false}, ui_open{false};
    bool show_touch_regions{false};
    bool scale_set{false}, origin_x_set{false}, origin_y_set{false};
    player::BackendKind backend{player::BackendKind::OpenGL};
    bool vulkan() const noexcept
    {
        return backend == player::BackendKind::Vulkan;
    }
    std::uint32_t seed{1};
    float scale{1};
    Vec2 origin{0, 400};
};
std::optional<Options> options(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string key = argv[i];
        const auto value = [&]() -> std::string
        {
            if (++i >= argc)
                throw std::runtime_error("missing value for " + key);
            return argv[i];
        };
        if (key == "--model")
            o.model = value();
        else if (key == "--session")
            o.session = value();
        else if (key == "--background")
            o.background = value();
        else if (key == "--assets")
            o.assets = value();
        else if (key == "--character")
            o.character = value();
        else if (key == "--background-name")
            o.background_name = value();
        else if (key == "--no-save")
            o.no_save = true;
        else if (key == "--fit")
            o.fit = true;
        else if (key == "--mute")
            o.mute = true;
        else if (key == "--transparent")
            o.transparent = true;
        else if (key == "--show-touch-regions")
            o.show_touch_regions = true;
        else if (key == "--backend")
        {
            const auto name = value();
            if (name == "opengl")
                o.backend = player::BackendKind::OpenGL;
            else if (name == "vulkan")
                o.backend = player::BackendKind::Vulkan;
            else
                throw std::runtime_error("backend must be opengl or vulkan");
        }
        else if (key == "--scale")
        {
            o.scale = std::stof(value());
            o.scale_set = true;
        }
        else if (key == "--origin-x")
        {
            o.origin.x = std::stof(value());
            o.origin_x_set = true;
        }
        else if (key == "--origin-y")
        {
            o.origin.y = std::stof(value());
            o.origin_y_set = true;
        }
        else if (key == "--width")
            o.width = std::stoi(value());
        else if (key == "--height")
            o.height = std::stoi(value());
        else if (key == "--check-window")
            o.check_window = value();
        else if (key == "--capture")
            o.capture = value();
        else if (key == "--capture-ui")
            o.capture_ui = value();
        else if (key == "--ui-open")
            o.ui_open = true;
        else if (key == "--ui-drawer")
            o.ui_drawer = std::stoi(value());
        else if (key == "--steps")
            o.steps = std::stoi(value());
        else if (key == "--fps")
        {
            const auto text = value();
            std::size_t consumed = 0;
            o.fps = std::stoi(text, &consumed);
            if (consumed != text.size())
                throw std::runtime_error("--fps requires an integer in 5..240");
        }
        else if (key == "--measure-frames")
            o.measure = std::stoi(value());
        else if (key == "--reaction")
            o.reaction = value();
        else if (key == "--seed")
        {
            const auto text = value();
            std::size_t consumed = 0;
            const auto number = std::stoull(text, &consumed);
            if (consumed != text.size() || text.front() == '-' || number > 0xffffffffULL)
                throw std::runtime_error("seed requires unsigned 32-bit integer");
            o.seed = static_cast<std::uint32_t>(number);
        }
        else if (key == "--help")
        {
            std::puts(
                "nekomote_player --assets <dir> [--character name] [--background-name name]\n"
                "  [--no-save] [--mute] [--fit] [--scale 1] [--origin-x 0 --origin-y 400]\n"
                "  [--width 800 --height 1080] [--fps 60] [--seed 1]\n"
                "  [--backend opengl|vulkan] (default opengl; Vulkan requires enabled build)\n"
                "Or: nekomote_player --model model.nkm.json --session session.nks.json\n"
                "  [--background image.png] [--mute] [--fit] [--scale 1] [--origin-x 0 --origin-y "
                "400]\n"
                "  [--width 800 --height 1080] [--fps 60] [--seed 1] [--reaction label]\n"
                "  [--capture output.png --steps N --mute --transparent] [--measure-frames N]\n"
                "  [--capture-ui output.png --steps N --mute --ui-open --ui-drawer N]\n"
                "  Diagnostic: --check-window output-directory --mute (no OS input injection)\n"
                "  Diagnostic: --show-touch-regions (bust red, eye yellow, face green, "
                "head cyan, pant purple); also included in captures.\n"
                "Assets mode keys: Tab/1-9 switch character, B/N switch background, C clear "
                "background.\n"
                "Mouse: gaze; left short release: touch; left hold 300ms: drag; wheel: zoom.\n"
                "Space: pause/resume; F: fit whole model; R: reset; Escape: close. Pointer is "
                "local to this window.");
            return std::nullopt;
        }
        else
            throw std::runtime_error("unknown option " + key);
    }
    if (!o.capture_ui.empty())
    {
        o.capture = o.capture_ui;
    }
    if (o.assets.empty() && (o.model.empty() || o.session.empty()))
        throw std::runtime_error("--assets or (--model and --session) required; see --help");
    if (o.width < 64 || o.width > 8192 || o.height < 64 || o.height > 8192 || o.steps < 0 ||
        o.steps > 100000 || (o.fps != 0 && (o.fps < 5 || o.fps > 240)) || o.measure < 0 ||
        o.measure > 1000000 || !std::isfinite(o.scale) || o.scale <= 0 ||
        !std::isfinite(o.origin.x) || !std::isfinite(o.origin.y))
        throw std::runtime_error(
            "invalid dimensions, FPS (5..240), step count, measurement count or placement");
    if (!o.capture.empty() && (!o.mute || o.measure != 0))
        throw std::runtime_error(
            "fixed-step screenshot requires --mute and excludes presentation measurement");
    if (!o.check_window.empty() && (!o.mute || o.measure || !o.capture.empty()))
        throw std::runtime_error("--check-window requires --mute and excludes capture/measurement");
    if (o.capture.empty() && o.steps != 0)
        throw std::runtime_error("--steps requires --capture");
    if (o.show_touch_regions && o.measure != 0)
        throw std::runtime_error("touch-region diagnostics exclude performance measurement");
    return o;
}
std::string_view geometry_error_name(runtime::GeometryError error) noexcept
{
    switch (error)
    {
        case runtime::GeometryError::None:
            return "None";
        case runtime::GeometryError::InvalidParameter:
            return "InvalidParameter";
        case runtime::GeometryError::NonfiniteParameter:
            return "NonfiniteParameter";
        case runtime::GeometryError::UnsupportedClamp:
            return "UnsupportedClamp";
        case runtime::GeometryError::UnsupportedDiscretization:
            return "UnsupportedDiscretization";
        case runtime::GeometryError::UnsupportedTransform:
            return "UnsupportedTransform";
        case runtime::GeometryError::UnsupportedCombination:
            return "UnsupportedCombination";
        case runtime::GeometryError::UnsupportedTessellation:
            return "UnsupportedTessellation";
        case runtime::GeometryError::UnsupportedMask:
            return "UnsupportedMask";
        case runtime::GeometryError::InvalidLocator:
            return "InvalidLocator";
        case runtime::GeometryError::InvalidScale:
            return "InvalidScale";
    }
    return "Unknown";
}
void check(application::SessionStatus status, const application::SessionProgram* program = nullptr,
           std::string_view character = {})
{
    if (status)
        return;
    std::string message = "session failure " + std::to_string(static_cast<int>(status.error)) +
                          "/" + std::to_string(static_cast<int>(status.playback.error));
    if (!character.empty())
        message += "; character='" + std::string(character) + "'";
    if (status.playback.error == runtime::PlaybackError::Geometry)
    {
        const auto& geometry = status.playback.geometry;
        message += "; geometry=" + std::string(geometry_error_name(geometry.error));
        if (program)
            message += "; node='" +
                       std::string(program->playback().scene().node_label(geometry.node)) + "'";
        if (geometry.error == runtime::GeometryError::UnsupportedTransform)
            message += "; selected model uses unimplemented or unverified rendering semantics "
                       "(e.g. inheritance, bm or attr); select another model with --character";
    }
    throw std::runtime_error(message);
}
struct Glfw
{
    Glfw()
    {
        glfwSetErrorCallback([](int code, const char* text)
                             { std::fprintf(stderr, "GLFW %d: %s\n", code, text); });
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");
        std::fprintf(stderr, "Window platform=%s\n",
                     glfwGetPlatform() == GLFW_PLATFORM_WAYLAND ? "wayland" : "x11");
    }
    ~Glfw()
    {
        glfwTerminate();
    }
};
struct DestroyWindow
{
    void operator()(GLFWwindow* w) const noexcept
    {
        glfwDestroyWindow(w);
    }
};

struct GpuHolder
{
    std::unique_ptr<player::BackendRenderer> renderer;
    std::unique_ptr<player::BackendRenderer> previous_renderer;
    std::unique_ptr<player::BackendRenderer> background_renderer;
    std::unique_ptr<audio::Device> device;
    std::shared_ptr<const model::AudioBank> audio_clips;
    TextureId current_background_id{0};
    fs::path current_background_path;
    int background_width{0}, background_height{0};
};

void finish_candidate_gpu(GpuHolder& gpu, const application::CandidateCpu& candidate,
                          player::BackendKind backend
#if defined(NEKOMOTE_HAS_VULKAN)
                          ,
                          const nekomote::render_vk::DeviceContext* vk_ctx = nullptr
#endif
)
{
    gpu.renderer = std::make_unique<player::BackendRenderer>(backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                                             ,
                                                             vk_ctx
#endif
    );
    std::printf("%s\n", gpu.renderer->diagnostics().c_str());
    const auto& scene = candidate.program->playback().scene();
    const auto capacity = scene.frame_capacity();
    gpu.renderer->reserve({capacity.vertices, capacity.indices, capacity.commands,
                           scene.textures().size(), capacity.mask_commands, capacity.masks});
    for (const auto& [id, image, region] : candidate.textures)
        gpu.renderer->upload_texture(id, image, region);
}

std::unique_ptr<audio::Device> create_audio_device(std::shared_ptr<const model::AudioBank> clips)
{
    auto device = std::make_unique<audio::Device>(std::move(clips));
    const auto deadline = Clock::now() + std::chrono::seconds(6);
    while (device->snapshot().state == audio::DeviceState::Connecting && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const auto snapshot = device->snapshot();
    if (snapshot.state != audio::DeviceState::Ready)
        throw std::runtime_error("audio device unavailable (error " +
                                 std::to_string(snapshot.error) +
                                 "); use --mute for silent playback");
    return device;
}

struct GpuCandidate
{
    GpuHolder gpu;
    enum class BackgroundOwnership
    {
        Prepared,
        RetainCurrent
    };
    BackgroundOwnership background{BackgroundOwnership::Prepared};
};

// Prepare every fallible device operation against a private candidate. The
// caller must finish any recorded/submitted frame before replacing current GPU
// owners, then commit the CPU candidate and these ready resources together.
GpuCandidate prepare_candidate_gpu(application::CandidateCpu& candidate, const GpuHolder& current,
                                   const fs::path& background, int width, int height,
                                   player::BackendKind backend, application::AudioMode audio_mode
#if defined(NEKOMOTE_HAS_VULKAN)
                                   ,
                                   const nekomote::render_vk::DeviceContext* vk_ctx = nullptr
#endif
)
{
    GpuCandidate result;
    finish_candidate_gpu(result.gpu, candidate, backend
#if defined(NEKOMOTE_HAS_VULKAN)
                         ,
                         vk_ctx
#endif
    );
    check(candidate.instance->set_output_size(std::max(width, 1), std::max(height, 1)),
          candidate.program.get(), candidate.name);
    if (!background.empty())
    {
        TextureId id = current.current_background_id;
        if (current.background_renderer && background == current.current_background_path)
        {
            result.background = GpuCandidate::BackgroundOwnership::RetainCurrent;
            result.gpu.background_width = current.background_width;
            result.gpu.background_height = current.background_height;
        }
        else
        {
            auto loaded = asset_io::load_png(background);
            if (!loaded.ok)
                throw std::runtime_error(loaded.error);
            result.gpu.background_renderer = std::make_unique<player::BackendRenderer>(backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                                                                       ,
                                                                                       vk_ctx
#endif
            );
            result.gpu.background_renderer->reserve({8, 12, 2, 2});
            id = TextureId{1};
            result.gpu.background_renderer->upload_texture(id, loaded.image);
            result.gpu.background_width = loaded.image.width;
            result.gpu.background_height = loaded.image.height;
        }
        check(candidate.instance->set_background(id, result.gpu.background_width,
                                                 result.gpu.background_height),
              candidate.program.get(), candidate.name);
        result.gpu.current_background_id = id;
        result.gpu.current_background_path = background;
    }
    check(candidate.instance->prepare_frame(), candidate.program.get(), candidate.name);
    result.gpu.renderer->prepare(candidate.instance->frame());
    result.gpu.renderer->resize_output(width, height);
    if (result.gpu.background_renderer)
    {
        result.gpu.background_renderer->prepare(candidate.instance->background_frame());
        result.gpu.background_renderer->resize_output(width, height);
    }
    result.gpu.audio_clips =
        std::make_shared<const model::AudioBank>(std::move(candidate.audio_clips));
    if (audio_mode == application::AudioMode::Device && !result.gpu.audio_clips->empty())
        result.gpu.device = create_audio_device(result.gpu.audio_clips);
    return result;
}

void commit_candidate_gpu(GpuHolder& current, GpuCandidate ready) noexcept
{
    if (ready.background == GpuCandidate::BackgroundOwnership::RetainCurrent)
        ready.gpu.background_renderer = std::move(current.background_renderer);
    ready.gpu.previous_renderer = std::move(current.renderer);
    current = std::move(ready.gpu);
}

void apply_background_gpu(GpuHolder& gpu, const fs::path& bg_path, double fade_duration,
                          int bg_index, application::WallpaperController& controller,
                          player::BackendKind backend
#if defined(NEKOMOTE_HAS_VULKAN)
                          ,
                          const nekomote::render_vk::DeviceContext* vk_ctx = nullptr
#endif
)
{
    auto new_image = asset_io::load_png(bg_path);
    if (!new_image.ok)
        throw std::runtime_error(new_image.error);
    const bool fade = controller.has_background() && fade_duration > 0;
    std::optional<asset_io::DecodedImageRgba8> old_image;
    if (fade)
    {
        auto loaded = asset_io::load_png(gpu.current_background_path);
        if (!loaded.ok)
            throw std::runtime_error(loaded.error);
        old_image = std::move(loaded.image);
    }
    auto ready_path = bg_path;
    auto renderer = std::make_unique<player::BackendRenderer>(backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                                              ,
                                                              vk_ctx
#endif
    );
    renderer->reserve({8, 12, 2, 2});
    const TextureId old_id{1}, new_id{fade ? 2U : 1U};
    if (old_image)
        renderer->upload_texture(old_id, *old_image);
    renderer->upload_texture(new_id, new_image.image);
    // CPU confirmation cannot discover an unsupported texture after this point.
    controller.confirm_background(bg_index, old_id, new_id, new_image.image.width,
                                  new_image.image.height, fade ? fade_duration : 0);
    gpu.background_renderer = std::move(renderer);
    gpu.current_background_id = new_id;
    gpu.current_background_path = std::move(ready_path);
    gpu.background_width = new_image.image.width;
    gpu.background_height = new_image.image.height;
}

struct Surface
{
    application::WallpaperController* controller;
    GLFWwindow* window;
    bool locked{false};
    bool paused{false}, minimized{false}, output_unavailable{false};
    ui::Context* ui_context{nullptr};
    const ui::IconState* ui_icon{nullptr};
    const ui::SidebarState* ui_sidebar{nullptr};
    bool captures_mouse() const noexcept
    {
        return ui_context && ui_icon && ui_sidebar &&
               ui_context->eval_captures_mouse(*ui_icon, *ui_sidebar);
    }
    void set_paused(bool value) noexcept
    {
        paused = value;
        controller->set_paused(value);
    }
    void set_minimized(bool value) noexcept
    {
        minimized = value;
        controller->set_minimized(value);
    }
    void set_output_unavailable(bool value) noexcept
    {
        output_unavailable = value;
        controller->set_output_unavailable(value);
    }
    Vec2 point(double x, double y) const noexcept
    {
        int w = 0, h = 0, fw = 0, fh = 0;
        glfwGetWindowSize(window, &w, &h);
        glfwGetFramebufferSize(window, &fw, &fh);
        return {static_cast<float>(w > 0 ? x * fw / w - fw * .5 : 0),
                static_cast<float>(h > 0 ? y * fh / h - fh * .5 : 0)};
    }
    static Surface& get(GLFWwindow* w) noexcept
    {
        return *static_cast<Surface*>(glfwGetWindowUserPointer(w));
    }
    static void move(GLFWwindow* w, double x, double y) noexcept
    {
        auto& s = get(w);
        const auto p = s.point(x, y);
        if (s.ui_context)
            s.ui_context->mouse_move(x, y);
        if (s.captures_mouse())
        {
            s.controller->pointer_leave();
            return;
        }
        if (s.locked)
            s.controller->cancel_pointer_gesture();
        s.controller->pointer_move(p);
    }
    static void enter(GLFWwindow* w, int entered) noexcept
    {
        auto& s = get(w);
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        if (entered)
        {
            auto p = s.point(x, y);
            s.controller->pointer_enter(p);
        }
        else
        {
            s.controller->pointer_leave();
        }
    }
    static void button(GLFWwindow* w, int button, int action, int) noexcept
    {
        auto& s = get(w);
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        if (s.ui_context)
        {
            s.ui_context->mouse_move(x, y);
            s.ui_context->mouse_button(button, action);
        }
        if (s.captures_mouse() || s.paused || s.minimized || s.locked)
        {
            s.controller->cancel_pointer_gesture();
            return;
        }
        if (button == GLFW_MOUSE_BUTTON_LEFT)
        {
            if (action == GLFW_PRESS)
                s.controller->pointer_press(s.point(x, y));
            else if (action == GLFW_RELEASE)
                s.controller->pointer_release(s.point(x, y));
        }
    }
    static void scroll(GLFWwindow* w, double, double amount) noexcept
    {
        auto& s = get(w);
        if (s.ui_context)
        {
            s.ui_context->mouse_scroll(amount);
            if (s.captures_mouse())
                return;
        }
        if (s.locked)
            return;
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        const auto p = s.point(x, y);
        s.controller->zoom_at(p, amount);
    }
    static void iconify(GLFWwindow* w, int minimized) noexcept
    {
        auto& s = get(w);
        s.set_minimized(minimized != 0);
    }
    void fit() noexcept
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        controller->fit_to_output(w, h);
    }
    static void key(GLFWwindow* w, int key, int, int action, int) noexcept
    {
        if (action != GLFW_PRESS)
            return;
        auto& s = get(w);
        if (key == GLFW_KEY_ESCAPE)
            glfwSetWindowShouldClose(w, GLFW_TRUE);
        else if (key == GLFW_KEY_SPACE)
        {
            s.set_paused(!s.paused);
        }
        else if (key == GLFW_KEY_F)
            s.fit();
        else if (key == GLFW_KEY_R)
        {
            s.controller->reset();
        }
    }
};

bool try_switch_sync(application::WallpaperController& controller, GpuHolder& gpu,
                     const application::CharacterEntry& entry,
                     const application::ControllerConfig& cfg, const fs::path& background,
                     GLFWwindow* window, player::BackendKind backend
#if defined(NEKOMOTE_HAS_VULKAN)
                     ,
                     const nekomote::render_vk::DeviceContext* vk_ctx = nullptr
#endif
)
{
    auto candidate = controller.load_candidate(entry, cfg);
    if (!candidate.ok)
    {
        std::fprintf(stderr, "Cannot switch to %s: %s; current session retained\n",
                     entry.name.c_str(), candidate.error.c_str());
        return false;
    }
    int width = 0, height = 0;
    glfwGetFramebufferSize(window, &width, &height);
    std::optional<GpuCandidate> ready;
    try
    {
        ready.emplace(prepare_candidate_gpu(candidate, gpu, background, width, height, backend,
                                            cfg.audio
#if defined(NEKOMOTE_HAS_VULKAN)
                                            ,
                                            vk_ctx
#endif
                                            ));
    }
    catch (const std::exception& error)
    {
        std::fprintf(stderr, "Cannot switch to %s: %s; current session retained\n",
                     entry.name.c_str(), error.what());
        return false;
    }
    int background_index = -1;
    if (!background.empty())
        for (std::size_t i = 0; i < controller.catalog().backgrounds.size(); ++i)
            if (controller.catalog().backgrounds[i].path() == background)
                background_index = static_cast<int>(i);
    controller.install_candidate_sync(std::move(candidate));
    commit_candidate_gpu(gpu, std::move(*ready));
    controller.set_output_size(std::max(width, 1), std::max(height, 1));
    if (!background.empty())
        controller.confirm_background(background_index, gpu.current_background_id,
                                      gpu.current_background_id, gpu.background_width,
                                      gpu.background_height, 0);
    return true;
}

int run(int argc, char** argv)
{
    auto parsed = options(argc, argv);
    if (!parsed)
        return 0;
    auto o = std::move(*parsed);
    const bool assets_mode = !o.assets.empty();
    application::WallpaperController controller;
    application::ControllerConfig controller_cfg;
    controller_cfg.seed = o.seed;
    controller_cfg.mute = o.mute;
    controller_cfg.origin = o.origin;
    controller_cfg.scale = o.scale;
    controller_cfg.pointer = application::PointerScope::LocalSurface;
    controller_cfg.audio =
        o.mute ? application::AudioMode::Disabled : application::AudioMode::Device;
    controller.set_no_save(o.no_save);
    if (assets_mode)
    {
        if (!controller.scan_assets(o.assets))
            throw std::runtime_error("no characters found in " + o.assets.string());
        const auto& catalog = controller.catalog();
        std::printf("Found %zu characters, %zu backgrounds\n", catalog.characters.size(),
                    catalog.backgrounds.size());
        for (const auto& c : catalog.characters)
            std::printf("  character: %s\n", c.name.c_str());
        for (const auto& b : catalog.backgrounds)
            std::printf("  background: %s\n", b.name.c_str());
        controller.set_config_path(o.assets / "config.json");
        controller.load_config(o.assets / "config.json");
        auto& config = controller.config();
        if (!o.mute)
        {
            o.mute = config.mute;
            controller_cfg.mute = o.mute;
            controller_cfg.audio =
                o.mute ? application::AudioMode::Disabled : application::AudioMode::Device;
        }
        if (!o.character.empty())
        {
            if (config.character != o.character)
                config.transform_saved = false;
            config.character = o.character;
        }
        if (!o.background_name.empty())
            config.background = o.background_name;
        if (config.character.empty())
            config.character = catalog.characters.front().name;
        config.fit = true;
        if (o.fit)
            config.transform_saved = false;
        if (o.origin_x_set)
            controller_cfg.origin.x = o.origin.x;
        if (o.origin_y_set)
            controller_cfg.origin.y = o.origin.y;
        if (o.scale_set)
            controller_cfg.scale = o.scale;
        if (!o.no_save)
        {
            if (!config.transform_saved || o.scale_set || o.fit)
                config.scale = controller_cfg.scale;
            if (!config.transform_saved || o.origin_x_set || o.fit)
                config.origin_x = controller_cfg.origin.x;
            if (!config.transform_saved || o.origin_y_set || o.fit)
                config.origin_y = controller_cfg.origin.y;
            config.width = o.width;
            config.height = o.height;
            if (o.fps > 0)
                config.fps = o.fps;
            config.mute = o.mute;
            config.fit = o.fit || config.fit;
        }
    }
    Glfw glfw;
    std::unique_ptr<GLFWwindow, DestroyWindow> window;
#if defined(NEKOMOTE_HAS_VULKAN)
    std::unique_ptr<player::VkHost> vk_host;
    nekomote::render_vk::DeviceContext vk_ctx;
    const nekomote::render_vk::DeviceContext* vk_ctx_ptr = nullptr;
#endif
    if (o.vulkan())
    {
#if defined(NEKOMOTE_HAS_VULKAN)
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        glfwWindowHint(GLFW_VISIBLE, o.capture.empty() ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
        window.reset(glfwCreateWindow(o.width, o.height, "NekoMote", nullptr, nullptr));
        if (!window)
            throw std::runtime_error("Vulkan window creation failed");
        vk_host = std::make_unique<player::VkHost>(
            o.measure != 0 ? player::PresentationPolicy::ApplicationPacedMeasurement
                           : player::PresentationPolicy::Synchronized);
        std::uint32_t glfw_ext_count = 0;
        const char** glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
        if (!glfw_exts || glfw_ext_count == 0)
            throw std::runtime_error("GLFW returned no Vulkan instance extensions");
        std::vector<const char*> required_exts(glfw_exts, glfw_exts + glfw_ext_count);
        const auto* validation = std::getenv("NEKOMOTE_VK_VALIDATION");
        vk_host->create_instance(required_exts, validation && std::strcmp(validation, "1") == 0
                                                    ? player::ValidationPolicy::Required
                                                    : player::kDefaultValidationPolicy);
        VkSurfaceKHR surface;
        if (glfwCreateWindowSurface(vk_host->instance, window.get(), nullptr, &surface) !=
            VK_SUCCESS)
            throw std::runtime_error("failed to create Vulkan surface");
        vk_host->select_physical_device(surface);
        vk_host->create_device();
        vk_ctx = vk_host->context();
        vk_ctx_ptr = &vk_ctx;
#else
        throw std::runtime_error(
            "Vulkan backend not built; reconfigure with NEKOMOTE_BUILD_VULKAN=ON");
#endif
    }
    else
    {
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        glfwWindowHint(GLFW_VISIBLE, o.capture.empty() ? GLFW_TRUE : GLFW_FALSE);
        glfwWindowHint(GLFW_ALPHA_BITS, 8);
        if (!o.check_window.empty() || o.measure != 0)
        {
            glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
            glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
        }
        window.reset(glfwCreateWindow(o.width, o.height, "NekoMote", nullptr, nullptr));
        if (!window)
            throw std::runtime_error("OpenGL window creation failed");
        glfwMakeContextCurrent(window.get());
        // Measurement uses the explicit application FPS budget. On some EGL
        // hosts an unfocused window's vblank wait stalls independently of work.
        glfwSwapInterval(o.capture.empty() && o.check_window.empty() && o.measure == 0 ? 1 : 0);
    }

    GpuHolder gpu;
    application::CandidateCpu candidate;
    if (assets_mode)
    {
        const auto& catalog = controller.catalog();
        const auto* initial = catalog.find_character(controller.config().character);
        if (!initial)
            initial = &catalog.characters.front();
        int initial_index = static_cast<int>(initial - catalog.characters.data());
        candidate = controller.load_initial(initial_index, controller_cfg);
    }
    else
    {
        application::CharacterEntry temp_entry{o.model.stem().string(), o.model, o.session,
                                               o.model.parent_path()};
        candidate = controller.load_initial(temp_entry, controller_cfg);
    }
    if (!candidate.ok)
        throw std::runtime_error(candidate.error);
    finish_candidate_gpu(gpu, candidate, o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                         ,
                         vk_ctx_ptr
#endif
    );
    gpu.audio_clips = std::make_shared<const model::AudioBank>(std::move(candidate.audio_clips));
    if (controller_cfg.audio == application::AudioMode::Device && !gpu.audio_clips->empty())
        gpu.device = create_audio_device(gpu.audio_clips);
    controller.install_initial(std::move(candidate));
    std::unique_ptr<player::TouchOverlay> touch_overlay;
    if (o.show_touch_regions)
        touch_overlay = std::make_unique<player::TouchOverlay>(o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                                               ,
                                                               vk_ctx_ptr
#endif
        );
    if (!o.reaction.empty())
        check(controller.current_instance()->react(
            controller.current_program()->find_reaction(o.reaction)));
    if (!o.background.empty())
    {
        auto new_image = asset_io::load_png(o.background);
        if (!new_image.ok)
            throw std::runtime_error(new_image.error);
        gpu.background_renderer = std::make_unique<player::BackendRenderer>(o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                                                            ,
                                                                            vk_ctx_ptr
#endif
        );
        gpu.background_renderer->reserve({8, 12, 2, 2});
        const TextureId id{1};
        gpu.background_renderer->upload_texture(id, new_image.image);
        controller.confirm_background(-1, id, id, new_image.image.width, new_image.image.height, 0);
        gpu.current_background_id = id;
        gpu.current_background_path = o.background;
        gpu.background_width = new_image.image.width;
        gpu.background_height = new_image.image.height;
    }
    else if (assets_mode && !controller.config().background.empty())
    {
        const auto& catalog = controller.catalog();
        const auto* bg = catalog.find_background(controller.config().background);
        if (bg)
        {
            int bg_index = static_cast<int>(bg - catalog.backgrounds.data());
            apply_background_gpu(gpu, controller.background_path(bg_index), 0, bg_index, controller,
                                 o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                 ,
                                 vk_ctx_ptr
#endif
            );
        }
    }
    Surface surface{&controller, window.get()};
    player::BackendUiRenderer ui_renderer;
    ui::Context ui_context;
    ui::IconState ui_icon{};
    ui::SidebarState ui_sidebar{};
    if (o.ui_open)
    {
        ui_sidebar.open = true;
        ui_sidebar.animation = 1.0f;
        ui_sidebar.expanded_drawer = o.ui_drawer;
    }
    std::uint32_t ui_icon_texture = 0;
    bool ui_ready = false;
    if (assets_mode)
    {
        int fw = 0, fh = 0;
        glfwGetFramebufferSize(window.get(), &fw, &fh);
        if (!ui_renderer.init(o.backend, fw, fh
#if defined(NEKOMOTE_HAS_VULKAN)
                              ,
                              vk_ctx_ptr
#endif
                              ))
            throw std::runtime_error("wallpaper UI initialization failed");
        if (!ui_renderer.init_font("", 36))
            throw std::runtime_error("wallpaper UI font initialization failed");
        ui_context.init(ui_renderer.painter());
        const auto icon_path = o.assets / "icons" / "toggle.png";
        if (std::filesystem::exists(icon_path))
        {
            auto image = asset_io::load_png(icon_path);
            if (!image.ok)
                throw std::runtime_error(image.error);
            ui_icon_texture = ui_renderer.upload_icon(image.image);
            if (!ui_icon_texture)
                throw std::runtime_error("wallpaper UI icon upload failed: " + icon_path.string());
        }
        if (controller.config().icon_position_saved)
        {
            ui_icon.x = controller.config().icon_x;
            ui_icon.y = controller.config().icon_y;
            ui_icon.initialized = true;
        }
        else
        {
            ui_icon.x = static_cast<float>(fw) - 60;
            ui_icon.y = 16;
        }
        if (controller.config().sidebar_position_saved && controller.config().sidebar_free)
        {
            ui_sidebar.x = controller.config().sidebar_x;
            ui_sidebar.y = controller.config().sidebar_y;
            ui_sidebar.is_free = true;
        }
        ui_ready = true;
        surface.ui_context = &ui_context;
        surface.ui_icon = &ui_icon;
        surface.ui_sidebar = &ui_sidebar;
    }
    int initial_width = 0, initial_height = 0;
    glfwGetFramebufferSize(window.get(), &initial_width, &initial_height);
    controller.set_output_size(initial_width, initial_height);
    if (o.fit && !assets_mode)
    {
        controller.prepare_frame();
        surface.fit();
        check(controller.last_error());
    }
    std::printf("NekoMote: local pointer=%s, touch=%s, audio=%s\n",
                controller.current_program()->gaze_available() ? "available" : "unavailable",
                controller.current_program()->touch_available() ? "available" : "unavailable",
                o.mute ? "disabled" : "device");
    if (assets_mode)
        std::printf("Character: %s | Background: %s | Tab/1-9: switch char | B/N: switch bg | C: "
                    "clear bg\n",
                    controller.current_character_name().c_str(),
                    controller.has_background() ? controller.config().background.c_str() : "none");

    std::vector<double> times(static_cast<std::size_t>(o.measure));
    std::vector<double> cadence(static_cast<std::size_t>(o.measure));
    Clock::time_point measure_begin{};
    float maximum_audio_rms = 0;
    std::size_t measured = 0;
    int warmup = 120, last_width = 0, last_height = 0;
#if defined(NEKOMOTE_HAS_VULKAN)
    player::VkCompletionGuard completion_guard{vk_host.get()};
#endif
    const auto capture_frame = [&](int width, int height)
    {
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_host)
            return vk_host->capture(width, height);
#endif
        return gpu.renderer->capture(width, height);
    };
    auto previous = Clock::now();
    bool captured = false;
    int check_frame = 0;
    std::vector<std::uint8_t> paused_pixels;
    std::optional<Clock::time_point> minimized_check;
    bool window_check_passed = false;
    std::optional<Clock::time_point> resize_check;
    std::optional<Clock::time_point> async_check;
    int checked_character = -1, checked_background = -1;
    if (!o.check_window.empty())
        fs::create_directories(o.check_window);
    if (!o.capture.empty())
        for (int i = 0; i < o.steps; ++i)
            check(controller.current_instance()->advance(1.0 / 60));
    int current_fps_limit = o.fps > 0 ? o.fps : controller.config().fps;
    if (o.capture.empty())
    {
        glfwSetWindowUserPointer(window.get(), &surface);
        glfwSetCursorPosCallback(window.get(), Surface::move);
        glfwSetCursorEnterCallback(window.get(), Surface::enter);
        glfwSetMouseButtonCallback(window.get(), Surface::button);
        glfwSetScrollCallback(window.get(), Surface::scroll);
        glfwSetWindowIconifyCallback(window.get(), Surface::iconify);
    }
    if (assets_mode && o.capture.empty())
    {
        glfwSetKeyCallback(
            window.get(),
            [](GLFWwindow* w, int key, int, int action, int)
            {
                if (action != GLFW_PRESS)
                    return;
                auto& s = Surface::get(w);
                if (key == GLFW_KEY_ESCAPE)
                    glfwSetWindowShouldClose(w, GLFW_TRUE);
                else if (key == GLFW_KEY_SPACE)
                    s.set_paused(!s.paused);
                else if (key == GLFW_KEY_F)
                    s.fit();
                else if (key == GLFW_KEY_R)
                    s.controller->reset();
                else if (key == GLFW_KEY_TAB)
                {
                    const int count = static_cast<int>(s.controller->catalog().characters.size());
                    if (count > 0)
                    {
                        int next = (s.controller->current_character_index() + 1) % count;
                        s.controller->request_switch_character(next);
                    }
                }
                else if (key == GLFW_KEY_B || key == GLFW_KEY_N)
                {
                    const int count = static_cast<int>(s.controller->catalog().backgrounds.size());
                    if (count > 0)
                    {
                        int cur = s.controller->current_background_index();
                        int next = (cur < 0 ? 0 : (cur + 1) % count);
                        s.controller->request_switch_background(next);
                    }
                }
                else if (key == GLFW_KEY_C)
                    s.controller->request_clear_background();
                else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
                {
                    const int index = key - GLFW_KEY_1;
                    const int count = static_cast<int>(s.controller->catalog().characters.size());
                    if (index < count)
                        s.controller->request_switch_character(index);
                }
            });
    }
    else if (o.capture.empty())
    {
        glfwSetKeyCallback(window.get(), Surface::key);
    }
    while (!glfwWindowShouldClose(window.get()))
    {
#if defined(NEKOMOTE_HAS_VULKAN)
        if (vk_host)
            vk_host->wait_ready();
#endif

        glfwPollEvents();
        if (minimized_check && Clock::now() - *minimized_check >= std::chrono::milliseconds(300))
        {
            if (!surface.minimized)
                throw std::runtime_error("owned-window iconify callback was not delivered");
            glfwRestoreWindow(window.get());
            minimized_check.reset();
        }
        check(controller.last_error());
        if (controller.switch_state() == application::SwitchState::Pending)
        {
            auto candidate_opt = controller.poll_candidate();
            if (candidate_opt)
            {
                if (candidate_opt->ok)
                {
                    fs::path bg_path;
                    const int bg_index = controller.current_background_index();
                    if (bg_index >= 0 &&
                        bg_index < static_cast<int>(controller.catalog().backgrounds.size()))
                        bg_path = controller.background_path(bg_index);
                    int width = 0, height = 0;
                    glfwGetFramebufferSize(window.get(), &width, &height);
                    std::optional<GpuCandidate> ready;
                    try
                    {
                        ready.emplace(prepare_candidate_gpu(*candidate_opt, gpu, bg_path, width,
                                                            height, o.backend, controller_cfg.audio
#if defined(NEKOMOTE_HAS_VULKAN)
                                                            ,
                                                            vk_ctx_ptr
#endif
                                                            ));
                    }
                    catch (const std::exception& error)
                    {
                        controller.reject_candidate(error.what());
                    }
                    if (ready)
                    {
                        controller.confirm_candidate(std::move(*candidate_opt));
                        commit_candidate_gpu(gpu, std::move(*ready));
                        controller.set_output_size(std::max(width, 1), std::max(height, 1));
                        if (!bg_path.empty())
                            controller.confirm_background(
                                bg_index, gpu.current_background_id, gpu.current_background_id,
                                gpu.background_width, gpu.background_height, 0);
                        if (controller.config().fit)
                        {
                            controller.prepare_frame();
                            controller.fit_to_output(width, height);
                        }
                        controller.set_paused(surface.paused);
                        controller.set_minimized(surface.minimized);
                        controller.set_output_unavailable(surface.output_unavailable);
                        last_width = 0;
                        previous = Clock::now();
                        std::printf("Character: %s | Background: %s\n",
                                    controller.current_character_name().c_str(),
                                    controller.has_background()
                                        ? controller.config().background.c_str()
                                        : "none");
                    }
                }
                else
                {
                    controller.reject_candidate(candidate_opt->error);
                }
            }
        }
        controller.update_fade();
        if (controller.switch_state() == application::SwitchState::Idle && gpu.previous_renderer)
            gpu.previous_renderer.reset();
        auto bg_req = controller.poll_background();
        if (bg_req)
        {
            if (bg_req->action == application::BackgroundAction::Switch)
            {
                std::printf(
                    "Switching to background: %s\n",
                    controller.catalog().backgrounds[bg_req->background_index].name.c_str());
                apply_background_gpu(gpu, bg_req->path, bg_req->fade_duration,
                                     bg_req->background_index, controller, o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                     ,
                                     vk_ctx_ptr
#endif
                );
                std::printf("Character: %s | Background: %s\n",
                            controller.current_character_name().c_str(),
                            controller.config().background.c_str());
            }
            else if (bg_req->action == application::BackgroundAction::Clear)
            {
                controller.confirm_background_clear();
                gpu.background_renderer.reset();
                gpu.current_background_id = {};
                gpu.background_width = gpu.background_height = 0;
                gpu.current_background_path.clear();
                std::printf("Background cleared\n");
            }
        }
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        int w = 0, h = 0;
        glfwGetFramebufferSize(window.get(), &w, &h);
        const bool unavailable = w <= 0 || h <= 0;
        if (unavailable != surface.output_unavailable)
        {
            surface.set_output_unavailable(unavailable);
            check(controller.last_error());
        }
        if (gpu.device)
        {
            while (const auto audio_command = controller.pop_audio_command())
                if (!gpu.device->submit(*audio_command))
                    throw std::runtime_error("audio command queue full or device failed");
            const auto snapshot = gpu.device->snapshot();
            if (snapshot.state == audio::DeviceState::Failed)
                throw std::runtime_error("audio device failed " + std::to_string(snapshot.error));
            maximum_audio_rms = std::max(maximum_audio_rms, snapshot.rms);
            controller.set_audio_level(snapshot.rms);
        }
        const auto begin = Clock::now();
        if (o.capture.empty())
        {
            controller.advance(elapsed);
        }
        if (w <= 0 || h <= 0 || surface.minimized)
        {
            glfwWaitEventsTimeout(.1);
            continue;
        }
        check(controller.last_error());
        controller.clear_error();
        controller.prepare_frame();
        check(controller.last_error(), controller.current_program(),
              controller.current_character_name());
        gpu.renderer->prepare(controller.current_instance()->frame());
        const bool in_crossfade =
            controller.switch_state() == application::SwitchState::CrossFade &&
            controller.previous_instance();
        if (in_crossfade)
        {
            check(controller.previous_instance()->prepare_frame(), controller.previous_program());
            gpu.previous_renderer->prepare(controller.previous_instance()->frame());
        }
        if (w != last_width || h != last_height)
        {
            gpu.renderer->resize_output(w, h);
            if (in_crossfade && gpu.previous_renderer)
                gpu.previous_renderer->resize_output(w, h);
            controller.set_output_size(w, h);
            std::printf("Output %dx%d, model scale %.6f origin (%.3f, %.3f)\n", w, h,
                        static_cast<double>(controller.scale()),
                        static_cast<double>(controller.origin().x),
                        static_cast<double>(controller.origin().y));
            if (ui_ready)
                ui_renderer.resize(w, h);
            last_width = w;
            last_height = h;
        }
        const std::array<float, 4> clear = o.transparent ? std::array<float, 4>{0, 0, 0, 0}
                                                         : std::array<float, 4>{.06f, .07f, .1f, 1};
#if defined(NEKOMOTE_HAS_VULKAN)
        if (o.vulkan() && vk_host)
        {
            const auto target = vk_host->begin_frame(w, h, o.capture.empty());
            gpu.renderer->set_target(target, *vk_host);
            if (gpu.background_renderer)
                gpu.background_renderer->set_target(target, *vk_host);
            if (gpu.previous_renderer)
                gpu.previous_renderer->set_target(target, *vk_host);
            if (ui_ready)
                ui_renderer.set_target(target);
            if (touch_overlay)
                touch_overlay->set_target(target, *vk_host);
        }
#endif
        RenderFrame bg_frame{};
        if (controller.has_background())
            bg_frame = controller.current_instance()->background_frame();
        if (!bg_frame.commands.empty())
        {
            gpu.background_renderer->prepare(bg_frame);
            gpu.background_renderer->render({0, 0, 1, clear}, player::BackendContents::Clear, w, h);
        }
        const auto target_contents = !bg_frame.commands.empty() ? player::BackendContents::Preserve
                                                                : player::BackendContents::Clear;
        const float fade_alpha = controller.fade_alpha();
        if (in_crossfade)
        {
            gpu.previous_renderer->render({static_cast<float>(w) * .5f, static_cast<float>(h) * .5f,
                                           1, clear, 1.0f - fade_alpha,
                                           controller.previous_program()->mirror()},
                                          target_contents, w, h);
            gpu.renderer->render({static_cast<float>(w) * .5f, static_cast<float>(h) * .5f, 1,
                                  clear, fade_alpha, controller.mirror()},
                                 player::BackendContents::Preserve, w, h);
        }
        else
        {
            gpu.renderer->render(
                {static_cast<float>(w) * .5f, static_cast<float>(h) * .5f, 1, clear,
                 controller.switch_state() == application::SwitchState::Idle ? 1.0f : fade_alpha,
                 controller.mirror()},
                target_contents, w, h);
        }
        if (touch_overlay)
            touch_overlay->render(*controller.current_instance(), w, h);
        if (ui_ready && (o.capture.empty() || !o.capture_ui.empty()) && o.check_window.empty())
        {
            ui_context.begin_frame();

            if (ui_context.toggle_icon(ui_icon, ui_icon_texture))
                ui_sidebar.open = !ui_sidebar.open;

            ui_context.sidebar(ui_sidebar, static_cast<float>(elapsed));

            const bool mouse_over_icon =
                ui_context.mouse_in_rect(ui_icon.x, ui_icon.y, ui_icon.size, ui_icon.size);
            const bool mouse_over_sidebar =
                ui_sidebar.animation > 0.05f &&
                ui_context.mouse_in_rect(ui_sidebar.x, ui_sidebar.y, ui_sidebar.width,
                                         ui_sidebar.height);
            ui_context.set_captures_mouse(mouse_over_icon || mouse_over_sidebar ||
                                          ui_icon.dragging || ui_sidebar.dragging);

            if (mouse_over_sidebar && ui_context.scroll_delta() != 0.0f)
            {
                ui_sidebar.scroll_y -= ui_context.scroll_delta() * 32.0f;
            }

            if (ui_sidebar.animation > 0.001f)
            {
                ui::MenuState menu;
                menu.sidebar = ui_sidebar;
                menu.fps_limit = current_fps_limit;
                menu.locked = surface.locked;
                menu.muted = o.mute;
                if (auto* p = ui_renderer.painter())
                    ui::build_sidebar(ui_context, *p, controller.catalog(), controller, menu, h);
                ui_sidebar = menu.sidebar;
                current_fps_limit = menu.fps_limit;
                surface.locked = menu.locked;
                const bool muted_changed = o.mute != menu.muted;
                o.mute = menu.muted;
                if (muted_changed)
                {
                    controller_cfg.audio =
                        o.mute ? application::AudioMode::Disabled : application::AudioMode::Device;
                    if (o.mute)
                        gpu.device.reset();
                    else if (gpu.audio_clips && !gpu.audio_clips->empty())
                    {
                        try
                        {
                            gpu.device = create_audio_device(gpu.audio_clips);
                        }
                        catch (const std::exception& error)
                        {
                            std::fprintf(stderr, "Cannot restore audio: %s\n", error.what());
                        }
                    }
                }
            }
            if ((ui_icon.moved || ui_sidebar.moved || ui_icon.position_reset ||
                 ui_sidebar.position_reset) &&
                !ui_icon.dragging && !ui_sidebar.dragging)
            {
                controller.save_ui_layout({ui_icon.x, ui_icon.y}, {ui_sidebar.x, ui_sidebar.y},
                                          ui_sidebar.is_free);
                ui_icon.moved = false;
                ui_sidebar.moved = false;
                ui_icon.position_reset = ui_sidebar.position_reset = false;
            }
            ui_context.end_frame();
            ui_renderer.end_frame();
        }
        if (!o.capture.empty())
        {
            const auto error = asset_io::write_png(o.capture, capture_frame(w, h));
            if (!error.empty())
                throw std::runtime_error(error);
            captured = true;
            break;
        }
        if (!o.check_window.empty())
        {
            ++check_frame;
            const auto save = [&](const char* name)
            {
                const auto image = capture_frame(w, h);
                const auto error = asset_io::write_png(o.check_window / name, image);
                if (!error.empty())
                    throw std::runtime_error(error);
                return image;
            };
            if (check_frame == 7)
                Surface::key(window.get(), GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
            else if (check_frame == 8)
                paused_pixels = save("paused-a.png").pixels;
            else if (check_frame == 9)
            {
                const auto* current_session = controller.current_instance();
                const auto* current_program = controller.current_program();
                const auto origin = controller.origin();
                const auto scale = controller.scale();
                const auto& catalog = controller.catalog();
                auto missing = catalog.characters[controller.current_character_index()];
                missing.model_path = missing.model_path / "missing-model.nkm.json";
                if (try_switch_sync(controller, gpu, missing, controller_cfg, {}, window.get(),
                                    o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                    ,
                                    vk_ctx_ptr
#endif
                                    ))
                    throw std::runtime_error("missing candidate unexpectedly succeeded");
                if (try_switch_sync(controller, gpu,
                                    catalog.characters[controller.current_character_index()],
                                    controller_cfg, missing.model_path / "missing-background.png",
                                    window.get(), o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                    ,
                                    vk_ctx_ptr
#endif
                                    ))
                    throw std::runtime_error("missing candidate background unexpectedly succeeded");
                if (controller.current_instance() != current_session ||
                    controller.current_program() != current_program ||
                    controller.previous_instance() || !surface.paused ||
                    controller.origin() != origin || controller.scale() != scale)
                    throw std::runtime_error(
                        "candidate failure changed current ownership or surface");
            }
            else if (check_frame == 10)
            {
                if (save("switch-failed-retained.png").pixels != paused_pixels)
                    throw std::runtime_error("candidate failure changed paused rendering");
            }
            else if (check_frame == 11)
            {
                (void)capture_frame(w, h);
                const auto* previous_session = controller.current_instance();
                const auto& catalog = controller.catalog();
                const auto background = gpu.current_background_path;
                if (!try_switch_sync(controller, gpu,
                                     catalog.characters[controller.current_character_index()],
                                     controller_cfg, background, window.get(), o.backend
#if defined(NEKOMOTE_HAS_VULKAN)
                                     ,
                                     vk_ctx_ptr
#endif
                                     ) ||
                    controller.current_instance() == previous_session ||
                    controller.previous_instance() != previous_session || !surface.paused)
                    throw std::runtime_error("successful switch broke ownership or pause");
                controller.set_paused(surface.paused);
                controller.set_minimized(surface.minimized);
                controller.set_output_unavailable(surface.output_unavailable);
                controller.clear_previous();
                gpu.previous_renderer.reset();
            }
            else if (check_frame == 12)
                paused_pixels = save("switch-success-paused-a.png").pixels;
            else if (check_frame == 14)
            {
                if (save("switch-success-paused-b.png").pixels != paused_pixels)
                    throw std::runtime_error("paused image changed");
                glfwSetWindowSize(window.get(), 640, 800);
                resize_check = Clock::now();
            }
            else if (check_frame == 15)
            {
                int logical_width = 0, logical_height = 0;
                glfwGetWindowSize(window.get(), &logical_width, &logical_height);
                if (logical_width == 640 && logical_height == 800)
                {
                    resize_check.reset();
                    Surface::key(window.get(), GLFW_KEY_F, 0, GLFW_PRESS, 0);
                }
                else if (resize_check && Clock::now() - *resize_check < std::chrono::seconds(2))
                    --check_frame;
                else
                    throw std::runtime_error(
                        "owned window resize failed: " + std::to_string(logical_width) + "x" +
                        std::to_string(logical_height));
            }
            else if (check_frame == 16)
            {
                save("resized-fit.png");
                glfwIconifyWindow(window.get());
                minimized_check = Clock::now();
            }
            else if (check_frame == 17)
            {
                if (surface.minimized)
                    throw std::runtime_error("owned window restore failed");
                Surface::key(window.get(), GLFW_KEY_R, 0, GLFW_PRESS, 0);
                Surface::key(window.get(), GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
            }
            else if (check_frame == 18 && controller.catalog().characters.size() > 1)
            {
                checked_character = (controller.current_character_index() + 1) %
                                    static_cast<int>(controller.catalog().characters.size());
                controller.request_switch_character(checked_character);
                async_check = Clock::now();
            }
            else if (check_frame == 20 && checked_character >= 0)
            {
                if (controller.current_character_index() == checked_character)
                    save("async-switch.png");
                else if (controller.switch_state() == application::SwitchState::Pending &&
                         async_check && Clock::now() - *async_check < std::chrono::seconds(6))
                    --check_frame;
                else
                    throw std::runtime_error("owned-window asynchronous candidate failed");
            }
            else if (check_frame == 22 && checked_character >= 0)
            {
                if (controller.switch_state() == application::SwitchState::Idle)
                {
                    if (controller.previous_instance() || gpu.previous_renderer)
                        throw std::runtime_error("completed transition retained old owners");
                    save("async-switch-complete.png");
                }
                else if (async_check && Clock::now() - *async_check < std::chrono::seconds(8))
                    --check_frame;
                else
                    throw std::runtime_error("owned-window transition did not complete");
            }
            else if (check_frame == 23 && controller.catalog().backgrounds.size() > 1)
            {
                checked_background = (controller.current_background_index() + 1) %
                                     static_cast<int>(controller.catalog().backgrounds.size());
                controller.request_switch_background(checked_background);
            }
            else if (check_frame == 24 && checked_background >= 0)
            {
                if (controller.current_background_index() != checked_background)
                    throw std::runtime_error("owned-window background candidate failed");
                save("background-switch.png");
            }
            else if (check_frame == 26)
            {
                save("restored.png");
                window_check_passed = true;
                Surface::key(window.get(), GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
            }
            check(controller.last_error());
        }
        const auto cpu_ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        if (o.vulkan())
        {
#if defined(NEKOMOTE_HAS_VULKAN)
            if (vk_host)
                vk_host->present();
#endif
        }
        else
            glfwSwapBuffers(window.get());
        if (current_fps_limit > 0)
        {
            const auto deadline = now + std::chrono::duration_cast<Clock::duration>(
                                            std::chrono::duration<double>(1.0 / current_fps_limit));
            while (!glfwWindowShouldClose(window.get()))
            {
                const auto remaining =
                    std::chrono::duration<double>(deadline - Clock::now()).count();
                if (remaining <= 0)
                    break;
                glfwWaitEventsTimeout(remaining);
            }
        }
        if (o.measure != 0 && !surface.minimized)
        {
            if (warmup > 0)
                --warmup;
            else
            {
                if (measured == 0)
                    measure_begin = now;
                times[measured] = cpu_ms;
                cadence[measured] =
                    std::chrono::duration<double, std::milli>(Clock::now() - now).count();
                ++measured;
                if (measured == times.size())
                    break;
            }
        }
    }
    if (o.measure != 0 && measured != 0)
    {
        const auto measure_seconds =
            std::chrono::duration<double>(Clock::now() - measure_begin).count();
        times.resize(measured);
        cadence.resize(measured);
        std::sort(cadence.begin(), cadence.end());
        std::sort(times.begin(), times.end());
        const auto percentile = [&](double p)
        { return times[static_cast<std::size_t>(p * static_cast<double>(measured - 1))]; };
        std::printf("CPU advance+prepare+%s %s after 120 warmup frames: n=%zu "
                    "P50=%.6f P95=%.6f P99=%.6f ms\n",
                    o.vulkan() ? "Vulkan" : "OpenGL",
                    o.vulkan() ? "command recording" : "submission", measured, percentile(.5),
                    percentile(.95), percentile(.99));
        const auto pacing = [&](double p)
        { return cadence[static_cast<std::size_t>(p * static_cast<double>(measured - 1))]; };
        std::printf("Frame loop cadence (swap+wait), target %d: %.3f fps P50=%.6f P95=%.6f "
                    "P99=%.6f ms; audio RMS max %.6f\n",
                    o.fps, static_cast<double>(measured) / measure_seconds, pacing(.5), pacing(.95),
                    pacing(.99), static_cast<double>(maximum_audio_rms));
    }
    if (!o.check_window.empty())
    {
        if (!window_check_passed)
            throw std::runtime_error("owned-window check interrupted");
        std::puts("Owned-window callbacks: pause pixel equality, resize/fit, minimize/restore, "
                  "candidate failure retention, reset/resume and close PASS; no OS keyboard/mouse "
                  "injection.");
        std::printf("Owned-window async character transition: %s; background switch: %s\n",
                    checked_character >= 0 ? "PASS" : "Skipped (requires two characters)",
                    checked_background >= 0 ? "PASS" : "Skipped (requires two backgrounds)");
    }
    if (!o.capture.empty() && !captured)
        throw std::runtime_error("capture interrupted");
    if (assets_mode && !o.no_save)
    {
        controller.config().scale = controller.scale();
        controller.config().origin_x = controller.origin().x;
        controller.config().origin_y = controller.origin().y;
        controller.save_config(controller.config_path());
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    if (vk_host)
    {
        vk_host->wait_ready();
        std::printf("Vulkan 1.0 validation: %s, errors=%u\n",
                    vk_host->validation_enabled ? "enabled" : "disabled or unavailable",
                    vk_host->validation_errors.load());
        if (vk_host->validation_errors)
            throw std::runtime_error("Vulkan validation errors detected");
    }
#endif
    glfwSetCursorPosCallback(window.get(), nullptr);
    if (ui_icon_texture)
        ui_renderer.destroy_icon(ui_icon_texture);
    glfwSetCursorEnterCallback(window.get(), nullptr);
    glfwSetMouseButtonCallback(window.get(), nullptr);
    glfwSetScrollCallback(window.get(), nullptr);
    glfwSetKeyCallback(window.get(), nullptr);
    glfwSetWindowIconifyCallback(window.get(), nullptr);
    glfwSetWindowUserPointer(window.get(), nullptr);
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
        std::fprintf(stderr, "NekoMote: %s\n", error.what());
        return 1;
    }
}
