#include <catch2/catch_test_macros.hpp>
#ifdef NEKOMOTE_HAS_ABI
#include <nekomote/abi.h>
#endif
#include <nekomote/asset_io/image.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_gl/ui_renderer.hpp>
#include <nekomote/ui/layout.hpp>

#include <glad/gl.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

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
        const bool gles = std::getenv("NEKOMOTE_TEST_GLES") != nullptr;
        glfwWindowHint(GLFW_CLIENT_API, gles ? GLFW_OPENGL_ES_API : GLFW_OPENGL_API);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, gles ? 0 : 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE,
                       gles ? GLFW_OPENGL_ANY_PROFILE : GLFW_OPENGL_CORE_PROFILE);
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
TEST_CASE("GPU cropped atlas storage preserves filtering and original clamp edges", "[gpu][crop]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    struct HostRectangle
    {
        GLuint handle{0};
        HostRectangle()
        {
            glGenTextures(1, &handle);
            glActiveTexture(GL_TEXTURE0);
            if (!std::getenv("NEKOMOTE_TEST_GLES"))
                glBindTexture(GL_TEXTURE_RECTANGLE, handle);
        }
        ~HostRectangle()
        {
            glDeleteTextures(1, &handle);
        }
    } host_rectangle;
    renderer.reserve({4, 6, 1, 4});
    ImageRgba8 cropped{7, 5, std::vector<std::uint8_t>(7 * 5 * 4, 0)};
    cropped.pixels[(2 * 7 + 3) * 4] = 240;
    cropped.pixels[(2 * 7 + 3) * 4 + 3] = 128;
    cropped.pixels[(1 * 7 + 2) * 4 + 3] = 1;
    cropped.pixels[(3 * 7 + 4) * 4 + 1] = 200; // transparent RGB
    for (const auto region : {TextureRegion{13, 11, 3, 2}, TextureRegion{13, 11, 0, 0}})
    {
        const bool edge = region.left == 0;
        if (edge)
        {
            cropped.pixels[0] = 220;
            cropped.pixels[3] = 180;
        }
        ImageRgba8 full{13, 11, std::vector<std::uint8_t>(13 * 11 * 4, 0)};
        for (int y = 0; y < cropped.height; ++y)
            std::copy_n(cropped.pixels.data() + y * cropped.width * 4, cropped.width * 4,
                        full.pixels.data() + ((y + region.top) * full.width + region.left) * 4);
        const TextureId original_id{edge ? 3U : 1U}, cropped_id{edge ? 4U : 2U};
        glActiveTexture(GL_TEXTURE0);
        GLint before = 0;
        const bool rectangle = !std::getenv("NEKOMOTE_TEST_GLES");
        if (rectangle)
            glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE, &before);
        renderer.upload_texture(original_id, full);
        renderer.upload_texture(cropped_id, cropped, region);
        GLint after = -1;
        if (rectangle)
        {
            glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE, &after);
            REQUIRE(after == before);
        }
        for (const auto composition :
             {ColorComposition::PremultipliedOver, ColorComposition::ProtectedStraight})
            for (const bool outside : {false, true})
            {
                INFO("original edge=" << edge << "; outside=" << outside);
                const auto vertices = quad(outside ? -.5f : 0, outside ? -.5f : 0,
                                           outside ? 1.5f : 1, outside ? 1.5f : 1);
                std::array<DrawCommand, 1> commands{{{original_id, 0, 6, BlendMode::Over}}};
                RenderFrame frame{vertices, std::span(kIndices).first(6), commands, composition};
                output.resize(64, 64);
                renderer.prepare(frame);
                renderer.resize_output(64, 64);
                renderer.render({output.framebuffer, 64, 64}, {});
                const auto reference = renderer.capture({output.framebuffer, 64, 64});
                commands[0].texture = cropped_id;
                renderer.prepare(frame);
                renderer.render({output.framebuffer, 64, 64}, {});
                const auto actual = renderer.capture({output.framebuffer, 64, 64});
                if (rectangle)
                {
                    glGetIntegerv(GL_TEXTURE_BINDING_RECTANGLE, &after);
                    REQUIRE(after == before);
                }
                unsigned max_error = 0;
                for (std::size_t i = 0; i < actual.pixels.size(); ++i)
                    max_error = std::max(
                        max_error, static_cast<unsigned>(std::abs(
                                       static_cast<int>(actual.pixels[i]) - reference.pixels[i])));
                INFO("RGBA max error=" << max_error);
                REQUIRE(max_error <= 1);
                allocation_count = 0;
                track_allocations = true;
                for (int i = 0; i < 16; ++i)
                {
                    renderer.prepare(frame);
                    renderer.render({output.framebuffer, 64, 64}, {});
                }
                track_allocations = false;
                REQUIRE(allocation_count == 0);
            }
    }
    REQUIRE(glGetError() == GL_NO_ERROR);
}
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
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 0, 0, 128}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {0, 255, 0, 255}});
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
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {200, 100, 50, 255}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {128, 64, 255, 128}});
    renderer.upload_texture(TextureId{3}, ImageRgba8{1, 1, {255, 0, 0, 128}});
    renderer.upload_texture(TextureId{4}, ImageRgba8{1, 1, {200, 100, 50, 128}});
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
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 0, 0, 128}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {0, 255, 0, 255}});
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

TEST_CASE("Small masked highlights share the group clip at reduced mask resolution", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(800, 1080);
    renderer.reserve({12, 18, 3, 2, 1, 1});
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 0, 0, 255}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {0, 0, 0, 255}});
    auto color = quad(0, 0, 1, 1);
    color[0].x = color[3].x = 447;
    color[1].x = color[2].x = 455;
    color[0].y = color[1].y = 545;
    color[2].y = color[3].y = 572;
    auto mask = quad(0, 0, 1, 1);
    mask[1].x = mask[2].x = 800;
    mask[2].y = mask[3].y = 1080;
    std::array<RenderVertex, 12> vertices{};
    std::copy(color.begin(), color.end(), vertices.begin());
    std::copy(mask.begin(), mask.end(), vertices.begin() + 4);
    for (std::size_t i = 0; i < 4; ++i)
    {
        vertices[i + 8] = mask[i];
        vertices[i + 8].color[3] = 0;
    }
    const std::array<DrawCommand, 3> commands{
        {{TextureId{1}, 12, 6, BlendMode::Over, ColorProtection::None, MaskId{1}},
         {TextureId{1}, 0, 0, BlendMode::Over, ColorProtection::None, MaskId{1}},
         {TextureId{1}, 0, 6, BlendMode::Over, ColorProtection::None, MaskId{1}}}};
    const std::array<std::uint32_t, 18> indices{0, 1, 2, 0, 2, 3,  4, 5,  6,
                                                4, 6, 7, 8, 9, 10, 8, 10, 11};
    const std::array<DrawCommand, 1> mask_commands{{{TextureId{2}, 6, 6, BlendMode::Over}}};
    const std::array<MaskDefinition, 1> masks{{{0, 1}}};
    renderer.prepare(
        {vertices, indices, commands, ColorComposition::ProtectedStraight, mask_commands, masks});
    renderer.resize_output(800, 1080);
    render_gl::View view;
    renderer.render({output.framebuffer, 800, 1080}, view);
    REQUIRE(pixel(renderer.capture({output.framebuffer, 800, 1080}), 454, 550) ==
            std::array<std::uint8_t, 4>{255, 0, 0, 255});
    view.flip_x = true;
    renderer.render({output.framebuffer, 800, 1080}, view);
    REQUIRE(pixel(renderer.capture({output.framebuffer, 800, 1080}), 345, 550) ==
            std::array<std::uint8_t, 4>{255, 0, 0, 255});
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("Opacity-zero source vertices retain full texture mask coverage", "[gpu]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.reserve({8, 12, 1, 2, 1, 1});
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 0, 0, 255}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {0, 255, 0, 255}});
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
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 0, 0, 128}});
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
    // A real incomplete GPU target must produce a renderer error, not ABI OK.
    Output incomplete;
    REQUIRE(nekomote_renderer_render(renderer, {incomplete.framebuffer, 64, 64}, view,
                                     NEKOMOTE_TARGET_CLEAR) == NEKOMOTE_ERROR_RENDERER);
    REQUIRE(std::string(nekomote_error_message()).find("render frame: GL error") !=
            std::string::npos);
    REQUIRE(glGetError() == GL_NO_ERROR);
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

TEST_CASE("UI draws to its explicit host target and reinitializes its font", "[gpu][ui]")
{
    Context context;
    render_gl::Renderer reader(glfwGetProcAddress);
    Output output, sentinel;
    output.resize(64, 64);
    sentinel.resize(64, 64);
    glClearColor(0, 1, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    render_gl::UiRenderer ui;
    nekomote::ui::Context layout;
    layout.init(&ui);
    nekomote::ui::IconState icon;
    nekomote::ui::SidebarState sidebar;
    const auto paint = [&]
    {
        ui.begin_frame();
        // Include the idle layout/recovery checks in the warmed allocation
        // measurement. Cover them with red so the host-target assertion remains
        // independent of the control styling.
        layout.toggle_icon(icon, 0);
        layout.sidebar(sidebar, 1.0f / 60.0f);
        ui.draw_solid_rect(0, 0, 64, 64, {1, 0, 0, 1});
        ui.draw_text(4, 4, "菜单 UI", 16, {1, 1, 1, 1});
        layout.end_frame();
        ui.end_frame();
    };
    for (int generation = 0; generation < 2; ++generation)
    {
        REQUIRE(ui.init(64, 64));
        REQUIRE(ui.init_font("", 36));
        REQUIRE(ui.font()->prepare_text("菜单 UI"));
        ui.set_render_target(output.framebuffer);
        // A host or resource upload may leave another FBO bound.
        glBindFramebuffer(GL_FRAMEBUFFER, sentinel.framebuffer);
        paint();
        const auto image = reader.capture({output.framebuffer, 64, 64});
        REQUIRE(pixel(image, 32, 50) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        std::size_t glyph_pixels = 0;
        for (std::size_t i = 0; i < image.pixels.size(); i += 4)
            glyph_pixels += image.pixels[i + 1] > 0;
        REQUIRE(glyph_pixels > 50);
        REQUIRE(pixel(reader.capture({sentinel.framebuffer, 64, 64}), 32, 50) ==
                std::array<std::uint8_t, 4>{0, 255, 0, 255});
        for (int frame = 0; frame < 10; ++frame)
            paint();
        allocation_count = 0;
        track_allocations = true;
        for (int frame = 0; frame < 120; ++frame)
            paint();
        track_allocations = false;
        REQUIRE(allocation_count == 0);
        ui.reset();
    }
}

#ifdef NEKOMOTE_HAS_ABI
TEST_CASE("Wallpaper ABI initializes UI after a late output size", "[gpu][abi][integration]")
{
    const auto source = std::filesystem::absolute("assets/characters/chocola/date-a");
    if (!std::filesystem::exists(source / "model.nkm"))
        SKIP("real asset integration unavailable: chocola/date-a");
    struct Assets
    {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("nekomote-ui-late-size-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        ~Assets()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } assets;
    std::filesystem::create_directories(assets.path / "characters" / "chocola");
    std::filesystem::create_directories(assets.path / "backgrounds");
    std::filesystem::create_directories(assets.path / "icons");
    std::filesystem::create_directory_symlink(source,
                                              assets.path / "characters" / "chocola" / "date-a");
    REQUIRE(
        asset_io::write_png(assets.path / "backgrounds/bg.png", ImageRgba8{1, 1, {0, 180, 0, 255}})
            .empty());
    REQUIRE(
        asset_io::write_png(assets.path / "icons/toggle.png", ImageRgba8{1, 1, {255, 0, 255, 255}})
            .empty());
    {
        std::ofstream config(assets.path / "config.json");
        config << R"({"character":"chocola/date-a","background":"bg","fit":true,"fps":120})";
    }
    Context context;
    render_gl::Renderer reader(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    std::unique_ptr<nekomote_wallpaper, decltype(&nekomote_wallpaper_destroy)> wallpaper{
        nekomote_wallpaper_create(glfwGetProcAddress), nekomote_wallpaper_destroy};
    REQUIRE(wallpaper);
    REQUIRE(nekomote_wallpaper_set_no_save(wallpaper.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_muted(wallpaper.get(), 1) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wallpaper.get(), assets.path.c_str()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_load_initial(wallpaper.get()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_fps_limit(wallpaper.get()) == 120);
    REQUIRE(nekomote_wallpaper_pointer_move(wallpaper.get(), 0, 0, UINT64_MAX) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_mouse_button(wallpaper.get(), 0, 99, 1000) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_mouse_button(wallpaper.get(), 0, NEKOMOTE_MOUSE_PRESS, UINT64_MAX) ==
            NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_output_size(wallpaper.get(), 64, 64) == NEKOMOTE_OK);
    nekomote_wallpaper_set_render_target(wallpaper.get(), output.framebuffer);
    for (int generation = 0; generation < 2; ++generation)
    {
        REQUIRE(nekomote_wallpaper_render_frame(wallpaper.get()) == NEKOMOTE_OK);
        REQUIRE(nekomote_wallpaper_render_ui(wallpaper.get(), 0) == NEKOMOTE_OK);
        const auto icon = pixel(reader.capture({output.framebuffer, 64, 64}), 34, 30);
        REQUIRE(icon[0] > 200);
        REQUIRE(icon[2] > 200);
        REQUIRE(nekomote_wallpaper_render_ui(wallpaper.get(), -1) == NEKOMOTE_ERROR_INVALID_INPUT);
        nekomote_wallpaper_reset_gl(wallpaper.get());
        if (generation == 0)
            REQUIRE(nekomote_wallpaper_load_initial(wallpaper.get()) == NEKOMOTE_OK);
    }
    // An optional file that exists but is damaged must report failure, and a
    // corrected file must permit retry without retaining partial UI resources.
    {
        std::ofstream broken(assets.path / "icons/toggle.png", std::ios::trunc);
        broken << "damaged PNG";
    }
    REQUIRE(nekomote_wallpaper_render_ui(wallpaper.get(), 0) == NEKOMOTE_ERROR_RENDERER);
    REQUIRE(
        asset_io::write_png(assets.path / "icons/toggle.png", ImageRgba8{1, 1, {255, 0, 255, 255}})
            .empty());
    REQUIRE(nekomote_wallpaper_render_ui(wallpaper.get(), 0) == NEKOMOTE_OK);
}
TEST_CASE("Wallpaper ABI probes config without creating a file and handles read-only config",
          "[gpu][abi][integration]")
{
    const auto source = std::filesystem::absolute("assets/characters/chocola/date-a");
    if (!std::filesystem::exists(source / "model.nkm"))
        SKIP("real asset integration unavailable: chocola/date-a");
    if (::geteuid() == 0)
        SKIP("read-only permission scenario requires an unprivileged user");
    struct Fixture
    {
        std::filesystem::path path =
            std::filesystem::temp_directory_path() /
            ("nekomote-config-probe-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::optional<std::string> home, xdg;
        Fixture()
        {
            if (const auto* value = std::getenv("HOME"))
                home = value;
            if (const auto* value = std::getenv("XDG_DATA_HOME"))
                xdg = value;
        }
        ~Fixture()
        {
            if (home)
                ::setenv("HOME", home->c_str(), 1);
            else
                ::unsetenv("HOME");
            if (xdg)
                ::setenv("XDG_DATA_HOME", xdg->c_str(), 1);
            else
                ::unsetenv("XDG_DATA_HOME");
            std::error_code ec;
            std::filesystem::remove_all(path, ec);
        }
    } fixture;
    const auto assets = fixture.path / "assets";
    std::filesystem::create_directories(assets / "characters/chocola");
    std::filesystem::create_directory_symlink(source, assets / "characters/chocola/date-a");
    Context context;
    render_gl::Renderer loader(glfwGetProcAddress);
    std::unique_ptr<nekomote_wallpaper, decltype(&nekomote_wallpaper_destroy)> wallpaper{
        nekomote_wallpaper_create(glfwGetProcAddress), nekomote_wallpaper_destroy};
    REQUIRE(wallpaper);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wallpaper.get(), assets.c_str()) == NEKOMOTE_OK);
    REQUIRE_FALSE(std::filesystem::exists(assets / "config.json"));
    {
        std::ofstream config(assets / "config.json");
        config << "{}";
    }
    std::filesystem::permissions(assets / "config.json", std::filesystem::perms::owner_read);
    REQUIRE(::unsetenv("HOME") == 0);
    REQUIRE(::unsetenv("XDG_DATA_HOME") == 0);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wallpaper.get(), assets.c_str()) ==
            NEKOMOTE_ERROR_NOT_FOUND);
    REQUIRE(std::string(nekomote_error_message()).find("HOME") != std::string::npos);
    std::filesystem::remove(assets / "config.json");
    std::filesystem::create_directory(assets / "config.json");
    REQUIRE(nekomote_wallpaper_set_assets_dir(wallpaper.get(), assets.c_str()) ==
            NEKOMOTE_ERROR_NOT_FOUND);
    const auto data = fixture.path / "data";
    REQUIRE(::setenv("XDG_DATA_HOME", data.c_str(), 1) == 0);
    REQUIRE(nekomote_wallpaper_set_assets_dir(wallpaper.get(), assets.c_str()) == NEKOMOTE_OK);
    REQUIRE(std::filesystem::is_directory(data / "nekomote"));
    REQUIRE_FALSE(std::filesystem::exists(data / "nekomote/config.json"));
}
#endif

TEST_CASE("Scratch masks survive ordinary draws and rebuild after another mask", "[gpu][mask]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.reserve({8, 12, 6, 2, 1, 2});
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 0, 0, 255}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {0, 255, 0, 255}});
    auto left = quad(.5f, .5f, .5f, .5f);
    left[1].x = left[2].x = 24;
    const auto full = quad(.5f, .5f, .5f, .5f);
    std::array<RenderVertex, 8> vertices{};
    std::copy(left.begin(), left.end(), vertices.begin());
    std::copy(full.begin(), full.end(), vertices.begin() + 4);
    const std::array<DrawCommand, 6> commands{
        {{TextureId{1}, 6, 6, BlendMode::Over, ColorProtection::None, MaskId{1}},
         {TextureId{2}, 6, 6, BlendMode::Over},
         {TextureId{1}, 6, 6, BlendMode::Over, ColorProtection::None, MaskId{1}},
         {TextureId{1}, 0, 0, BlendMode::Over, ColorProtection::None, MaskId{2}},
         {TextureId{1}, 6, 6, BlendMode::Over, ColorProtection::None, MaskId{2}},
         {TextureId{1}, 6, 6, BlendMode::Over, ColorProtection::None, MaskId{1}}}};
    const std::array<DrawCommand, 1> sources{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    std::array<MaskDefinition, 2> masks{{{0, 1}, {0, 0}}};
    const RenderFrame frame{vertices, std::span(kIndices).first(12),
                            commands, ColorComposition::ProtectedStraight,
                            sources,  masks};
    renderer.prepare(frame);
    renderer.resize_output(64, 64);
    for (const bool flip : {false, true})
    {
        render_gl::View view;
        view.flip_x = flip;
        renderer.render({output.framebuffer, 64, 64}, view);
        const auto image = renderer.capture({output.framebuffer, 64, 64});
        REQUIRE(pixel(image, flip ? 56 : 8, 32) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
        REQUIRE(pixel(image, flip ? 8 : 56, 32) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
    }
    masks[0].command_count = 0;
    renderer.prepare(frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    REQUIRE(pixel(renderer.capture({output.framebuffer, 64, 64}), 8, 32) ==
            std::array<std::uint8_t, 4>{0, 255, 0, 255});
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("Background texture retirement keeps transition pixels and frees settled storage",
          "[gpu][background][lifecycle]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    Output output;
    output.resize(64, 64);
    renderer.reserve({8, 12, 2, 2});
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 0, 0, 255}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {0, 0, 255, 255}});
    const auto old_quad = quad(.5f, .5f, .5f, .5f);
    const auto next_quad = quad(.5f, .5f, .5f, .5f, .5f);
    std::array<RenderVertex, 8> vertices{};
    std::copy(old_quad.begin(), old_quad.end(), vertices.begin());
    std::copy(next_quad.begin(), next_quad.end(), vertices.begin() + 4);
    const std::array<DrawCommand, 2> transition{
        {{TextureId{1}, 0, 6, BlendMode::Over}, {TextureId{2}, 6, 6, BlendMode::Over}}};
    renderer.prepare({vertices, kIndices, transition});
    renderer.render({output.framebuffer, 64, 64}, {});
    const auto fading = renderer.capture({output.framebuffer, 64, 64});
    REQUIRE(renderer.release_unused_textures() == 0);
    renderer.render({output.framebuffer, 64, 64}, {});
    REQUIRE(renderer.capture({output.framebuffer, 64, 64}).pixels == fading.pixels);
    const auto center = pixel(fading, 32, 32);
    REQUIRE(center[0] >= 127);
    REQUIRE(center[2] >= 127);

    const std::array<DrawCommand, 1> settled{{{TextureId{2}, 0, 6, BlendMode::Over}}};
    const RenderFrame settled_frame{old_quad, std::span(kIndices).first(6), settled};
    renderer.prepare(settled_frame);
    renderer.render({output.framebuffer, 64, 64}, {});
    const auto complete = renderer.capture({output.framebuffer, 64, 64});
    REQUIRE(renderer.release_unused_textures() == 1);
    renderer.render({output.framebuffer, 64, 64}, {});
    REQUIRE(renderer.capture({output.framebuffer, 64, 64}).pixels == complete.pixels);
    REQUIRE(pixel(complete, 32, 32) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
    REQUIRE_THROWS(renderer.prepare({vertices, kIndices, transition}));
    REQUIRE(renderer.release_unused_textures() == 0);
    renderer.render({output.framebuffer, 64, 64}, {});
    REQUIRE(renderer.capture({output.framebuffer, 64, 64}).pixels == complete.pixels);

    // Retired IDs and capacity are reusable across repeated background changes.
    for (int cycle = 0; cycle < 12; ++cycle)
    {
        renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {0, 255, 0, 255}});
        renderer.prepare(settled_frame);
        REQUIRE(renderer.release_unused_textures() == 1);
    }
    allocation_count = 0;
    track_allocations = true;
    std::size_t released = 0;
    for (int frame = 0; frame < 120; ++frame)
        released += renderer.release_unused_textures();
    track_allocations = false;
    REQUIRE(released == 0);
    REQUIRE(allocation_count == 0);
    renderer.prepare({});
    REQUIRE(renderer.release_unused_textures() == 1);
    REQUIRE(glGetError() == GL_NO_ERROR);
}

TEST_CASE("Texture retirement retains textures used only by mask sources",
          "[gpu][background][lifecycle][mask]")
{
    Context context;
    render_gl::Renderer renderer(glfwGetProcAddress);
    renderer.reserve({4, 6, 1, 2, 1, 1});
    renderer.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 255, 255, 255}});
    renderer.upload_texture(TextureId{2}, ImageRgba8{1, 1, {0, 0, 255, 255}});
    const auto vertices = quad(.5f, .5f, .5f, .5f);
    const std::array<DrawCommand, 1> commands{
        {{TextureId{2}, 0, 6, BlendMode::Over, ColorProtection::None, MaskId{1}}}};
    const std::array<DrawCommand, 1> sources{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    const std::array<MaskDefinition, 1> masks{{{0, 1}}};
    renderer.prepare({vertices, std::span(kIndices).first(6), commands,
                      ColorComposition::ProtectedStraight, sources, masks});
    REQUIRE(renderer.release_unused_textures() == 0);
    renderer.prepare({});
    REQUIRE(renderer.release_unused_textures() == 2);
}

TEST_CASE("Renderer peers isolate frame state and outlive their program source", "[gpu][peer]")
{
    Context context;
    auto source = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
    source->reserve({4, 6, 1, 1});
    source->upload_texture(TextureId{1}, kAtlas);
    auto peer = source->create_peer();
    peer.reserve({4, 6, 1, 1});
    const ImageRgba8 wide{3, 1, {255, 40, 0, 128, 0, 255, 90, 255, 30, 0, 255, 200}};
    peer.upload_texture(TextureId{1}, wide);
    const auto a_vertices = quad(.5f, .5f, 1.5f, 1.5f, .8f);
    const auto b_vertices = quad(.5f, .5f, 2.5f, .5f, .6f);
    const std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    const RenderFrame a{a_vertices, std::span(kIndices).first(6), commands,
                        ColorComposition::ProtectedStraight};
    const RenderFrame b{b_vertices, std::span(kIndices).first(6), commands};
    const render_gl::View a_view{3, -2, .9f, {.1f, .2f, .3f, 1}, .7f, false};
    const render_gl::View b_view{-4, 2, .8f, {.3f, .1f, .2f, 1}, .4f, true};
    Output output;
    const auto draw = [&](render_gl::Renderer& renderer, RenderFrame frame,
                          const render_gl::View& view, int width, int height)
    {
        output.resize(width, height);
        renderer.prepare(frame);
        renderer.resize_output(width, height);
        renderer.render({output.framebuffer, width, height}, view);
        return renderer.capture({output.framebuffer, width, height});
    };
    const auto expected_a = draw(*source, a, a_view, 64, 64);
    const auto expected_b = draw(peer, b, b_view, 48, 32);
    REQUIRE(draw(*source, a, a_view, 64, 64).pixels == expected_a.pixels);
    {
        auto failed = source->create_peer();
        failed.reserve({4, 6, 1, 1});
        REQUIRE_THROWS(failed.upload_texture(TextureId{1}, ImageRgba8{0, 1, {}}));
    }
    REQUIRE(draw(*source, a, a_view, 64, 64).pixels == expected_a.pixels);
    source.reset();
    REQUIRE(draw(peer, b, b_view, 48, 32).pixels == expected_b.pixels);
    REQUIRE(glGetError() == GL_NO_ERROR);
}
