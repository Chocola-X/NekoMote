#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/asset_io/image.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nlohmann/json.hpp>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <array>
#include <cstdio>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
using namespace nekomote;
using Json = nlohmann::json;
struct Glfw
{
    Glfw()
    {
        if (!glfwInit())
            throw std::runtime_error("cannot initialize GLFW");
    }
    ~Glfw()
    {
        glfwTerminate();
    }
};
struct WindowDeleter
{
    void operator()(GLFWwindow* w) const noexcept
    {
        glfwDestroyWindow(w);
    }
};
std::vector<DrawCommand> commands(const Json& list)
{
    std::vector<DrawCommand> result;
    if (!list.is_array() || list.size() > 100000)
        throw std::runtime_error("invalid command count");
    for (const auto& c : list)
        result.push_back({TextureId{c.at("texture").get<std::uint32_t>()},
                          c.at("first_index").get<std::uint32_t>(),
                          c.at("index_count").get<std::uint32_t>(),
                          static_cast<BlendMode>(c.at("blend").get<int>()),
                          static_cast<ColorProtection>(c.at("protection").get<int>()),
                          MaskId{c.at("mask").get<std::uint32_t>()}});
    return result;
}
int run(int argc, char** argv)
{
    if (argc != 4)
        throw std::runtime_error(
            "usage: nekomote_frame_render MODEL OBSERVATION_JSON OUTPUT_PNG (800x1080)");
    auto asset = asset_io::load_asset(argv[1]);
    if (!asset.ok)
        throw std::runtime_error(asset.errors.front());
    auto geometry = runtime::compile_geometry_scene(asset.asset);
    if (!geometry.scene)
        throw std::runtime_error(geometry.errors.front());
    std::ifstream input(argv[2]);
    if (!input)
        throw std::runtime_error("cannot open frame observation");
    const auto observation = Json::parse(input);
    const auto& frame = observation.at("frame");
    std::vector<RenderVertex> vertices;
    if (!frame.at("vertices").is_array() || frame.at("vertices").size() > 1000000)
        throw std::runtime_error("invalid vertex count");
    for (const auto& v : frame.at("vertices"))
    {
        if (!v.is_array() || v.size() != 8)
            throw std::runtime_error(
                "current frame observation requires xyuv+RGBA; rebuild observations");
        vertices.push_back(
            {v[0].get<float>(),
             v[1].get<float>(),
             v[2].get<float>(),
             v[3].get<float>(),
             {v[4].get<float>(), v[5].get<float>(), v[6].get<float>(), v[7].get<float>()}});
    }
    auto indices = frame.at("indices").get<std::vector<std::uint32_t>>();
    auto draws = commands(frame.at("commands"));
    auto mask_commands = commands(frame.at("mask_commands"));
    std::vector<MaskDefinition> masks;
    for (const auto& m : frame.at("masks"))
        masks.push_back({m.at("first_command").get<std::uint32_t>(),
                         m.at("command_count").get<std::uint32_t>()});
    Glfw glfw;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    glfwWindowHint(GLFW_ALPHA_BITS, 8);
    std::unique_ptr<GLFWwindow, WindowDeleter> window(
        glfwCreateWindow(800, 1080, "NekoMote frame observation", nullptr, nullptr));
    if (!window)
        throw std::runtime_error("cannot create diagnostic GL window");
    glfwMakeContextCurrent(window.get());
    render_gl::Renderer renderer(glfwGetProcAddress);
    renderer.reserve({vertices.size(), indices.size(), draws.size(),
                      geometry.scene->textures().size(), mask_commands.size(), masks.size()});
    for (const auto& t : geometry.scene->textures())
    {
        auto image = asset_io::load_png(std::filesystem::path(argv[1]).parent_path() / t.path);
        if (!image.ok)
            throw std::runtime_error(image.error);
        renderer.upload_texture(t.id, image.image);
    }
    renderer.prepare({vertices, indices, draws,
                      static_cast<ColorComposition>(frame.at("composition").get<int>()),
                      mask_commands, masks});
    renderer.resize_output(800, 1080);
    renderer.render({0, 800, 1080}, {400, 540, 1, {0, 0, 0, 0}});
    const auto error = asset_io::write_png(argv[3], renderer.capture({0, 800, 1080}));
    if (!error.empty())
        throw std::runtime_error(error);
    std::printf("Diagnostic frame observation only; no model evaluator certification.\n");
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
        std::fprintf(stderr, "nekomote_frame_render: %s\n", e.what());
        return 1;
    }
}
