#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nekomote/runtime/playback.hpp>
#include <nlohmann/json.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
using namespace nekomote;
struct VariableTransition
{
    std::string label;
    float value{0}, frames{0}, easing{0};
};
struct Options
{
    std::filesystem::path asset, screenshot, playback_script;
    std::string icon;
    int width{800}, height{1080}, frames{0}, benchmark{0};
    float x{400}, y{540}, scale{1}, opacity{1};
    bool hidden{false}, geometry{false}, animation{false}, playback{false};
    bool disable_eye_controller{false};
    int steps{0};
    std::uint32_t random_seed{0};
    std::vector<std::pair<std::string, runtime::TimelinePlayMode>> plays;
    std::vector<std::pair<std::string, float>> variables;
    std::vector<VariableTransition> transitions;
};
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
float parse_number(const std::string& value)
{
    std::size_t end = 0;
    const auto result = std::stof(value, &end);
    if (end != value.size() || !std::isfinite(result))
        throw std::runtime_error("invalid number: " + value);
    return result;
}
int parse_integer(const std::string& text)
{
    int value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size())
        throw std::runtime_error("invalid integer: " + text);
    return value;
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
Options parse(int argc, char** argv)
{
    Options o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string flag = argv[i];
        auto next = [&]() -> std::string
        {
            if (i + 1 >= argc)
                throw std::runtime_error("missing value for " + flag);
            return argv[++i];
        };
        if (flag == "--asset")
            o.asset = next();
        else if (flag == "--playback-script")
            o.playback_script = next();
        else if (flag == "--icon")
            o.icon = next();
        else if (flag == "--screenshot")
            o.screenshot = next();
        else if (flag == "--width")
            o.width = parse_integer(next());
        else if (flag == "--height")
            o.height = parse_integer(next());
        else if (flag == "--frames")
            o.frames = parse_integer(next());
        else if (flag == "--benchmark")
            o.benchmark = parse_integer(next());
        else if (flag == "--origin")
        {
            o.x = parse_number(next());
            o.y = parse_number(next());
        }
        else if (flag == "--scale")
            o.scale = parse_number(next());
        else if (flag == "--opacity")
            o.opacity = parse_number(next());
        else if (flag == "--steps")
            o.steps = parse_integer(next());
        else if (flag == "--random-seed")
        {
            const auto seed = next();
            const auto parsed =
                std::from_chars(seed.data(), seed.data() + seed.size(), o.random_seed);
            if (parsed.ec != std::errc{} || parsed.ptr != seed.data() + seed.size())
                throw std::runtime_error("random seed must be an unsigned 32-bit integer");
        }
        else if (flag == "--playback")
            o.playback = true;
        else if (flag == "--animation-diagnostic")
            o.animation = true;
        else if (flag == "--disable-eye-controller")
            o.disable_eye_controller = true;
        else if (flag == "--play")
        {
            const auto name = next();
            const auto flags = next();
            if (flags != "0" && flags != "1" && flags != "3")
                throw std::runtime_error("play flags must be 0, 1 or 3");
            o.plays.emplace_back(name, flags == "0" ? runtime::TimelinePlayMode::Replace
                                       : flags == "1"
                                           ? runtime::TimelinePlayMode::Parallel
                                           : runtime::TimelinePlayMode::AdditiveParallel);
        }
        else if (flag == "--geometry-diagnostic")
            o.geometry = true;
        else if (flag == "--variable")
        {
            const auto name = next();
            o.variables.emplace_back(name, parse_number(next()));
        }
        else if (flag == "--transition")
        {
            VariableTransition request;
            request.label = next();
            request.value = parse_number(next());
            request.frames = parse_number(next());
            request.easing = parse_number(next());
            if (request.frames < 0)
                throw std::runtime_error("transition frames must be nonnegative");
            o.transitions.push_back(std::move(request));
        }
        else if (flag == "--hidden")
            o.hidden = true;
        else
            throw std::runtime_error("unknown argument: " + flag);
    }
    if (!o.playback_script.empty() && (!(o.playback || o.animation) || o.screenshot.empty()))
        throw std::runtime_error("timeline script requires fixed-step playback screenshot");
    if (o.width <= 0 || o.height <= 0 || o.width > 16384 || o.height > 16384 || o.frames < 0 ||
        o.benchmark < 0 || o.steps < 0 || o.steps > 100000 || o.scale <= 0 || o.opacity < 0 ||
        o.opacity > 1)
        throw std::runtime_error("invalid output/diagnostic settings");
    if ((o.geometry || o.animation || o.playback) && (o.asset.empty() || !o.icon.empty()))
        throw std::runtime_error("geometry diagnostic requires an asset and no icon filter");
    if (!o.geometry && !o.animation && !o.playback && !o.variables.empty())
        throw std::runtime_error("--variable requires --geometry-diagnostic");
    if (!o.geometry && !o.animation && !o.playback && !o.asset.empty() && o.icon.empty())
        throw std::runtime_error(
            "whole-model output requires --playback or an explicit diagnostic mode");
    if ((static_cast<int>(o.geometry) + static_cast<int>(o.animation) +
             static_cast<int>(o.playback) >
         1) ||
        (!(o.animation || o.playback) && (o.steps || !o.plays.empty() || !o.transitions.empty())))
        throw std::runtime_error("select one geometry, animation diagnostic or playback mode");
    if (o.disable_eye_controller && !o.animation)
        throw std::runtime_error("Eye controller fixture requires --animation-diagnostic");
    if (o.asset.empty() && !o.icon.empty())
        throw std::runtime_error("--icon requires --asset");
    if (!o.screenshot.empty() && o.frames == 0)
        o.frames = 1;
    return o;
}
struct Glfw
{
    Glfw()
    {
        glfwSetErrorCallback([](int code, const char* text)
                             { std::fprintf(stderr, "GLFW %d: %s\n", code, text); });
        if (!glfwInit())
            throw std::runtime_error("GLFW initialization failed");
    }
    ~Glfw()
    {
        glfwTerminate();
    }
    Glfw(const Glfw&) = delete;
    Glfw& operator=(const Glfw&) = delete;
};
struct WindowDeleter
{
    void operator()(GLFWwindow* w) const
    {
        glfwDestroyWindow(w);
    }
};
int run(const Options& o)
{
    Glfw glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, o.hidden ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    std::unique_ptr<GLFWwindow, WindowDeleter> window(
        glfwCreateWindow(o.width, o.height, "NekoMote preview", nullptr, nullptr));
    if (!window)
        throw std::runtime_error("cannot create OpenGL 3.3 window");
    glfwMakeContextCurrent(window.get());
    glfwSwapInterval(o.benchmark ? 0 : 1);
    render_gl::Renderer renderer(glfwGetProcAddress);
    std::printf("%s\n", renderer.diagnostics().c_str());
    renderer.reserve({4, 6, 1, 1});
    std::array<RenderVertex, 4> vertices{};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    const std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    render_gl::View view{o.x, o.y, o.scale, {0, 0, 0, 0}};
    std::unique_ptr<const runtime::GeometryScene> static_scene;
    const runtime::GeometryScene* geometry_scene = nullptr;
    std::unique_ptr<runtime::GeometryInstance> geometry_instance;
    std::unique_ptr<const runtime::PlaybackProgram> playback_program;
    std::unique_ptr<runtime::PlaybackInstance> playback_instance;
    const auto playback_checked = [](runtime::PlaybackStatus status)
    {
        if (!status)
            throw std::runtime_error(
                "playback error=" + std::to_string(static_cast<int>(status.error)) +
                " animation=" + std::to_string(static_cast<int>(status.animation.error)) +
                " geometry=" + std::to_string(static_cast<int>(status.geometry.error)) +
                " node=" + std::to_string(status.geometry.node.value) +
                " physics=" + std::to_string(static_cast<int>(status.physics.error)) +
                " controller=" + std::to_string(status.physics.controller));
    };
    if (o.geometry || o.animation || o.playback)
    {
        auto loaded = asset_io::load_asset(o.asset);
        if (!loaded.ok)
            throw std::runtime_error(loaded.errors.front());
        if (o.disable_eye_controller)
        {
            if (!o.animation)
                throw std::runtime_error("Eye controller fixture requires --animation-diagnostic");
            for (auto& control : loaded.asset.variable_controls)
                if (control.kind == model::VariableControlKind::Eye)
                    control.enabled = false;
        }
        RenderFrame frame;
        if (o.animation || o.playback)
        {
            nlohmann::ordered_json script = nlohmann::ordered_json::object();
            if (!o.playback_script.empty())
            {
                std::ifstream input(o.playback_script);
                if (!input)
                    throw std::runtime_error("cannot open timeline script");
                script = nlohmann::ordered_json::parse(input);
            }
            const auto sources = variable_sources(script);
            auto compiled =
                runtime::compile_playback(loaded.asset,
                                          o.playback ? runtime::PhysicsMode::Enabled
                                                     : runtime::PhysicsMode::DisabledDiagnostic,
                                          sources);
            if (!compiled.program)
                throw std::runtime_error(compiled.errors.front());
            playback_program = std::move(compiled.program);
            geometry_scene = &playback_program->scene();
            playback_instance = std::make_unique<runtime::PlaybackInstance>(
                *playback_program, runtime::AnimationConfig{o.random_seed});
            {
                // Model placement participates in author-space Float32 matrices;
                // the final view separately maps the centered canvas to pixels.
                playback_checked(
                    playback_instance->set_origin({o.x - static_cast<float>(o.width) * 0.5f,
                                                   o.y - static_cast<float>(o.height) * 0.5f}));
                playback_checked(playback_instance->set_scale(o.scale));
                view.scale = 1;
                view.origin_x = static_cast<float>(o.width) * 0.5f;
                view.origin_y = static_cast<float>(o.height) * 0.5f;
            }
            for (const auto& [name, value] : o.variables)
                playback_checked(
                    playback_instance->transition(geometry_scene->find_variable(name), value));
            for (const auto& request : o.transitions)
                playback_checked(
                    playback_instance->transition(geometry_scene->find_variable(request.label),
                                                  request.value, request.frames, request.easing));
            for (const auto& [name, mode] : o.plays)
                playback_checked(playback_instance->play(
                    playback_program->animation().find_timeline(name), mode));
            timeline_requests(*playback_instance, playback_program->animation(),
                              script.value("timeline_requests", nlohmann::ordered_json::array()));
            difference_requests(*playback_instance, *playback_program,
                                script.value("differences", nlohmann::ordered_json::object()));
            const auto events = script.value("events", nlohmann::ordered_json::array());
            validate_events(events, o.steps);
            for (int i = 0; i < o.steps; ++i)
            {
                for (const auto& event : events)
                    if (event.at("step").get<int>() == i + 1)
                    {
                        timeline_requests(
                            *playback_instance, playback_program->animation(),
                            event.value("timeline_requests", nlohmann::ordered_json::array()));
                        difference_requests(
                            *playback_instance, *playback_program,
                            event.value("differences", nlohmann::ordered_json::object()));
                    }
                playback_checked(playback_instance->step());
            }
            playback_checked(playback_instance->prepare_frame());
            frame = playback_instance->frame();
        }
        else
        {
            auto compiled = runtime::compile_geometry_scene(loaded.asset);
            if (!compiled.scene)
                throw std::runtime_error(compiled.errors.front());
            static_scene = std::move(compiled.scene);
            geometry_scene = static_scene.get();
            geometry_instance = std::make_unique<runtime::GeometryInstance>(*geometry_scene);
            const auto checked = [&](runtime::GeometryStatus status)
            {
                if (!status)
                    throw std::runtime_error("geometry error " +
                                             std::to_string(static_cast<int>(status.error)) + ": " +
                                             std::string(geometry_scene->node_label(status.node)));
            };
            checked(geometry_instance->set_origin({o.x - static_cast<float>(o.width) * 0.5f,
                                                   o.y - static_cast<float>(o.height) * 0.5f}));
            checked(geometry_instance->set_scale(o.scale));
            view = {static_cast<float>(o.width) * 0.5f,
                    static_cast<float>(o.height) * 0.5f,
                    1,
                    {0, 0, 0, 0}};
            for (const auto& [name, value] : o.variables)
                checked(
                    geometry_instance->set_parameter(geometry_scene->find_variable(name), value));
            checked(geometry_instance->evaluate());
            checked(geometry_instance->build_frame());
            frame = geometry_instance->frame();
        }
        const auto capacity = geometry_scene->frame_capacity();
        renderer.reserve({capacity.vertices, capacity.indices, capacity.commands,
                          geometry_scene->textures().size(), capacity.mask_commands,
                          capacity.masks});
        for (const auto& texture : geometry_scene->textures())
        {
            auto decoded = asset_io::load_png(o.asset.parent_path() / texture.path);
            if (!decoded.ok)
                throw std::runtime_error(decoded.error);
            if (decoded.image.width != texture.width || decoded.image.height != texture.height)
                throw std::runtime_error("PNG dimensions disagree with compiled scene");
            renderer.upload_texture(texture.id, decoded.image);
        }
        view.alpha = o.opacity; // Whole-model opacity is applied at the final composite.
        view.flip_x = loaded.asset.mirror;
        renderer.prepare(frame);
        std::printf("%s: %zu draw commands\n",
                    o.playback    ? "PLAYBACK (validation in progress)"
                    : o.animation ? "ANIMATION DIAGNOSTIC (physics disabled)"
                                  : "STATIC DIAGNOSTIC",
                    frame.commands.size());
        for (const auto& diagnostic : geometry_scene->diagnostics())
            std::printf("%s\n", diagnostic.c_str());
    }
    else if (!o.asset.empty())
    {
        auto loaded = asset_io::load_asset(o.asset);
        if (!loaded.ok)
            throw std::runtime_error(loaded.errors.front());
        const auto& a = loaded.asset;
        const auto icon = a.icons.find(o.icon);
        if (icon == a.icons.end())
            throw std::runtime_error("unknown atlas icon: " + o.icon);
        const auto& r = icon->second;
        const auto texture = std::find_if(a.textures.begin(), a.textures.end(),
                                          [&r](const auto& t) { return t.name == r.texture; });
        auto image = asset_io::load_png(o.asset.parent_path() / texture->path);
        if (!image.ok)
            throw std::runtime_error(image.error);
        if (image.image.width != texture->width || image.image.height != texture->height)
            throw std::runtime_error("PNG dimensions disagree with asset atlas");
        renderer.upload_texture(TextureId{1}, image.image);
        const float w = static_cast<float>(texture->width), h = static_cast<float>(texture->height);
        const float x0 = -r.origin_x, y0 = -r.origin_y, x1 = x0 + r.width, y1 = y0 + r.height;
        const float u0 = r.left / w, v0 = r.top / h, u1 = (r.left + r.width) / w,
                    v1 = (r.top + r.height) / h;
        vertices = {RenderVertex{x0, y0, u0, v0, {1, 1, 1, o.opacity}},
                    RenderVertex{x1, y0, u1, v0, {1, 1, 1, o.opacity}},
                    RenderVertex{x1, y1, u1, v1, {1, 1, 1, o.opacity}},
                    RenderVertex{x0, y1, u0, v1, {1, 1, 1, o.opacity}}};
        renderer.prepare({vertices, indices, commands});
        std::printf("diagnostic atlas icon: %s; complete model capability: unavailable\n",
                    o.icon.c_str());
    }
    else
    {
        renderer.prepare({{}, {}, {}});
        view.clear = {0.10f, 0.06f, 0.16f, 1};
    }
    if (o.benchmark)
    {
        renderer.resize_output(o.width, o.height);
        const render_gl::Target target{0, o.width, o.height};
        for (int i = 0; i < 120; ++i)
        {
            renderer.render(target, view);
            glfwSwapBuffers(window.get());
        }
        std::vector<double> times;
        times.reserve(static_cast<std::size_t>(o.benchmark));
        for (int i = 0; i < o.benchmark; ++i)
        {
            const auto start = std::chrono::steady_clock::now();
            renderer.render(target, view);
            glfwSwapBuffers(window.get());
            times.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
        }
        std::sort(times.begin(), times.end());
        const auto percentile = [&times](std::size_t percent)
        { return times[(times.size() - 1) * percent / 100]; };
        std::printf(
            "render+swap CPU ms: P50=%.6f P95=%.6f P99=%.6f; samples=%d warmup=120 vsync=0\n",
            percentile(50), percentile(95), percentile(99), o.benchmark);
        return 0;
    }
    int count = 0;
    auto previous_frame = std::chrono::steady_clock::now();
    bool space_down = false;
    auto playback = runtime::PlaybackState::Running;
    while (!glfwWindowShouldClose(window.get()))
    {
        const auto now = std::chrono::steady_clock::now();
        const double elapsed = std::chrono::duration<double>(now - previous_frame).count();
        previous_frame = now;
        if (playback_instance && o.screenshot.empty())
        {
            const bool pressed = glfwGetKey(window.get(), GLFW_KEY_SPACE) == GLFW_PRESS;
            if (pressed && !space_down)
            {
                playback = playback == runtime::PlaybackState::Running
                               ? runtime::PlaybackState::Paused
                               : runtime::PlaybackState::Running;
                playback_instance->set_playback_state(playback);
            }
            space_down = pressed;
            const auto previous_tick = playback_instance->tick_count();
            playback_checked(playback_instance->advance(elapsed));
            if (playback_instance->tick_count() != previous_tick)
            {
                playback_checked(playback_instance->prepare_frame());
                renderer.prepare(playback_instance->frame());
            }
        }
        int w = 0, h = 0;
        glfwGetFramebufferSize(window.get(), &w, &h);
        const render_gl::Target target{0, w, h};
        renderer.resize_output(w, h);
        renderer.render(target, view);
        if (!o.screenshot.empty() && count == 0)
        {
            const auto error = asset_io::write_png(o.screenshot, renderer.capture(target));
            if (!error.empty())
                throw std::runtime_error(error);
        }
        glfwSwapBuffers(window.get());
        glfwPollEvents();
        ++count;
        if ((o.frames && count >= o.frames) ||
            glfwGetKey(window.get(), GLFW_KEY_ESCAPE) == GLFW_PRESS)
            break;
        if (w == 0 || h == 0)
            glfwWaitEventsTimeout(0.1);
    }
    return 0;
}
} // namespace
int main(int argc, char** argv)
{
    try
    {
        return run(parse(argc, argv));
    }
    catch (const std::exception& e)
    {
        std::fprintf(stderr, "nekomote_preview: %s\n", e.what());
        return 1;
    }
}
