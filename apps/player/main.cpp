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
#include <cstdio>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace nekomote;
using Clock = std::chrono::steady_clock;
struct Options
{
    std::filesystem::path model, session, background, capture;
    std::string reaction;
    int width{800}, height{1080}, steps{0}, measure{0};
    bool mute{false}, transparent{false}, fit{false};
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
        else if (key == "--capture")
            o.capture = value();
        else if (key == "--steps")
            o.steps = std::stoi(value());
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
                "nekomote_player --model model.nkm.json --session session.nks.json\n"
                "  [--background image.png] [--mute] [--fit] [--scale 1] [--origin-x 0 --origin-y "
                "400]\n"
                "  [--width 800 --height 1080] [--seed 1] [--reaction label]\n"
                "  [--capture output.png --steps N --mute --transparent] [--measure-frames N]\n"
                "Mouse: gaze; left click: touch; right drag: move; wheel: zoom.\n"
                "Space: pause/resume; F: fit whole model; R: reset; Escape: close. Pointer is "
                "local to this window.");
            return {};
        }
        else
            throw std::runtime_error("unknown option " + key);
    }
    if (o.model.empty() || o.session.empty())
        throw std::runtime_error("--model and --session required; see --help");
    if (o.width < 64 || o.width > 8192 || o.height < 64 || o.height > 8192 || o.steps < 0 ||
        o.steps > 100000 || o.measure < 0 || o.measure > 1000000 || !std::isfinite(o.scale) ||
        o.scale <= 0 || !std::isfinite(o.origin.x) || !std::isfinite(o.origin.y))
        throw std::runtime_error("invalid dimensions, step count, measurement count or placement");
    if (!o.capture.empty() && (!o.mute || o.measure != 0))
        throw std::runtime_error(
            "fixed-step screenshot requires --mute and excludes presentation measurement");
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
struct Surface
{
    application::SessionInstance& session;
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
            s.accept(s.session.set_origin(s.origin));
        }
        s.previous = p;
        s.accept(s.session.pointer_move(p));
    }
    static void enter(GLFWwindow* w, int entered) noexcept
    {
        auto& s = get(w);
        double x = 0, y = 0;
        glfwGetCursorPos(w, &x, &y);
        if (entered)
            s.accept(s.session.pointer_enter(s.point(x, y)));
        else
        {
            s.dragging = false;
            s.accept(s.session.pointer_leave());
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
            s.accept(s.session.touch(s.point(x, y)));
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
        s.accept(s.session.set_scale(scale));
        s.accept(s.session.set_origin(s.origin));
    }
    void synchronize_pause() noexcept
    {
        accept(session.set_playback_state(paused || minimized || output_unavailable
                                              ? runtime::PlaybackState::Paused
                                              : runtime::PlaybackState::Running));
    }
    void fit() noexcept
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window, &w, &h);
        if (w <= 32 || h <= 32)
            return;
        const auto frame = session.frame();
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
        accept(session.set_scale(scale));
        accept(session.set_origin(origin));
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
            s.accept(s.session.reset());
            s.synchronize_pause();
        }
    }
    static void iconify(GLFWwindow* w, int minimized) noexcept
    {
        auto& s = get(w);
        s.minimized = minimized != 0;
        s.synchronize_pause();
    }
};
void background_vertices(std::array<RenderVertex, 4>& vertices, int w, int h, int iw, int ih)
{
    const auto cover = std::max(static_cast<double>(w) / iw, static_cast<double>(h) / ih);
    const auto du = static_cast<float>(w / (iw * cover) * .5),
               dv = static_cast<float>(h / (ih * cover) * .5);
    const auto fw = static_cast<float>(w), fh = static_cast<float>(h);
    vertices = {{{0, 0, .5f - du, .5f - dv},
                 {fw, 0, .5f + du, .5f - dv},
                 {fw, fh, .5f + du, .5f + dv},
                 {0, fh, .5f - du, .5f + dv}}};
}
int run(int argc, char** argv)
{
    const auto o = options(argc, argv);
    if (o.model.empty())
        return 0;
    auto model = asset_io::load_asset(o.model);
    if (!model.ok)
        throw std::runtime_error(model.errors.front());
    auto definition = asset_io::load_session_asset(o.session);
    if (!definition.ok)
        throw std::runtime_error(definition.errors.front());
    auto compiled = application::compile_session(model.asset, definition.asset);
    if (!compiled.program)
        throw std::runtime_error(compiled.errors.front());
    model = {};
    definition = {};
    const auto& program = *compiled.program;
    std::unique_ptr<audio::Device> device;
    if (!o.mute)
    {
        std::vector<model::AudioClip> clips;
        for (const auto& path : program.audio_paths())
        {
            auto clip = asset_io::load_wave(o.session.parent_path() / path);
            if (!clip.ok)
                throw std::runtime_error(clip.error);
            clips.push_back(std::move(clip.clip));
        }
        device = std::make_unique<audio::Device>(std::move(clips));
        const auto deadline = Clock::now() + std::chrono::seconds(6);
        while (device->snapshot().state == audio::DeviceState::Connecting &&
               Clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        const auto snapshot = device->snapshot();
        if (snapshot.state != audio::DeviceState::Ready)
            throw std::runtime_error("audio device unavailable (error " +
                                     std::to_string(snapshot.error) +
                                     "); use --mute for silent playback");
    }
    Glfw glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, o.capture.empty() ? GLFW_TRUE : GLFW_FALSE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    std::unique_ptr<GLFWwindow, DestroyWindow> window(
        glfwCreateWindow(o.width, o.height, "NekoMote", nullptr, nullptr));
    if (!window)
        throw std::runtime_error("OpenGL window creation failed");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(o.capture.empty() ? 1 : 0);
    application::SessionInstance session(
        program, {{o.seed},
                  o.seed,
                  application::PointerScope::LocalSurface,
                  o.mute ? application::AudioMode::Disabled : application::AudioMode::Device});
    check(session.set_origin(o.origin));
    check(session.set_scale(o.scale));
    check(session.start());
    if (!o.reaction.empty())
        check(session.react(program.find_reaction(o.reaction)));
    Surface surface{session, window.get(), o.origin, {}, o.scale, false, false, false, false, {}};
    if (o.fit)
    {
        check(session.prepare_frame());
        surface.fit();
        check(surface.error);
    }
    if (o.capture.empty())
    {
        glfwSetWindowUserPointer(window.get(), &surface);
        glfwSetCursorPosCallback(window.get(), Surface::move);
        glfwSetCursorEnterCallback(window.get(), Surface::enter);
        glfwSetMouseButtonCallback(window.get(), Surface::button);
        glfwSetScrollCallback(window.get(), Surface::scroll);
        glfwSetKeyCallback(window.get(), Surface::key);
        glfwSetWindowIconifyCallback(window.get(), Surface::iconify);
    }
    render_gl::Renderer renderer(glfwGetProcAddress);
    std::printf("%s\n", renderer.diagnostics().c_str());
    const auto& scene = program.playback().scene();
    const auto capacity = scene.frame_capacity();
    renderer.reserve({capacity.vertices, capacity.indices, capacity.commands,
                      scene.textures().size(), capacity.mask_commands, capacity.masks});
    for (const auto& texture : scene.textures())
    {
        auto image = asset_io::load_png(o.model.parent_path() / texture.path);
        if (!image.ok)
            throw std::runtime_error(image.error);
        renderer.upload_texture(texture.id, image.image);
    }
    std::unique_ptr<render_gl::Renderer> background;
    std::array<RenderVertex, 4> bg_vertices{};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    const std::array<DrawCommand, 1> command{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    int image_width = 0, image_height = 0;
    if (!o.background.empty())
    {
        auto image = asset_io::load_png(o.background);
        if (!image.ok)
            throw std::runtime_error(image.error);
        image_width = image.image.width;
        image_height = image.image.height;
        background = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
        background->reserve({4, 6, 1, 1});
        background->upload_texture(TextureId{1}, image.image);
    }
    std::vector<double> times(static_cast<std::size_t>(o.measure));
    std::vector<double> cadence(static_cast<std::size_t>(o.measure));
    Clock::time_point measure_begin{};
    float maximum_audio_rms = 0;
    std::size_t measured = 0;
    int warmup = 120, last_width = 0, last_height = 0;
    auto previous = Clock::now();
    bool captured = false;
    if (!o.capture.empty())
        for (int i = 0; i < o.steps; ++i)
            check(session.advance(1.0 / 60));
    std::printf("NekoMote: local pointer=%s, touch=%s, audio=%s\n",
                program.gaze_available() ? "available" : "unavailable",
                program.touch_available() ? "available" : "unavailable",
                o.mute ? "disabled" : "device");
    while (!glfwWindowShouldClose(window.get()))
    {
        glfwPollEvents();
        check(surface.error);
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
        if (device)
        {
            while (const auto audio_command = session.pop_audio_command())
                if (!device->submit(*audio_command))
                    throw std::runtime_error("audio command queue full or device failed");
            const auto snapshot = device->snapshot();
            if (snapshot.state == audio::DeviceState::Failed)
                throw std::runtime_error("audio device failed " + std::to_string(snapshot.error));
            maximum_audio_rms = std::max(maximum_audio_rms, snapshot.rms);
            check(session.set_audio_level(snapshot.rms));
        }
        const auto begin = Clock::now();
        if (o.capture.empty())
            check(session.advance(elapsed));
        if (w <= 0 || h <= 0)
        {
            glfwWaitEventsTimeout(.1);
            continue;
        }
        check(session.prepare_frame());
        renderer.prepare(session.frame());
        if (w != last_width || h != last_height)
        {
            renderer.resize_output(w, h);
            std::printf("Output %dx%d, model scale %.6f origin (%.3f, %.3f)\n", w, h,
                        static_cast<double>(surface.scale), static_cast<double>(surface.origin.x),
                        static_cast<double>(surface.origin.y));
            if (background)
            {
                background_vertices(bg_vertices, w, h, image_width, image_height);
                background->prepare({bg_vertices, indices, command});
            }
            last_width = w;
            last_height = h;
        }
        const std::array<float, 4> clear = o.transparent ? std::array<float, 4>{0, 0, 0, 0}
                                                         : std::array<float, 4>{.06f, .07f, .1f, 1};
        const render_gl::Target target{0, w, h};
        if (background)
            background->render(target, {0, 0, 1, clear});
        renderer.render(
            target, {static_cast<float>(w) * .5f, static_cast<float>(h) * .5f, 1, clear},
            background ? render_gl::TargetContents::Preserve : render_gl::TargetContents::Clear);
        if (!o.capture.empty())
        {
            const auto error = asset_io::write_png(o.capture, renderer.capture(target));
            if (!error.empty())
                throw std::runtime_error(error);
            captured = true;
            break;
        }
        // CPU submission time excludes vsync and GPU readback. Presentation
        // pacing is reported separately; this is not a GPU/power benchmark.
        const auto cpu_ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        glfwSwapBuffers(window.get());
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
        std::printf(
            "Swap-call cadence: %.3f fps P50=%.6f P95=%.6f P99=%.6f ms; audio RMS max %.6f\n",
            static_cast<double>(measured) / measure_seconds, pacing(.5), pacing(.95), pacing(.99),
            static_cast<double>(maximum_audio_rms));
    }
    if (!o.capture.empty() && !captured)
        throw std::runtime_error("capture interrupted");
    // Clear callbacks before borrowed surface/session disappear; GL objects
    // destruct before the owning window/context, audio worker joins afterward.
    glfwSetCursorPosCallback(window.get(), nullptr);
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
