#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
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
#include <vector>

namespace
{
using namespace nekomote;
struct Options
{
    std::filesystem::path asset, screenshot;
    std::string icon;
    int width{800}, height{1080}, frames{0}, benchmark{0};
    float x{400}, y{540}, scale{1}, opacity{1};
    bool hidden{false};
};
float parse_number(const std::string& value)
{
    std::size_t end = 0;
    const auto result = std::stof(value, &end);
    if (end != value.size() || !std::isfinite(result))
        throw std::runtime_error("invalid number: " + value);
    return result;
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
        else if (flag == "--icon")
            o.icon = next();
        else if (flag == "--screenshot")
            o.screenshot = next();
        else if (flag == "--width")
            o.width = std::stoi(next());
        else if (flag == "--height")
            o.height = std::stoi(next());
        else if (flag == "--frames")
            o.frames = std::stoi(next());
        else if (flag == "--benchmark")
            o.benchmark = std::stoi(next());
        else if (flag == "--origin")
        {
            o.x = parse_number(next());
            o.y = parse_number(next());
        }
        else if (flag == "--scale")
            o.scale = parse_number(next());
        else if (flag == "--opacity")
            o.opacity = parse_number(next());
        else if (flag == "--hidden")
            o.hidden = true;
        else
            throw std::runtime_error("unknown argument: " + flag);
    }
    if (o.width <= 0 || o.height <= 0 || o.width > 16384 || o.height > 16384 || o.frames < 0 ||
        o.benchmark < 0 || o.scale <= 0 || o.opacity < 0 || o.opacity > 1)
        throw std::runtime_error("invalid output/diagnostic settings");
    if (!o.asset.empty() && o.icon.empty())
        throw std::runtime_error(
            "P2 requires --icon texture/icon; complete model evaluation is not implemented");
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
    renderer.reserve(4, 6, 1, 1);
    std::array<RenderVertex, 4> vertices{};
    const std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    const std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    render_gl::View view{o.x, o.y, o.scale, {0, 0, 0, 0}};
    if (!o.asset.empty())
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
    while (!glfwWindowShouldClose(window.get()))
    {
        int w = 0, h = 0;
        glfwGetFramebufferSize(window.get(), &w, &h);
        const render_gl::Target target{0, w, h};
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
