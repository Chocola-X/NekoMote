#include <catch2/catch_test_macros.hpp>
#ifdef NEKOMOTE_HAS_ABI
#include <nekomote/abi.h>
#endif
#include <nekomote/asset_io/image.hpp>
#include <nekomote/render_gl/renderer.hpp>

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <new>
#include <stdexcept>

namespace
{
thread_local bool fail_allocations = false;
thread_local bool track_allocations = false;
thread_local std::size_t allocation_count = 0;
} // namespace
// Observe C++ allocations in the warmed project prepare/render path. Driver
// malloc and screenshot/test-framework allocations are outside this measurement.
void* operator new(std::size_t size)
{
    if (fail_allocations)
        throw std::bad_alloc();
    if (track_allocations)
        ++allocation_count;
    if (void* p = std::malloc(size ? size : 1))
        return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
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

namespace
{
using namespace nekomote;
struct Context
{
    GLFWwindow* window{};
    Context()
    {
        if (!glfwInit())
            throw std::runtime_error("GLFW init failed");
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        window = glfwCreateWindow(64, 64, "GPU regression", nullptr, nullptr);
        if (!window)
        {
            glfwTerminate();
            throw std::runtime_error("GLFW context failed");
        }
        glfwMakeContextCurrent(window);
    }
    ~Context()
    {
        glfwDestroyWindow(window);
        glfwTerminate();
    }
    Context(const Context&) = delete;
    Context& operator=(const Context&) = delete;
};
struct Output
{
    GLuint framebuffer{}, texture{};
    Output()
    {
        glGenFramebuffers(1, &framebuffer);
        glGenTextures(1, &texture);
    }
    ~Output()
    {
        glDeleteFramebuffers(1, &framebuffer);
        glDeleteTextures(1, &texture);
    }
    Output(const Output&) = delete;
    Output& operator=(const Output&) = delete;
    void resize(int w, int h)
    {
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
    }
};
std::array<std::uint8_t, 4> pixel(const ImageRgba8& image, int x, int y)
{
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                    static_cast<std::size_t>(x)) *
                   4;
    return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}
const ImageRgba8 kAtlas{2, 2, {255, 0, 0, 128, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0}};
const std::array<std::uint32_t, 12> kIndices{0, 1, 2, 0, 2, 3, 4, 5, 6, 4, 6, 7};
std::array<RenderVertex, 4> quad(float u0, float v0, float u1, float v1, float opacity = 1)
{
    return {{{0, 0, u0, v0, {1, 1, 1, opacity}},
             {64, 0, u1, v0, {1, 1, 1, opacity}},
             {64, 64, u1, v1, {1, 1, 1, opacity}},
             {0, 64, u0, v1, {1, 1, 1, opacity}}}};
}
} // namespace
TEST_CASE("GPU atlas orientation, alpha, host target and resize", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    renderer.reserve({8, 12, 2, 1});
    renderer.upload_texture(TextureId{1}, kAtlas);
    output.resize(64, 64);
    const auto vertices = quad(0, 0, 1, 1);
    const std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    renderer.prepare({vertices, std::span(kIndices).first(6), commands});
    // Dirty host state must not leak into the owned render pass.
    glEnable(GL_SCISSOR_TEST);
    glScissor(0, 0, 1, 1);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    renderer.render({output.framebuffer, 64, 64}, {});
    auto image = renderer.capture({output.framebuffer, 64, 64});
    // Pixel centres fall slightly between atlas texel centres under linear
    // filtering. Check the known quadrant and quantized alpha, not a nearest
    // sampling result that this renderer does not request.
    auto sample = pixel(image, 16, 16);
    REQUIRE(sample[0] > 240);
    REQUIRE(sample[1] < 8);
    REQUIRE(sample[2] < 8);
    REQUIRE(sample[3] >= 128);
    REQUIRE(sample[3] <= 134);
    sample = pixel(image, 48, 16);
    REQUIRE(sample[0] < 8);
    REQUIRE(sample[1] > 245);
    REQUIRE(sample[2] < 8);
    REQUIRE(sample[3] > 245);
    sample = pixel(image, 16, 48);
    REQUIRE(sample[0] < 8);
    REQUIRE(sample[1] < 8);
    REQUIRE(sample[2] > 245);
    REQUIRE(sample[3] > 245);
    REQUIRE(pixel(image, 48, 48)[3] == 0);
    output.resize(128, 128);
    renderer.render({output.framebuffer, 128, 128}, {0, 0, 2, {0, 0, 0, 0}});
    image = renderer.capture({output.framebuffer, 128, 128});
    sample = pixel(image, 32, 32);
    REQUIRE(sample[0] > 245);
    REQUIRE(sample[3] >= 128);
    REQUIRE(sample[3] <= 132);
    sample = pixel(image, 96, 32);
    REQUIRE(sample[0] < 4);
    REQUIRE(sample[1] > 250);
    REQUIRE(sample[3] > 250);
    REQUIRE(glGetError() == GL_NO_ERROR);
}
TEST_CASE("GPU preserves transparent draw order and warmed frames allocate no C++ heap", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.reserve({8, 12, 2, 1});
    renderer.upload_texture(TextureId{1}, kAtlas);
    auto red = quad(0.25f, 0.25f, 0.25f, 0.25f), green = quad(0.75f, 0.25f, 0.75f, 0.25f, 0.5f);
    std::array<RenderVertex, 8> vertices{};
    std::copy(red.begin(), red.end(), vertices.begin());
    std::copy(green.begin(), green.end(), vertices.begin() + 4);
    std::array<DrawCommand, 2> commands{
        {{TextureId{1}, 0, 6, BlendMode::Over}, {TextureId{1}, 6, 6, BlendMode::Over}}};
    RenderFrame frame{vertices, kIndices, commands};
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    auto image = renderer.capture({output.framebuffer, 64, 64});
    auto p = pixel(image, 32, 32);
    REQUIRE(p[3] >= 191);
    REQUIRE(p[3] <= 192);
    REQUIRE(p[0] >= 84);
    REQUIRE(p[0] <= 86);
    REQUIRE(p[1] >= 169);
    REQUIRE(p[1] <= 171);
    std::swap(commands[0], commands[1]);
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    p = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(p[0] >= 169);
    REQUIRE(p[1] <= 86);
    for (int i = 0; i < 120; ++i)
    {
        renderer.prepare(frame);
        renderer.render({output.framebuffer, 64, 64}, {});
    }
    allocation_count = 0;
    track_allocations = true;
    for (int i = 0; i < 100; ++i)
    {
        renderer.prepare(frame);
        renderer.render({output.framebuffer, 64, 64}, {});
    }
    track_allocations = false;
    REQUIRE(allocation_count == 0);
    REQUIRE(glGetError() == GL_NO_ERROR);
}
TEST_CASE("GPU rejects bad frames and supports repeated resource lifetimes", "[gpu]")
{
    Context context;
    for (int i = 0; i < 12; ++i)
    {
        render_gl::Renderer renderer(glfwGetProcAddress);
        renderer.reserve({4, 6, 1, 1});
        renderer.upload_texture(TextureId{1}, kAtlas);
        REQUIRE_THROWS(renderer.upload_texture(TextureId{1}, kAtlas));
        const auto vertices = quad(0, 0, 1, 1);
        const std::array<DrawCommand, 1> commands{{{TextureId{99}, 0, 6, BlendMode::Over}}};
        REQUIRE_THROWS(renderer.prepare({vertices, std::span(kIndices).first(6), commands}));
        renderer.prepare({{}, {}, {}});
        renderer.render({0, 64, 64}, {});
        renderer.render({0, 0, 0}, {});
        REQUIRE(glGetError() == GL_NO_ERROR);
    }
}

TEST_CASE("GPU protected straight colour differs from ordinary premultiplied over", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.reserve({8, 12, 2, 2});
    renderer.upload_texture(TextureId{1}, {1, 1, {255, 0, 0, 128}});
    renderer.upload_texture(TextureId{2}, {1, 1, {0, 255, 0, 255}});
    const auto red = quad(0.5f, 0.5f, 0.5f, 0.5f);
    auto green = quad(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    std::array<RenderVertex, 8> vertices{};
    std::copy(red.begin(), red.end(), vertices.begin());
    std::copy(green.begin(), green.end(), vertices.begin() + 4);
    std::array<DrawCommand, 2> commands{
        {{TextureId{1}, 0, 6, BlendMode::Over, ColorProtection::PreserveTranslucent},
         {TextureId{2}, 6, 6, BlendMode::Over, ColorProtection::PreserveTranslucent}}};
    RenderFrame frame{vertices, kIndices, commands, ColorComposition::ProtectedStraight};
    renderer.prepare(frame);
    REQUIRE_THROWS(renderer.render({output.framebuffer, 64, 64}, {}));
    renderer.resize_output(64, 64);
    GLint green_bits = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &green_bits);
    INFO("mask attachment green bits=" << green_bits);
    renderer.render({output.framebuffer, 64, 64}, {});
    auto sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(sample[0] >= 63);
    REQUIRE(sample[0] <= 65);
    REQUIRE(sample[1] >= 190);
    REQUIRE(sample[1] <= 192);
    REQUIRE(sample[3] >= 191);
    REQUIRE(sample[3] <= 192);
    frame.commands = std::span(commands).first(1);
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(sample[0] == 255);
    REQUIRE(sample[3] == 128);
    commands[0].protection = ColorProtection::None;
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(sample[0] >= 127);
    REQUIRE(sample[0] <= 129);
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("GPU multiply preserves alpha, weights opacity and restores ordinary over", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.resize_output(64, 64);
    renderer.reserve({8, 12, 3, 4});
    renderer.upload_texture(TextureId{1}, {1, 1, {200, 100, 50, 255}});
    renderer.upload_texture(TextureId{2}, {1, 1, {128, 64, 255, 128}});
    renderer.upload_texture(TextureId{3}, {1, 1, {255, 0, 0, 128}});
    renderer.upload_texture(TextureId{4}, {1, 1, {200, 100, 50, 128}});
    const auto background = quad(0.5f, 0.5f, 0.5f, 0.5f);
    const auto multiply = quad(0.5f, 0.5f, 0.5f, 0.5f, 0.5f);
    std::array<RenderVertex, 8> vertices{};
    std::copy(background.begin(), background.end(), vertices.begin());
    std::copy(multiply.begin(), multiply.end(), vertices.begin() + 4);
    std::array<DrawCommand, 3> commands{{{TextureId{1}, 0, 6, BlendMode::Over},
                                         {TextureId{2}, 6, 6, BlendMode::Multiply},
                                         {TextureId{3}, 0, 6, BlendMode::Over}}};
    RenderFrame frame{vertices, kIndices, std::span(commands).first(2),
                      ColorComposition::ProtectedStraight};
    renderer.prepare(frame);
    renderer.resize_output(64, 64);
    renderer.render({output.framebuffer, 64, 64}, {});
    auto sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    // Source RGB is lerped against white using texture alpha * node opacity.
    REQUIRE(sample[0] >= 174);
    REQUIRE(sample[0] <= 176);
    REQUIRE(sample[1] >= 80);
    REQUIRE(sample[1] <= 82);
    REQUIRE(sample[2] == 50);
    REQUIRE(sample[3] == 255);
    render_gl::View faded{};
    faded.alpha = 0.5f;
    renderer.render({output.framebuffer, 64, 64}, faded);
    sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(sample[0] >= 173);
    REQUIRE(sample[0] <= 177);
    REQUIRE(sample[3] >= 127);
    REQUIRE(sample[3] <= 128);

    // Following Over must use straight shader RGB again, even across frames.
    frame.commands = commands;
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(sample[0] >= 214);
    REQUIRE(sample[0] <= 216);
    REQUIRE(sample[1] >= 39);
    REQUIRE(sample[1] <= 41);
    REQUIRE(sample[3] == 255);
    allocation_count = 0;
    track_allocations = true;
    for (int i = 0; i < 100; ++i)
    {
        renderer.prepare(frame);
        renderer.render({output.framebuffer, 64, 64}, {});
    }
    track_allocations = false;
    REQUIRE(allocation_count == 0);
    commands[0].texture = TextureId{4};
    frame.commands = std::span(commands).first(2);
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(sample[3] == 128); // Multiply must not accumulate source alpha.
    REQUIRE(sample[0] >= 86);
    REQUIRE(sample[0] <= 89);
    frame.commands = std::span(commands).subspan(1, 1);
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    REQUIRE(pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32)[3] == 0);
    commands[1].protection = ColorProtection::PreserveTranslucent;
    REQUIRE_THROWS(renderer.prepare(frame));
    commands[1].protection = ColorProtection::None;
    frame.composition = ColorComposition::PremultipliedOver;
    REQUIRE_THROWS(renderer.prepare(frame));
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("GPU alpha masks union with observed five-bit precision and reuse resized resources",
          "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.reserve({8, 12, 1, 2, 2, 1});
    renderer.upload_texture(TextureId{1}, {1, 1, {255, 0, 0, 128}});
    renderer.upload_texture(TextureId{2}, {1, 1, {0, 255, 0, 255}});
    auto vertices = quad(0.5f, 0.5f, 0.5f, 0.5f);
    std::array<DrawCommand, 1> commands{
        {{TextureId{2}, 0, 6, BlendMode::Over, ColorProtection::PreserveTranslucent, MaskId{1}}}};
    std::array<DrawCommand, 2> mask_commands{
        {{TextureId{1}, 0, 6, BlendMode::Over}, {TextureId{1}, 0, 6, BlendMode::Over}}};
    std::array<MaskDefinition, 1> masks{{{0, 2}}};
    RenderFrame frame{vertices,      std::span(kIndices).first(6),
                      commands,      ColorComposition::ProtectedStraight,
                      mask_commands, masks};
    renderer.prepare(frame);
    renderer.resize_output(64, 64);
    GLint green_bits = 0;
    glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                          GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE, &green_bits);
    INFO("mask attachment green bits=" << green_bits);
    renderer.render({output.framebuffer, 64, 64}, {});
    auto sample = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
    REQUIRE(sample[1] == 255);
    INFO("mask alpha=" << static_cast<int>(sample[3]));
    REQUIRE(sample[3] >= 197);
    REQUIRE(sample[3] <= 198); // quantized union: 16/31 then 24/31; sampling rounds to RGBA8
    masks[0].command_count = 0;
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    REQUIRE(pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32)[3] == 0);
    masks[0].command_count = 2;
    renderer.prepare(frame);
    for (int i = 0; i < 120; ++i)
        renderer.render({output.framebuffer, 64, 64}, {});
    allocation_count = 0;
    track_allocations = true;
    for (int i = 0; i < 100; ++i)
    {
        renderer.prepare(frame);
        renderer.resize_output(64, 64);
        renderer.render({output.framebuffer, 64, 64}, {});
    }
    track_allocations = false;
    REQUIRE(allocation_count == 0);
    output.resize(128, 128);
    REQUIRE_THROWS(renderer.render({output.framebuffer, 128, 128}, {}));
    renderer.resize_output(128, 128);
    renderer.render({output.framebuffer, 128, 128}, {0, 0, 2, {}});
    sample = pixel(renderer.capture({output.framebuffer, 128, 128}), 64, 64);
    REQUIRE(sample[3] >= 197);
    REQUIRE(sample[3] <= 198);
    masks[0] = {2, 1};
    REQUIRE_THROWS(renderer.prepare(frame));
    masks[0] = {0, 2};
    mask_commands[0].mask = MaskId{1};
    REQUIRE_THROWS(renderer.prepare(frame)); // recursive mask reads are explicitly unsupported
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("Opacity-zero source vertices retain full texture mask coverage", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.reserve({8, 12, 1, 2, 1, 1});
    renderer.upload_texture(TextureId{1}, {1, 1, {255, 0, 0, 255}});
    renderer.upload_texture(TextureId{2}, {1, 1, {0, 255, 0, 255}});
    const auto color = quad(0, 0, 1, 1);
    const auto mask = quad(0, 0, 1, 1, 0);
    std::array<RenderVertex, 8> vertices;
    std::copy(color.begin(), color.end(), vertices.begin());
    std::copy(mask.begin(), mask.end(), vertices.begin() + 4);
    std::array<DrawCommand, 1> commands{
        {{TextureId{1}, 0, 6, BlendMode::Over, ColorProtection::None, MaskId{1}}}};
    std::array<DrawCommand, 1> mask_commands{{{TextureId{2}, 6, 6, BlendMode::Over}}};
    std::array<MaskDefinition, 1> masks{{{0, 1}}};
    renderer.prepare(
        {vertices, kIndices, commands, ColorComposition::ProtectedStraight, mask_commands, masks});
    renderer.resize_output(64, 64);
    renderer.render({output.framebuffer, 64, 64}, {});
    REQUIRE(pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32) ==
            std::array<unsigned char, 4>{255, 0, 0, 255});
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("Model compositing preserves a host background in both composition modes", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.resize_output(64, 64);
    renderer.reserve({4, 6, 1, 1});
    renderer.upload_texture(TextureId{1}, {1, 1, {255, 0, 0, 128}});
    const auto vertices = quad(0, 0, 1, 1);
    std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    for (const auto composition :
         {ColorComposition::PremultipliedOver, ColorComposition::ProtectedStraight})
    {
        commands[0].protection = composition == ColorComposition::ProtectedStraight
                                     ? ColorProtection::PreserveTranslucent
                                     : ColorProtection::None;
        renderer.prepare({vertices, std::span(kIndices).first(6), commands, composition});
        renderer.resize_output(64, 64);
        glBindFramebuffer(GL_FRAMEBUFFER, output.framebuffer);
        glDisable(GL_SCISSOR_TEST);
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glClearColor(0, 0, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        renderer.render({output.framebuffer, 64, 64}, {}, render_gl::TargetContents::Preserve);
        auto p = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
        REQUIRE(p[0] >= 127);
        REQUIRE(p[0] <= 129);
        REQUIRE(p[1] == 0);
        REQUIRE(p[2] >= 126);
        REQUIRE(p[2] <= 128);
        REQUIRE(p[3] == 255);
        renderer.render({output.framebuffer, 64, 64}, {});
        p = pixel(renderer.capture({output.framebuffer, 64, 64}), 32, 32);
        REQUIRE(p[2] == 0);
        REQUIRE(p[3] == 128);
    }
}

#ifdef NEKOMOTE_HAS_ABI
TEST_CASE("C renderer validates real handles and cannot reuse failed frames", "[gpu][abi]")
{
    Context context;
    auto* renderer = nekomote_renderer_create(glfwGetProcAddress);
    REQUIRE(renderer);
    const std::unique_ptr<nekomote_renderer, decltype(&nekomote_renderer_destroy)> owner(
        renderer, nekomote_renderer_destroy);
    Output output;
    output.resize(64, 64);
    const std::array<std::uint8_t, 4> red{255, 0, 0, 255};
    REQUIRE(nekomote_renderer_reserve(renderer, {0, 0, 0, 1, 0, 0}) == NEKOMOTE_OK);
    REQUIRE(nekomote_renderer_upload_texture(renderer, 1, 1, 1, red.data(), 3) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_renderer_upload_texture(renderer, 1, -1, 1, red.data(), 4) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_renderer_upload_texture(renderer, 1, 1, 1, red.data(), 4) == NEKOMOTE_OK);
    REQUIRE(nekomote_renderer_resize_output(renderer, 0, 64) == NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_renderer_resize_output(renderer, 64, 64) == NEKOMOTE_OK);
    std::array<nekomote_vertex, 4> vertices{{{0, 0, 0, 0, {1, 1, 1, 1}},
                                             {64, 0, 1, 0, {1, 1, 1, 1}},
                                             {64, 64, 1, 1, {1, 1, 1, 1}},
                                             {0, 64, 0, 1, {1, 1, 1, 1}}}};
    nekomote_draw_command command{1, 0, 6, NEKOMOTE_BLEND_OVER, NEKOMOTE_PROTECTION_NONE, 0};
    nekomote_render_frame frame{vertices.data(),
                                vertices.size(),
                                kIndices.data(),
                                6,
                                &command,
                                1,
                                NEKOMOTE_COMPOSITION_PREMULTIPLIED_OVER,
                                nullptr,
                                0,
                                nullptr,
                                0};
    // No reserve yet: the copy must allocate, and the C entry must contain OOM.
    fail_allocations = true;
    const auto failure = nekomote_renderer_prepare(renderer, frame);
    fail_allocations = false;
    REQUIRE(failure == NEKOMOTE_ERROR_OUT_OF_MEMORY);
    REQUIRE(nekomote_last_error() == NEKOMOTE_ERROR_OUT_OF_MEMORY);
    REQUIRE(nekomote_error_message());
    REQUIRE(nekomote_renderer_reserve(renderer, {4, 6, 1, 1, 0, 0}) == NEKOMOTE_OK);
    REQUIRE(nekomote_renderer_prepare(renderer, frame) == NEKOMOTE_OK);
    nekomote_render_view view{};
    view.scale = view.alpha = 1;
    const nekomote_render_target target{output.framebuffer, 64, 64};
    REQUIRE(nekomote_renderer_render(renderer, target, view, NEKOMOTE_TARGET_CLEAR) == NEKOMOTE_OK);
    render_gl::Renderer inspector(glfwGetProcAddress);
    REQUIRE(pixel(inspector.capture({output.framebuffer, 64, 64}), 32, 32) == red);
    for (const auto invalid : {0, 1, 2, 3})
    {
        auto bad = frame;
        auto bad_command = command;
        bad.commands = &bad_command;
        if (invalid == 0)
            bad_command.blend = static_cast<nekomote_blend_mode>(999);
        else if (invalid == 1)
            bad_command.protection = static_cast<nekomote_color_protection>(999);
        else if (invalid == 2)
            bad.composition = static_cast<nekomote_color_composition>(999);
        else
            bad.vertices = nullptr;
        REQUIRE(nekomote_renderer_prepare(renderer, bad) == NEKOMOTE_ERROR_INVALID_INPUT);
        glBindFramebuffer(GL_FRAMEBUFFER, output.framebuffer);
        glClearColor(1, 1, 1, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        REQUIRE(nekomote_renderer_render(renderer, target, view, NEKOMOTE_TARGET_PRESERVE) ==
                NEKOMOTE_ERROR_RENDERER);
        REQUIRE(pixel(inspector.capture({output.framebuffer, 64, 64}), 32, 32) ==
                std::array<std::uint8_t, 4>{255, 255, 255, 255});
        REQUIRE(nekomote_renderer_prepare(renderer, frame) == NEKOMOTE_OK);
    }
    allocation_count = 0;
    track_allocations = true;
    for (int i = 0; i < 100; ++i)
    {
        (void)nekomote_renderer_prepare(renderer, frame);
        (void)nekomote_renderer_render(renderer, target, view, NEKOMOTE_TARGET_CLEAR);
    }
    track_allocations = false;
    REQUIRE(allocation_count == 0);
    REQUIRE(nekomote_renderer_prepare(renderer, {}) == NEKOMOTE_OK);
    REQUIRE(glGetError() == GL_NO_ERROR);
}
#endif
