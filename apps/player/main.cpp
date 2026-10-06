#include "asset_catalog.hpp"
#include <nekomote/application/session.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/audio.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/asset_io/session_asset.hpp>
#include <nekomote/audio/device.hpp>
#include <nekomote/render_gl/renderer.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace nekomote;
using Clock = std::chrono::steady_clock;
namespace fs = std::filesystem;
constexpr double kBackgroundFadeSeconds = 0.8;
constexpr double kCharacterFadeSeconds = 0.4;
enum class CharacterFadeState
{
    None,
    FadingOut,
    FadingIn
};

struct Options
{
    fs::path model, session, background, capture, check_window, assets;
    std::string reaction, character, background_name;
    int width{800}, height{1080}, steps{0}, measure{0}, fps{30};
    bool mute{false}, transparent{false}, fit{false}, no_save{false};
    std::uint32_t seed{1};
    float scale{1};
    Vec2 origin{0, 400};
};
Options options(int argc, char** argv)
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
        else if (key == "--scale")
            o.scale = std::stof(value());
        else if (key == "--origin-x")
            o.origin.x = std::stof(value());
        else if (key == "--origin-y")
            o.origin.y = std::stof(value());
        else if (key == "--width")
            o.width = std::stoi(value());
        else if (key == "--height")
            o.height = std::stoi(value());
        else if (key == "--check-window")
            o.check_window = value();
        else if (key == "--capture")
            o.capture = value();
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
                "  [--width 800 --height 1080] [--fps 30] [--seed 1]\n"
                "Or: nekomote_player --model model.nkm.json --session session.nks.json\n"
                "  [--background image.png] [--mute] [--fit] [--scale 1] [--origin-x 0 --origin-y "
                "400]\n"
                "  [--width 800 --height 1080] [--fps 30] [--seed 1] [--reaction label]\n"
                "  [--capture output.png --steps N --mute --transparent] [--measure-frames N]\n"
                "  Diagnostic: --check-window output-directory --mute (no OS input injection)\n"
                "Assets mode keys: Tab/1-9 switch character, B/N switch background, C clear "
                "background.\n"
                "Mouse: gaze; left click: touch; right drag: move; wheel: zoom.\n"
                "Space: pause/resume; F: fit whole model; R: reset; Escape: close. Pointer is "
                "local to this window.");
            return {};
        }
        else
            throw std::runtime_error("unknown option " + key);
    }
    if (o.assets.empty() && (o.model.empty() || o.session.empty()))
        throw std::runtime_error("--assets or (--model and --session) required; see --help");
    if (o.width < 64 || o.width > 8192 || o.height < 64 || o.height > 8192 || o.steps < 0 ||
        o.steps > 100000 || o.fps < 5 || o.fps > 240 || o.measure < 0 || o.measure > 1000000 ||
        !std::isfinite(o.scale) || o.scale <= 0 || !std::isfinite(o.origin.x) ||
        !std::isfinite(o.origin.y))
        throw std::runtime_error(
            "invalid dimensions, FPS (5..240), step count, measurement count or placement");
    if (!o.capture.empty() && (!o.mute || o.measure != 0))
        throw std::runtime_error(
            "fixed-step screenshot requires --mute and excludes presentation measurement");
    if (!o.check_window.empty() && (!o.mute || o.measure || !o.capture.empty()))
        throw std::runtime_error("--check-window requires --mute and excludes capture/measurement");
    if (o.capture.empty() && o.steps != 0)
        throw std::runtime_error("--steps requires --capture");
    return o;
}
void check(application::SessionStatus status)
{
    if (!status)
        throw std::runtime_error("session failure " +
                                 std::to_string(static_cast<int>(status.error)) + "/" +
                                 std::to_string(static_cast<int>(status.playback.error)));
}
struct Glfw
{
    Glfw()
    {
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");
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

struct CharacterHolder
{
    std::unique_ptr<const application::SessionProgram> program;
    std::unique_ptr<application::SessionInstance> instance;
    std::unique_ptr<audio::Device> device;
    std::unique_ptr<render_gl::Renderer> renderer;
    std::unique_ptr<render_gl::Renderer> background_renderer;
    TextureId current_background_id{0};
    bool has_background{false};
    std::string name;
    fs::path model_dir;
    fs::path current_background_path;
};

CharacterHolder load_character(const player::CharacterEntry& entry, const Options& o)
{
    CharacterHolder holder;
    holder.name = entry.name;
    holder.model_dir = entry.directory;
    auto model = asset_io::load_asset(entry.model_path);
    if (!model.ok)
        throw std::runtime_error(model.errors.front());
    auto definition = asset_io::load_session_asset(entry.session_path);
    if (!definition.ok)
        throw std::runtime_error(definition.errors.front());
    auto compiled = application::compile_session(model.asset, definition.asset);
    if (!compiled.program)
        throw std::runtime_error(compiled.errors.front());
    holder.program = std::move(compiled.program);
    const auto& program = *holder.program;
    if (!o.mute)
    {
        std::vector<model::AudioClip> clips;
        for (const auto& path : program.audio_paths())
        {
            auto clip = asset_io::load_wave(entry.session_path.parent_path() / path);
            if (!clip.ok)
                throw std::runtime_error(clip.error);
            clips.push_back(std::move(clip.clip));
        }
        holder.device = std::make_unique<audio::Device>(std::move(clips));
        const auto deadline = Clock::now() + std::chrono::seconds(6);
        while (holder.device->snapshot().state == audio::DeviceState::Connecting &&
               Clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto snapshot = holder.device->snapshot();
        if (snapshot.state != audio::DeviceState::Ready)
            throw std::runtime_error("audio device unavailable (error " +
                                     std::to_string(snapshot.error) +
                                     "); use --mute for silent playback");
    }
    holder.instance = std::make_unique<application::SessionInstance>(
        program,
        application::SessionConfig{{o.seed},
                                   o.seed,
                                   application::PointerScope::LocalSurface,
                                   o.mute ? application::AudioMode::Disabled
                                          : application::AudioMode::Device});
    check(holder.instance->set_origin(o.origin));
    check(holder.instance->set_scale(o.scale));
    check(holder.instance->start());
    holder.renderer = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
    std::printf("%s\n", holder.renderer->diagnostics().c_str());
    const auto& scene = program.playback().scene();
    const auto capacity = scene.frame_capacity();
    holder.renderer->reserve({capacity.vertices, capacity.indices, capacity.commands,
                              scene.textures().size(), capacity.mask_commands, capacity.masks});
    for (const auto& texture : scene.textures())
    {
        auto image = asset_io::load_png(entry.model_path.parent_path() / texture.path);
        if (!image.ok)
            throw std::runtime_error(image.error);
        holder.renderer->upload_texture(texture.id, image.image);
    }
    holder.background_renderer = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
    holder.background_renderer->reserve({8, 12, 2, 2});
    return holder;
}

void apply_background(CharacterHolder& holder, const fs::path& bg_path, double fade_duration)
{
    auto new_image = asset_io::load_png(bg_path);
    if (!new_image.ok)
        throw std::runtime_error(new_image.error);
    if (!holder.has_background || fade_duration <= 0)
    {
        holder.background_renderer = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
        holder.background_renderer->reserve({8, 12, 2, 2});
        const TextureId id{1};
        holder.background_renderer->upload_texture(id, new_image.image);
        check(holder.instance->set_background(id, new_image.image.width, new_image.image.height));
        holder.current_background_id = id;
        holder.has_background = true;
        holder.current_background_path = bg_path;
        return;
    }
    auto old_image = asset_io::load_png(holder.current_background_path);
    if (!old_image.ok)
        throw std::runtime_error(old_image.error);
    holder.background_renderer = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
    holder.background_renderer->reserve({8, 12, 2, 2});
    const TextureId old_id{1}, new_id{2};
    holder.background_renderer->upload_texture(old_id, old_image.image);
    holder.background_renderer->upload_texture(new_id, new_image.image);
    check(holder.instance->set_background(old_id, old_image.image.width,
                                          old_image.image.height));
    check(holder.instance->fade_background(new_id, new_image.image.width,
                                           new_image.image.height, fade_duration));
    holder.current_background_id = new_id;
    holder.has_background = true;
    holder.current_background_path = bg_path;
}

struct Surface
{
    application::SessionInstance* session;
    GLFWwindow* window;
    Vec2 origin, previous{};
    float scale;
    bool dragging{false}, paused{false}, minimized{false}, output_unavailable{false};
    application::SessionStatus error;
    void accept(application::SessionStatus s) noexcept
    {
        if (!s && s.error != application::SessionError::Busy &&
            s.error != application::SessionError::InputUnavailable && error)
            error = s;
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
        if (s.dragging)
        {
            s.origin.x += p.x - s.previous.x;
            s.origin.y += p.y - s.previous.y;
            s.accept(s.session->set_origin(s.origin));
        }
        s.previous = p;
        s.accept(s.session->pointer_move(p));
    }
    static void enter(GLFWwindow* w, int entered) noexcept
    {
        auto& s = get(w);
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        if (entered)
            s.accept(s.session->pointer_enter(s.point(x, y)));
        else
        {
            s.dragging = false;
            s.accept(s.session->pointer_leave());
        }
    }
    static void button(GLFWwindow* w, int button, int action, int) noexcept
    {
        auto& s = get(w);
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        if (button == GLFW_MOUSE_BUTTON_RIGHT)
        {
            s.dragging = action == GLFW_PRESS;
            s.previous = s.point(x, y);
        }
        else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS && !s.paused &&
                 !s.minimized)
            s.accept(s.session->touch(s.point(x, y)));
    }
    static void scroll(GLFWwindow* w, double, double amount) noexcept
    {
        auto& s = get(w);
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        const auto p = s.point(x, y);
        const auto scale =
            std::clamp(s.scale * static_cast<float>(std::pow(1.1, amount)), .05f, 5.0f);
        const auto ratio = scale / s.scale;
        s.origin = {p.x + (s.origin.x - p.x) * ratio, p.y + (s.origin.y - p.y) * ratio};
        s.scale = scale;
        s.accept(s.session->set_scale(scale));
        s.accept(s.session->set_origin(s.origin));
    }
    void synchronize_pause() noexcept
    {
        accept(session->set_playback_state(paused || minimized || output_unavailable
                                              ? runtime::PlaybackState::Paused
                                              : runtime::PlaybackState::Running));
    }
    void fit() noexcept
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w <= 32 || h <= 32)
            return;
        const auto frame = session->frame();
        float left = INFINITY, top = INFINITY, right = -INFINITY, bottom = -INFINITY;
        for (const auto& command : frame.commands)
            for (std::uint32_t i = 0; i < command.index_count; ++i)
            {
                const auto& v = frame.vertices[frame.indices[command.first_index + i]];
                if (v.color[3] <= 0)
                    continue;
                left = std::min(left, v.x);
                right = std::max(right, v.x);
                top = std::min(top, v.y);
                bottom = std::max(bottom, v.y);
            }
        if (!(right > left) || !(bottom > top))
            return;
        const auto ratio = std::min(static_cast<float>(w - 32) / (right - left),
                                    static_cast<float>(h - 32) / (bottom - top));
        const auto new_scale = std::clamp(scale * ratio, .05f, 5.0f);
        const auto actual_ratio = new_scale / scale;
        origin = {(origin.x - (left + right) * .5f) * actual_ratio,
                  (origin.y - (top + bottom) * .5f) * actual_ratio};
        scale = new_scale;
        accept(session->set_scale(scale));
        accept(session->set_origin(origin));
    }
    static void iconify(GLFWwindow* w, int minimized) noexcept
    {
        auto& s = get(w);
        s.minimized = minimized != 0;
        s.synchronize_pause();
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
            s.paused = !s.paused;
            s.synchronize_pause();
        }
        else if (key == GLFW_KEY_F)
            s.fit();
        else if (key == GLFW_KEY_R)
        {
            s.accept(s.session->reset());
            s.synchronize_pause();
        }
    }
};

int run(int argc, char** argv)
{
    const auto o = options(argc, argv);
    const bool assets_mode = !o.assets.empty();
    player::AssetCatalog catalog;
    player::WallpaperConfig config;
    fs::path config_path;
    if (assets_mode)
    {
        catalog = player::AssetCatalog::scan(o.assets);
        if (catalog.characters.empty())
            throw std::runtime_error("no characters found in " + o.assets.string());
        std::printf("Found %zu characters, %zu backgrounds\n", catalog.characters.size(),
                    catalog.backgrounds.size());
        for (const auto& c : catalog.characters)
            std::printf("  character: %s\n", c.name.c_str());
        for (const auto& b : catalog.backgrounds)
            std::printf("  background: %s\n", b.name.c_str());
        config_path = o.assets / "config.json";
        config = player::WallpaperConfig::load(config_path);
        if (!o.character.empty())
            config.character = o.character;
        if (!o.background_name.empty())
            config.background = o.background_name;
        if (config.character.empty())
            config.character = catalog.characters.front().name;
        if (!o.no_save)
        {
            config.scale = o.scale;
            config.origin_x = o.origin.x;
            config.origin_y = o.origin.y;
            config.width = o.width;
            config.height = o.height;
            config.fps = o.fps;
            config.mute = o.mute;
            config.fit = o.fit;
        }
    }
    Glfw glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, o.capture.empty() ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    if (!o.check_window.empty())
    {
        glfwWindowHint(GLFW_FOCUSED, GLFW_FALSE);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_FALSE);
    }
    std::unique_ptr<GLFWwindow, DestroyWindow> window(
        glfwCreateWindow(o.width, o.height, "NekoMote", nullptr, nullptr));
    if (!window)
        throw std::runtime_error("OpenGL window creation failed");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(o.capture.empty() && o.check_window.empty() ? 1 : 0);

    const player::CharacterEntry* initial = nullptr;
    if (assets_mode)
    {
        initial = catalog.find_character(config.character);
        if (!initial)
            initial = &catalog.characters.front();
    }
    else
    {
        static player::CharacterEntry temp_entry;
        temp_entry = {o.model.stem().string(), o.model, o.session, o.model.parent_path()};
        initial = &temp_entry;
    }
    CharacterHolder holder = load_character(*initial, o);
    if (!o.reaction.empty())
        check(holder.instance->react(holder.program->find_reaction(o.reaction)));
    if (!o.background.empty())
        apply_background(holder, o.background, 0);
    else if (assets_mode && !config.background.empty())
    {
        const auto* bg = catalog.find_background(config.background);
        if (bg)
            apply_background(holder, bg->path, 0);
    }
    Surface surface{holder.instance.get(), window.get(), o.origin, {}, o.scale,
                    false, false, false, false, {}};
    if (o.fit || (assets_mode && config.fit))
    {
        check(holder.instance->prepare_frame());
        surface.fit();
        check(surface.error);
    }
    std::printf("%s\n", holder.renderer->diagnostics().c_str());
    std::printf("NekoMote: local pointer=%s, touch=%s, audio=%s\n",
                holder.program->gaze_available() ? "available" : "unavailable",
                holder.program->touch_available() ? "available" : "unavailable",
                o.mute ? "disabled" : "device");
    if (assets_mode)
        std::printf("Character: %s | Background: %s | Tab/1-9: switch char | B/N: switch bg | C: "
                    "clear bg\n",
                    holder.name.c_str(),
                    holder.has_background ? config.background.c_str() : "none");

    std::vector<double> times(static_cast<std::size_t>(o.measure));
    std::vector<double> cadence(static_cast<std::size_t>(o.measure));
    Clock::time_point measure_begin{};
    float maximum_audio_rms = 0;
    std::size_t measured = 0;
    int warmup = 120, last_width = 0, last_height = 0;
    auto previous = Clock::now();
    bool captured = false;
    int check_frame = 0;
    std::vector<std::uint8_t> paused_pixels;
    std::optional<Clock::time_point> minimized_check;
    bool window_check_passed = false;
    std::optional<Clock::time_point> resize_check;
    if (!o.check_window.empty())
        fs::create_directories(o.check_window);
    if (!o.capture.empty())
        for (int i = 0; i < o.steps; ++i)
            check(holder.instance->advance(1.0 / 60));
    int current_character_index = 0;
    int current_background_index = -1;
    if (assets_mode)
    {
        for (std::size_t i = 0; i < catalog.characters.size(); ++i)
            if (catalog.characters[i].name == holder.name)
                current_character_index = static_cast<int>(i);
        for (std::size_t i = 0; i < catalog.backgrounds.size(); ++i)
            if (catalog.backgrounds[i].name == config.background)
                current_background_index = static_cast<int>(i);
    }
    bool need_reload = false;
    int target_character_index = current_character_index;
    bool need_bg_switch = false;
    int target_background_index = current_background_index;
    bool need_bg_clear = false;
    CharacterFadeState fade_state = CharacterFadeState::None;
    float fade_alpha = 1;
    Clock::time_point fade_start;
    int fade_target_character = -1;
    auto switch_character = [&](int index)
    {
        if (index < 0 || index >= static_cast<int>(catalog.characters.size()))
            return;
        if (index == current_character_index || fade_state != CharacterFadeState::None)
            return;
        fade_target_character = index;
        fade_state = CharacterFadeState::FadingOut;
        fade_alpha = 1;
        fade_start = Clock::now();
    };
    auto switch_background = [&](int index)
    {
        if (index < 0 || index >= static_cast<int>(catalog.backgrounds.size()))
            return;
        target_background_index = index;
        need_bg_switch = true;
    };
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
        struct KeyState
        {
            Surface* surface;
            std::function<void(int)> switch_character;
            std::function<void(int)> switch_background;
            std::function<void()> clear_background;
            std::function<void()> next_character;
            std::function<void()> next_background;
            int character_count;
            int background_count;
        };
        static KeyState key_state;
        key_state.surface = &surface;
        key_state.switch_character = switch_character;
        key_state.switch_background = switch_background;
        key_state.clear_background = [&]() { need_bg_clear = true; };
        key_state.next_character = [&]()
        { switch_character((current_character_index + 1) % static_cast<int>(catalog.characters.size())); };
        key_state.next_background = [&]()
        {
            if (catalog.backgrounds.empty())
                return;
            switch_background((current_background_index + 1) % static_cast<int>(catalog.backgrounds.size()));
        };
        key_state.character_count = static_cast<int>(catalog.characters.size());
        key_state.background_count = static_cast<int>(catalog.backgrounds.size());
        glfwSetKeyCallback(window.get(), [](GLFWwindow* w, int key, int, int action, int)
        {
            if (action != GLFW_PRESS)
                return;
            if (key == GLFW_KEY_ESCAPE)
                glfwSetWindowShouldClose(w, GLFW_TRUE);
            else if (key == GLFW_KEY_SPACE)
            {
                key_state.surface->paused = !key_state.surface->paused;
                key_state.surface->synchronize_pause();
            }
            else if (key == GLFW_KEY_F)
                key_state.surface->fit();
            else if (key == GLFW_KEY_R)
            {
                key_state.surface->accept(key_state.surface->session->reset());
                key_state.surface->synchronize_pause();
            }
            else if (key == GLFW_KEY_TAB)
                key_state.next_character();
            else if (key == GLFW_KEY_B || key == GLFW_KEY_N)
                key_state.next_background();
            else if (key == GLFW_KEY_C)
                key_state.clear_background();
            else if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9)
            {
                const int index = key - GLFW_KEY_1;
                if (index < key_state.character_count)
                    key_state.switch_character(index);
            }
        });
    }
    else if (o.capture.empty())
    {
        glfwSetKeyCallback(window.get(), [](GLFWwindow* w, int key, int, int action, int)
        {
            if (action != GLFW_PRESS)
                return;
            auto& s = Surface::get(w);
            if (key == GLFW_KEY_ESCAPE)
                glfwSetWindowShouldClose(w, GLFW_TRUE);
            else if (key == GLFW_KEY_SPACE)
            {
                s.paused = !s.paused;
                s.synchronize_pause();
            }
            else if (key == GLFW_KEY_F)
                s.fit();
            else if (key == GLFW_KEY_R)
            {
                s.accept(s.session->reset());
                s.synchronize_pause();
            }
        });
    }
    while (!glfwWindowShouldClose(window.get()))
    {
        glfwPollEvents();
        if (minimized_check && Clock::now() - *minimized_check >= std::chrono::milliseconds(300))
        {
            if (!surface.minimized)
                throw std::runtime_error("owned-window iconify callback was not delivered");
            glfwRestoreWindow(window.get());
            minimized_check.reset();
        }
        check(surface.error);
        if (fade_state != CharacterFadeState::None)
        {
            const auto elapsed = std::chrono::duration<double>(Clock::now() - fade_start).count();
            const auto progress = static_cast<float>(elapsed / kCharacterFadeSeconds);
            if (fade_state == CharacterFadeState::FadingOut)
            {
                fade_alpha = std::max(1.0f - progress, 0.0f);
                if (fade_alpha <= 0.0f)
                {
                    target_character_index = fade_target_character;
                    need_reload = true;
                    fade_state = CharacterFadeState::FadingIn;
                    fade_alpha = 0.0f;
                    fade_start = Clock::now();
                }
            }
            else
            {
                fade_alpha = std::min(progress, 1.0f);
                if (fade_alpha >= 1.0f)
                {
                    fade_alpha = 1.0f;
                    fade_state = CharacterFadeState::None;
                }
            }
        }
        if (need_reload && assets_mode)
        {
            glfwSetWindowUserPointer(window.get(), nullptr);
            const auto& entry = catalog.characters[target_character_index];
            std::printf("Switching to character: %s\n", entry.name.c_str());
            holder = load_character(entry, o);
            surface.session = holder.instance.get();
            surface.origin = o.origin;
            surface.scale = o.scale;
            surface.paused = false;
            check(holder.instance->set_origin(surface.origin));
            check(holder.instance->set_scale(surface.scale));
            glfwSetWindowUserPointer(window.get(), &surface);
            current_character_index = target_character_index;
            config.character = entry.name;
            if (current_background_index >= 0 &&
                current_background_index < static_cast<int>(catalog.backgrounds.size()))
            {
                const auto& bg = catalog.backgrounds[current_background_index];
                apply_background(holder, bg.path, 0);
                config.background = bg.name;
            }
            if (!o.no_save)
                config.save(config_path);
            std::printf("Character: %s | Background: %s\n", holder.name.c_str(),
                        holder.has_background ? config.background.c_str() : "none");
            need_reload = false;
            last_width = 0;
            last_height = 0;
            fade_start = Clock::now();
        }
        if (need_bg_switch && assets_mode)
        {
            if (target_background_index >= 0 &&
                target_background_index < static_cast<int>(catalog.backgrounds.size()))
            {
                const auto& bg = catalog.backgrounds[target_background_index];
                std::printf("Switching to background: %s\n", bg.name.c_str());
                apply_background(holder, bg.path, kBackgroundFadeSeconds);
                current_background_index = target_background_index;
                config.background = bg.name;
                if (!o.no_save)
                    config.save(config_path);
                std::printf("Character: %s | Background: %s\n", holder.name.c_str(),
                            config.background.c_str());
            }
            need_bg_switch = false;
        }
        if (need_bg_clear && assets_mode)
        {
            holder.instance->clear_background();
            holder.has_background = false;
            current_background_index = -1;
            config.background.clear();
            if (!o.no_save)
                config.save(config_path);
            std::printf("Background cleared\n");
            need_bg_clear = false;
        }
        const auto now = Clock::now();
        const auto elapsed = std::chrono::duration<double>(now - previous).count();
        previous = now;
        int w = 0, h = 0;
        glfwGetFramebufferSize(window.get(), &w, &h);
        const bool unavailable = w <= 0 || h <= 0;
        if (unavailable != surface.output_unavailable)
        {
            surface.output_unavailable = unavailable;
            surface.synchronize_pause();
            check(surface.error);
        }
        if (holder.device)
        {
            while (const auto audio_command = holder.instance->pop_audio_command())
                if (!holder.device->submit(*audio_command))
                    throw std::runtime_error("audio command queue full or device failed");
            const auto snapshot = holder.device->snapshot();
            if (snapshot.state == audio::DeviceState::Failed)
                throw std::runtime_error("audio device failed " + std::to_string(snapshot.error));
            maximum_audio_rms = std::max(maximum_audio_rms, snapshot.rms);
            check(holder.instance->set_audio_level(snapshot.rms));
        }
        const auto begin = Clock::now();
        if (o.capture.empty())
            check(holder.instance->advance(elapsed));
        if (w <= 0 || h <= 0 || surface.minimized)
        {
            glfwWaitEventsTimeout(.1);
            continue;
        }
        check(holder.instance->prepare_frame());
        holder.renderer->prepare(holder.instance->frame());
        if (w != last_width || h != last_height)
        {
            holder.renderer->resize_output(w, h);
            check(holder.instance->set_output_size(w, h));
            std::printf("Output %dx%d, model scale %.6f origin (%.3f, %.3f)\n", w, h,
                        static_cast<double>(surface.scale), static_cast<double>(surface.origin.x),
                        static_cast<double>(surface.origin.y));
            last_width = w;
            last_height = h;
        }
        const std::array<float, 4> clear = o.transparent ? std::array<float, 4>{0, 0, 0, 0}
                                                         : std::array<float, 4>{.06f, .07f, .1f, 1};
        const render_gl::Target target{0, w, h};
        RenderFrame bg_frame{};
        if (holder.has_background)
            bg_frame = holder.instance->background_frame();
        if (!bg_frame.commands.empty())
        {
            holder.background_renderer->prepare(bg_frame);
            holder.background_renderer->render(target, {0, 0, 1, clear});
        }
        holder.renderer->render(
            target, {static_cast<float>(w) * .5f, static_cast<float>(h) * .5f, 1, clear,
                     fade_state == CharacterFadeState::None ? 1.0f : fade_alpha},
            !bg_frame.commands.empty() ? render_gl::TargetContents::Preserve
                                       : render_gl::TargetContents::Clear);
        if (!o.capture.empty())
        {
            const auto error = asset_io::write_png(o.capture, holder.renderer->capture(target));
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
                const auto image = holder.renderer->capture(target);
                const auto error = asset_io::write_png(o.check_window / name, image);
                if (!error.empty())
                    throw std::runtime_error(error);
                return image;
            };
            if (check_frame == 7)
                Surface::key(window.get(), GLFW_KEY_SPACE, 0, GLFW_PRESS, 0);
            else if (check_frame == 8)
                paused_pixels = save("paused-a.png").pixels;
            else if (check_frame == 14)
            {
                if (save("paused-b.png").pixels != paused_pixels)
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
            else if (check_frame == 26)
            {
                save("restored.png");
                window_check_passed = true;
                Surface::key(window.get(), GLFW_KEY_ESCAPE, 0, GLFW_PRESS, 0);
            }
            check(surface.error);
        }
        const auto cpu_ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        glfwSwapBuffers(window.get());
        const auto deadline = now + std::chrono::duration_cast<Clock::duration>(
                                        std::chrono::duration<double>(1.0 / o.fps));
        while (!glfwWindowShouldClose(window.get()))
        {
            const auto remaining = std::chrono::duration<double>(deadline - Clock::now()).count();
            if (remaining <= 0)
                break;
            glfwWaitEventsTimeout(remaining);
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
        std::printf("CPU advance+prepare+GL submission after 120 warmup frames: n=%zu P50=%.6f "
                    "P95=%.6f P99=%.6f ms\n",
                    measured, percentile(.5), percentile(.95), percentile(.99));
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
                  "reset/resume and close PASS; no OS keyboard/mouse injection.");
    }
    if (!o.capture.empty() && !captured)
        throw std::runtime_error("capture interrupted");
    if (assets_mode && !o.no_save)
    {
        config.scale = surface.scale;
        config.origin_x = surface.origin.x;
        config.origin_y = surface.origin.y;
        config.save(config_path);
    }
    glfwSetCursorPosCallback(window.get(), nullptr);
    glfwSetCursorEnterCallback(window.get(), nullptr);
    glfwSetMouseButtonCallback(window.get(), nullptr);
    glfwSetScrollCallback(window.get(), nullptr);
    glfwSetKeyCallback(window.get(), nullptr);
    glfwSetWindowIconifyCallback(window.get(), nullptr);
    glfwSetWindowUserPointer(window.get(), nullptr);
    return 0;
}
}
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
