#include <catch2/catch_test_macros.hpp>
#include <nekomote/asset_io/asset_file.hpp>
#include <nekomote/render_gl/renderer.hpp>
#include <nekomote/render_gl/ui_renderer.hpp>
#include <nekomote/render_vk/renderer.hpp>
#include <nekomote/render_vk/ui_renderer.hpp>

#include <glad/gl.h>
#include <nekomote/abi.h>
#ifndef GLFW_INCLUDE_NONE
#define GLFW_INCLUDE_NONE
#endif
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <new>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "vk_host.hpp"

namespace
{
thread_local bool track_allocations = false;
thread_local bool fail_allocations = false;
thread_local std::size_t allocation_count = 0;
} // namespace
// Count ordinary C++ allocations on the rendering thread; driver malloc,
// aligned allocations, other threads and diagnostic readback are excluded.
void* operator new(std::size_t size)
{
    if (fail_allocations)
        throw std::bad_alloc();
    if (track_allocations)
        ++allocation_count;
    if (void* pointer = std::malloc(size ? size : 1))
        return pointer;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size)
{
    return ::operator new(size);
}
void operator delete(void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer) noexcept
{
    std::free(pointer);
}
void operator delete(void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}
void operator delete[](void* pointer, std::size_t) noexcept
{
    std::free(pointer);
}

namespace
{
using namespace nekomote;

struct GlfwSession
{
    GlfwSession()
    {
        if (!glfwInit())
            throw std::runtime_error("Vulkan regression GLFW initialization failed");
    }
    ~GlfwSession()
    {
        glfwTerminate();
    }
};
struct DestroyWindow
{
    void operator()(GLFWwindow* window) const noexcept
    {
        if (window)
            glfwDestroyWindow(window);
    }
};
using Window = std::unique_ptr<GLFWwindow, DestroyWindow>;

// These are owned hidden windows. No events are injected into desktop windows.
// Capture is the only readback path, and is excluded from normal-frame checks.
struct Fixture
{
    GlfwSession glfw;
    Window gl_window, vk_window;
    player::VkHost host;
    std::unique_ptr<render_gl::Renderer> gl;
    std::unique_ptr<render_vk::Renderer> vk;
    GLuint framebuffer{0}, texture{0};
    int width{0}, height{0};

    explicit Fixture(player::ValidationPolicy validation = player::ValidationPolicy::IfAvailable)
    {
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
        gl_window.reset(glfwCreateWindow(64, 64, "NekoMote GL fixture", nullptr, nullptr));
        if (!gl_window)
            throw std::runtime_error("GL reference fixture context unavailable");
        glfwMakeContextCurrent(gl_window.get());
        gl = std::make_unique<render_gl::Renderer>(glfwGetProcAddress);
        glfwDefaultWindowHints();
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
        vk_window.reset(glfwCreateWindow(64, 64, "NekoMote Vulkan fixture", nullptr, nullptr));
        if (!vk_window)
            throw std::runtime_error("Vulkan fixture window unavailable");
        std::uint32_t count = 0;
        const auto extensions = glfwGetRequiredInstanceExtensions(&count);
        if (!extensions || !count)
            throw std::runtime_error("Vulkan GLFW surface extensions unavailable");
        host.create_instance({extensions, extensions + count}, validation);
        VkSurfaceKHR surface{VK_NULL_HANDLE};
        render_vk::check_vk(
            glfwCreateWindowSurface(host.instance, vk_window.get(), nullptr, &surface),
            "create Vulkan regression surface");
        host.select_physical_device(surface);
        host.create_device();
        vk = std::make_unique<render_vk::Renderer>(host.context());
        gl->reserve({16, 24, 8, 4, 8, 4});
        vk->reserve({16, 24, 8, 4, 8, 4});
        glGenFramebuffers(1, &framebuffer);
        glGenTextures(1, &texture);
    }
    ~Fixture()
    {
        // Also protects renderer resources if an assertion or recording throws.
        host.wait_noexcept();
        vk.reset();
        glfwMakeContextCurrent(gl_window.get());
        gl.reset();
        if (framebuffer)
            glDeleteFramebuffers(1, &framebuffer);
        if (texture)
            glDeleteTextures(1, &texture);
    }
    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    void upload(TextureId id, const ImageRgba8& image, TextureRegion region = {})
    {
        host.wait_ready();
        gl->upload_texture(id, image, region);
        vk->upload_texture(id, image, region);
    }
    void ensure_output(int w, int h)
    {
        if (w == width && h == height)
            return;
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        REQUIRE(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE);
        width = w;
        height = h;
    }
    render_vk::Target draw(RenderFrame frame, render_vk::View view = {}, int w = 64, int h = 64,
                           render_vk::TargetContents contents = render_vk::TargetContents::Clear)
    {
        host.wait_ready();
        ensure_output(w, h);
        gl->prepare(frame);
        gl->resize_output(w, h);
        gl->render({framebuffer, w, h},
                   {view.origin_x, view.origin_y, view.scale, view.clear, view.alpha, view.flip_x},
                   contents == render_vk::TargetContents::Clear
                       ? render_gl::TargetContents::Clear
                       : render_gl::TargetContents::Preserve);
        vk->prepare(frame);
        vk->resize_output(w, h);
        const auto target = host.begin_frame(w, h, false);
        vk->render(target, view, contents);
        return target;
    }
    std::pair<ImageRgba8, ImageRgba8> capture()
    {
        auto actual = host.capture(width, height);
        auto reference = gl->capture({framebuffer, width, height});
        REQUIRE(glGetError() == GL_NO_ERROR);
        REQUIRE(host.validation_errors == 0);
        return {std::move(reference), std::move(actual)};
    }
};

constexpr std::array<std::uint32_t, 18> kIndices{0, 1, 2, 0, 2, 3,  4, 5,  6,
                                                 4, 6, 7, 8, 9, 10, 8, 10, 11};
std::array<RenderVertex, 4> quad(float x0 = 0, float y0 = 0, float x1 = 64, float y1 = 64,
                                 float u0 = .5f, float v0 = .5f, float u1 = .5f, float v1 = .5f,
                                 float opacity = 1)
{
    return {{{x0, y0, u0, v0, {1, 1, 1, opacity}},
             {x1, y0, u1, v0, {1, 1, 1, opacity}},
             {x1, y1, u1, v1, {1, 1, 1, opacity}},
             {x0, y1, u0, v1, {1, 1, 1, opacity}}}};
}
std::array<std::uint8_t, 4> pixel(const ImageRgba8& image, int x, int y)
{
    const auto i = (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
                    static_cast<std::size_t>(x)) *
                   4;
    return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}

// Both capture APIs return straight RGBA. Compare the observable black/white
// composites plus alpha, so low-alpha unassociation rounding does not dominate.
void require_same_picture(const ImageRgba8& reference, const ImageRgba8& actual,
                          unsigned tolerance = 2)
{
    REQUIRE(reference.width == actual.width);
    REQUIRE(reference.height == actual.height);
    REQUIRE(reference.pixels.size() == actual.pixels.size());
    std::array<unsigned, 4> max_error{};
    std::size_t differing = 0;
    for (std::size_t i = 0; i < actual.pixels.size(); i += 4)
    {
        const auto ar = reference.pixels[i + 3], av = actual.pixels[i + 3];
        const auto alpha_error = static_cast<unsigned>(std::abs(static_cast<int>(ar) - av));
        max_error[3] = std::max(max_error[3], alpha_error);
        bool bad = alpha_error > tolerance;
        for (std::size_t c = 0; c < 3; ++c)
            for (unsigned background : {0U, 255U})
            {
                const auto a = (static_cast<unsigned>(reference.pixels[i + c]) * ar +
                                background * (255U - ar) + 127U) /
                               255U;
                const auto b = (static_cast<unsigned>(actual.pixels[i + c]) * av +
                                background * (255U - av) + 127U) /
                               255U;
                const auto error =
                    static_cast<unsigned>(std::abs(static_cast<int>(a) - static_cast<int>(b)));
                max_error[c] = std::max(max_error[c], error);
                bad = bad || error > tolerance;
            }
        differing += bad ? 1U : 0U;
    }
    if (differing)
        if (const char* output_dir = std::getenv("NEKOMOTE_VK_EVIDENCE_DIR"))
        {
            std::filesystem::create_directories(output_dir);
            const auto save = [&](const char* name, const ImageRgba8& image)
            {
                std::ofstream file(std::filesystem::path(output_dir) / name, std::ios::binary);
                file << "P6\n" << image.width << ' ' << image.height << "\n255\n";
                for (std::size_t i = 0; i < image.pixels.size(); i += 4)
                    file.write(reinterpret_cast<const char*>(image.pixels.data() + i), 3);
            };
            save("reference.ppm", reference);
            save("actual.ppm", actual);
        }
    INFO("composed max RGBA error: " << max_error[0] << ',' << max_error[1] << ',' << max_error[2]
                                     << ',' << max_error[3]
                                     << "; pixels above tolerance: " << differing);
    REQUIRE(differing == 0);
}
void require_same_picture(const std::pair<ImageRgba8, ImageRgba8>& pair)
{
    require_same_picture(pair.first, pair.second);
}
} // namespace

TEST_CASE("Vulkan target clear preserve and top-left atlas coordinates match OpenGL",
          "[gpu][vulkan]")
{
    Fixture f;
    f.upload(TextureId{1},
             {2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 0}});
    f.draw({{}, {}, {}}, {0, 0, 1, {0, 0, 1, 1}});
    require_same_picture(f.capture());
    auto vertices = quad(8, 12, 56, 52, 0, 0, 1, 1);
    const std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    RenderFrame frame{vertices, std::span(kIndices).first(6), commands};
    f.draw(frame, {}, 64, 64, render_vk::TargetContents::Preserve);
    auto images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 2, 2) == std::array<std::uint8_t, 4>{0, 0, 255, 255});
    auto tl = pixel(images.second, 12, 16), bl = pixel(images.second, 12, 48);
    REQUIRE(tl[0] > 240);
    REQUIRE(bl[2] > 240);
    render_vk::View flipped;
    flipped.flip_x = true;
    f.draw(frame, flipped);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 2, 2)[3] == 0);
    REQUIRE(pixel(images.second, 52, 16)[0] > 240);
}

TEST_CASE("Vulkan transparent order protection multiply and whole-model fade match OpenGL",
          "[gpu][vulkan]")
{
    Fixture f;
    f.upload(TextureId{1}, {1, 1, {255, 0, 0, 128}});
    f.upload(TextureId{2}, {1, 1, {0, 255, 0, 255}});
    f.upload(TextureId{3}, {1, 1, {128, 64, 255, 128}});
    std::array<RenderVertex, 8> vertices{};
    const auto first = quad(), second = quad(0, 0, 64, 64, .5f, .5f, .5f, .5f, .5f);
    std::copy(first.begin(), first.end(), vertices.begin());
    std::copy(second.begin(), second.end(), vertices.begin() + 4);
    std::array<DrawCommand, 2> commands{
        {{TextureId{1}, 0, 6, BlendMode::Over}, {TextureId{2}, 6, 6, BlendMode::Over}}};
    RenderFrame frame{vertices, std::span(kIndices).first(12), commands};
    f.draw(frame);
    auto images = f.capture();
    require_same_picture(images);
    const auto over = pixel(images.second, 32, 32);
    REQUIRE(over[1] > over[0]);
    std::swap(commands[0], commands[1]);
    f.draw(frame);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 32, 32)[0] > pixel(images.second, 32, 32)[1]);
    std::swap(commands[0], commands[1]);
    frame.composition = ColorComposition::ProtectedStraight;
    for (auto& command : commands)
        command.protection = ColorProtection::PreserveTranslucent;
    f.draw(frame);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 32, 32)[1] >= 190);
    commands[1] = {TextureId{3}, 6, 6, BlendMode::Multiply};
    f.draw(frame);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 32, 32)[3] == 128);
    render_vk::View faded;
    faded.alpha = .5f;
    f.draw(frame, faded);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 32, 32)[3] >= 63);
    REQUIRE(pixel(images.second, 32, 32)[3] <= 64);
}

TEST_CASE("Vulkan five-bit mask union reuses descriptors beyond original exhaustion point",
          "[gpu][vulkan]")
{
    Fixture f;
    f.upload(TextureId{1}, {1, 1, {255, 0, 0, 128}});
    f.upload(TextureId{2}, {1, 1, {0, 255, 0, 255}});
    auto vertices = quad();
    for (auto& vertex : vertices)
        vertex.color[3] = 1;
    std::array<DrawCommand, 1> commands{
        {{TextureId{2}, 0, 6, BlendMode::Over, ColorProtection::PreserveTranslucent, MaskId{1}}}};
    std::array<DrawCommand, 2> sources{
        {{TextureId{1}, 0, 6, BlendMode::Over}, {TextureId{1}, 0, 6, BlendMode::Over}}};
    std::array<MaskDefinition, 1> masks{{{0, 2}}};
    RenderFrame frame{vertices, std::span(kIndices).first(6),
                      commands, ColorComposition::ProtectedStraight,
                      sources,  masks};
    f.draw(frame);
    auto images = f.capture();
    require_same_picture(images);
    const auto coverage = pixel(images.second, 32, 32);
    REQUIRE(coverage[1] == 255);
    REQUIRE(coverage[3] >= 197);
    REQUIRE(coverage[3] <= 198); // 16/31 union 16/31 rounds to 24/31 stored green.
    masks[0].command_count = 0;
    f.draw(frame);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 32, 32)[3] == 0);
    masks[0].command_count = 2;
    for (int i = 0; i < 400; ++i)
    {
        f.draw(frame);
        f.host.present(); // Submit offscreen without screenshot/readback.
    }
    f.host.wait_ready();
    require_same_picture(f.capture());
    for (auto& vertex : vertices)
    {
        vertex.x *= 2;
        vertex.y *= 2;
    }
    f.draw(frame, {}, 128, 128);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 64, 64)[3] >= 197);
    REQUIRE(pixel(images.second, 64, 64)[3] <= 198);
    REQUIRE(f.host.validation_errors == 0);
}

TEST_CASE(
    "Vulkan reduced-resolution masks retain shared highlight clip and source opacity semantics",
    "[gpu][vulkan]")
{
    Fixture f;
    f.upload(TextureId{1}, {1, 1, {255, 0, 0, 255}});
    f.upload(TextureId{2}, {1, 1, {0, 0, 0, 255}});
    const auto highlight = quad(447, 545, 455, 572);
    const auto source = quad(0, 0, 800, 1080);
    auto invisible_group = source;
    for (auto& vertex : invisible_group)
        vertex.color[3] = 0;
    std::array<RenderVertex, 12> vertices{};
    std::copy(highlight.begin(), highlight.end(), vertices.begin());
    std::copy(source.begin(), source.end(), vertices.begin() + 4);
    std::copy(invisible_group.begin(), invisible_group.end(), vertices.begin() + 8);
    const std::array<DrawCommand, 3> commands{
        {{TextureId{1}, 12, 6, BlendMode::Over, ColorProtection::None, MaskId{1}},
         {TextureId{1}, 0, 0, BlendMode::Over, ColorProtection::None, MaskId{1}},
         {TextureId{1}, 0, 6, BlendMode::Over, ColorProtection::None, MaskId{1}}}};
    const std::array<DrawCommand, 1> sources{{{TextureId{2}, 6, 6, BlendMode::Over}}};
    const std::array<MaskDefinition, 1> masks{{{0, 1}}};
    RenderFrame frame{vertices, kIndices, commands, ColorComposition::ProtectedStraight,
                      sources,  masks};
    f.draw(frame, {}, 800, 1080);
    auto images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 454, 550) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    render_vk::View mirrored;
    mirrored.flip_x = true;
    f.draw(frame, mirrored, 800, 1080);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 345, 550) == std::array<std::uint8_t, 4>{255, 0, 0, 255});
    for (std::size_t i = 4; i < 8; ++i)
        vertices[i].color[3] = 0;
    f.draw(frame, {}, 800, 1080);
    images = f.capture();
    require_same_picture(images);
    REQUIRE(pixel(images.second, 454, 550)[3] == 255);
}

TEST_CASE("Vulkan UI preserves painter order glyphs icon UVs and nested clips across resets",
          "[gpu][vulkan][ui]")
{
    Fixture f;
    render_gl::UiRenderer gl_ui;
    render_vk::UiRenderer vk_ui;
    player::VkCompletionGuard guard{&f.host};
    const ImageRgba8 icon{
        2, 2, {255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 255}};
    for (int generation = 0; generation < 2; ++generation)
    {
        REQUIRE(gl_ui.init(96, 96));
        REQUIRE(vk_ui.init(f.host.context(), 96, 96));
        REQUIRE(gl_ui.init_font("", 16));
        REQUIRE(vk_ui.init_font("", 16));
        REQUIRE(gl_ui.font()->prepare_text("UI 123"));
        REQUIRE(vk_ui.font()->prepare_text("UI 123"));
        const auto gl_icon = gl_ui.upload_icon(icon), vk_icon = vk_ui.upload_icon(icon);
        REQUIRE(gl_icon != 0);
        REQUIRE(vk_icon != 0);
        const auto target = f.draw({{}, {}, {}}, {0, 0, 1, {0, 0, 0, 1}}, 96, 96);
        gl_ui.set_render_target(f.framebuffer);
        // See below: UI records into the same host target as the empty background.
        vk_ui.set_render_target(target);
        const auto paint = [](ui::Painter& painter, std::uint32_t image)
        {
            painter.begin_frame();
            painter.draw_solid_rect(4, 4, 44, 44, {1, 0, 0, .5f});
            painter.draw_icon(12, 12, 28, 28, image, {1, 1, 1, 1});
            painter.draw_solid_rect(20, 20, 24, 24, {0, 1, 0, .5f});
            painter.push_clip_rect(48, 4, 44, 44);
            painter.push_clip_rect(52, 8, 28, 28);
            painter.draw_solid_rect(40, 0, 52, 52, {0, 0, 1, 1});
            painter.pop_clip_rect();
            painter.pop_clip_rect();
            painter.draw_text(4, 60, "UI 123", 16, {1, 1, 1, 1});
            painter.draw_rounded_rect(70, 60, 20, 20, 5, {1, 1, 0, 1}, {0, 0, 1, 1}, 1);
            painter.draw_circle(84, 50, 4, {1, 0, 1, 1}, {0, 1, 1, 1}, 1);
            painter.set_global_alpha(.5f);
            painter.draw_arrow(50, 50, 8, true, {1, 1, 1, 1});
            painter.end_frame();
        };
        paint(gl_ui, gl_icon);
        paint(vk_ui, vk_icon);
        REQUIRE(gl_ui.global_alpha() == 1.0f);
        REQUIRE(vk_ui.global_alpha() == 1.0f);
        vk_ui.begin_frame();
        vk_ui.set_global_alpha(.25f);
        vk_ui.end_frame();
        REQUIRE(vk_ui.global_alpha() == 1.0f);
        auto images = f.capture();
        require_same_picture(images);
        REQUIRE(pixel(images.second, 54, 10)[2] == 255);
        REQUIRE(pixel(images.second, 50, 10)[2] == 0);
        REQUIRE(pixel(images.second, 10, 10)[0] >= 127);
        std::size_t glyph_pixels = 0;
        for (int y = 58; y < 90; ++y)
            for (int x = 4; x < 64; ++x)
                glyph_pixels += pixel(images.second, x, y)[0] > 32 ? 1U : 0U;
        REQUIRE(glyph_pixels > 50);
        f.host.wait_ready();
        gl_ui.destroy_icon(gl_icon);
        vk_ui.destroy_icon(vk_icon);
        gl_ui.reset();
        vk_ui.reset();
    }
}

TEST_CASE("Vulkan warmed model and UI frame paths reuse CPU allocations", "[gpu][vulkan]")
{
    // Validation layers allocate independently. Count the renderer/host path
    // with validation explicitly disabled, separately from correctness cases.
    Fixture f(player::ValidationPolicy::Disabled);
    f.upload(TextureId{1}, {1, 1, {255, 255, 255, 128}});
    const auto vertices = quad();
    const std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    const RenderFrame frame{vertices, std::span(kIndices).first(6), commands,
                            ColorComposition::ProtectedStraight};
    render_vk::UiRenderer ui;
    struct CompletionGuard
    {
        player::VkHost& host;
        ~CompletionGuard()
        {
            host.wait_noexcept();
        }
    } completion{f.host};
    REQUIRE(ui.init(f.host.context(), 64, 64));
    REQUIRE(ui.init_font("", 24));
    REQUIRE(ui.font()->prepare_text("Frame 123"));
    const auto paint = [&]
    {
        f.host.wait_ready();
        f.vk->prepare(frame);
        f.vk->resize_output(64, 64);
        const auto target = f.host.begin_frame(64, 64, false);
        f.vk->render(target, {});
        ui.set_render_target(target);
        ui.begin_frame();
        ui.draw_text(0, 0, "Frame 123", 14, {1, 1, 1, 1});
        ui.push_clip_rect(0, 0, 64, 64);
        ui.draw_circle(32, 32, 12, {1, 0, 0, .5f}, {0, 0, 0, 0}, 0);
        ui.pop_clip_rect();
        ui.end_frame();
        f.host.present();
    };
    for (int i = 0; i < 4; ++i)
        paint();
    struct AllocationScope
    {
        AllocationScope()
        {
            allocation_count = 0;
            track_allocations = true;
        }
        ~AllocationScope()
        {
            track_allocations = false;
        }
    };
    {
        AllocationScope count;
        for (int i = 0; i < 120; ++i)
            paint();
        f.host.wait_ready();
    }
    REQUIRE(allocation_count == 0);
    const auto image = f.host.capture(64, 64);
    REQUIRE(pixel(image, 32, 32)[3] > 0);
}

#if defined(NEKOMOTE_HAS_ABI)
TEST_CASE("Vulkan wallpaper ABI validates borrowed targets and retains model after failed load",
          "[gpu][vulkan][abi]")
{
    Fixture f;
    const auto source = std::filesystem::path(__FILE__).parent_path().parent_path() / "fixtures";
    const auto path = std::filesystem::temp_directory_path() /
                      ("nekomote-vk-abi-" +
                       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct RemoveBundle
    {
        std::filesystem::path path;
        ~RemoveBundle()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    } cleanup{path};
    REQUIRE(std::filesystem::create_directory(path));
    auto imported =
        asset_io::import_emote({source / "minimal.json", source / "minimal.resx.json", source, {}});
    REQUIRE(imported.ok);
    for (auto& part : imported.asset.parts)
        for (auto& layer : part.layers)
            layer.inherit_mask = 0x20007fc;
    imported.asset.base_group = "body_parts";
    imported.asset.base_motion = "torso";
    model::Timeline idle;
    idle.label = "idle";
    idle.loop_begin = 0;
    idle.loop_end = 30;
    idle.tracks.push_back(
        {"move_UD",
         {{0, 2, {model::FieldState::Value, 0.f}, {model::FieldState::Value, 0.f}},
          {30, 0, {}, {}}}});
    imported.asset.timelines.push_back(std::move(idle));
    REQUIRE(asset_io::write_bundle(imported, source, path / "model").empty());
    const auto session = path / "session.nks.json";
    {
        std::ofstream out(session);
        out << R"({"schema":"nekomote.session","version":1,"session":{
            "model":"synthetic","idle_timeline":"idle","talk_max":5,
            "interaction":{"eye":null,"regions":{"bust":null,"eye":null,
            "face":null,"head":null,"pant":null}},"reactions":[]}})";
    }
    auto ctx = f.host.context();
    nekomote_vk_device_context cctx{
        ctx.instance,       ctx.physical_device,       ctx.device,
        ctx.graphics_queue, ctx.graphics_queue_family, static_cast<int32_t>(ctx.color_format)};
    auto bad_ctx = cctx;
    bad_ctx.color_format = 50; // Unsupported SRGB attachment, with real native handles.
    REQUIRE(nekomote_wallpaper_create_vk(bad_ctx) == nullptr);
    REQUIRE(nekomote_last_error() == NEKOMOTE_ERROR_INVALID_INPUT);
    using Owner = std::unique_ptr<nekomote_wallpaper, decltype(&nekomote_wallpaper_destroy)>;
    Owner wallpaper(nekomote_wallpaper_create_vk(cctx), nekomote_wallpaper_destroy);
    Owner reference(
        nekomote_wallpaper_create(reinterpret_cast<nekomote_gl_loader>(glfwGetProcAddress)),
        nekomote_wallpaper_destroy);
    // Unwind any assertion only after pending GPU work has finished, before owners.
    struct WaitBeforeOwners
    {
        player::VkHost& host;
        ~WaitBeforeOwners()
        {
            host.wait_noexcept();
        }
    } completion{f.host};
    REQUIRE(wallpaper);
    REQUIRE(reference);
    const auto model_path = path / "model/model.nkm";
    for (auto* wp : {wallpaper.get(), reference.get()})
    {
        REQUIRE(nekomote_wallpaper_set_muted(wp, 1) == NEKOMOTE_OK);
        REQUIRE(nekomote_wallpaper_set_output_size(wp, 64, 64) == NEKOMOTE_OK);
        REQUIRE(nekomote_wallpaper_load_model(wp, model_path.c_str(), session.c_str()) ==
                NEKOMOTE_OK);
    }
    f.ensure_output(64, 64);
    nekomote_wallpaper_set_render_target(reference.get(), f.framebuffer);
    REQUIRE(nekomote_wallpaper_render_frame(reference.get()) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_render_frame(wallpaper.get()) == NEKOMOTE_ERROR_RENDERER);
    const auto target = f.host.begin_frame(64, 64, false);
    const nekomote_vk_target valid{target.commands, target.framebuffer, 64, 64};
    for (const nekomote_vk_target invalid :
         {nekomote_vk_target{nullptr, target.framebuffer, 64, 64},
          nekomote_vk_target{target.commands, nullptr, 64, 64},
          nekomote_vk_target{target.commands, target.framebuffer, 0, 64},
          nekomote_vk_target{target.commands, target.framebuffer, 32, 64}})
        REQUIRE(nekomote_wallpaper_set_vk_target(wallpaper.get(), invalid) ==
                NEKOMOTE_ERROR_INVALID_INPUT);
    REQUIRE(nekomote_wallpaper_set_vk_target(reference.get(), valid) == NEKOMOTE_ERROR_UNSUPPORTED);
    REQUIRE(nekomote_wallpaper_set_vk_target(wallpaper.get(), valid) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_advance(wallpaper.get(), 0) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_render_frame(wallpaper.get()) == NEKOMOTE_ERROR_RENDERER);
    REQUIRE(nekomote_wallpaper_set_vk_target(wallpaper.get(), valid) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_render_frame(wallpaper.get()) == NEKOMOTE_OK);
    auto actual = f.host.capture(64, 64);
    auto expected = f.gl->capture({f.framebuffer, 64, 64});
    require_same_picture(expected, actual);
    // Capture completed this frame; failure now cannot destroy active resources.
    fail_allocations = true;
    const auto load_error =
        nekomote_wallpaper_load_model(wallpaper.get(), model_path.c_str(), session.c_str());
    fail_allocations = false;
    REQUIRE(load_error == NEKOMOTE_ERROR_OUT_OF_MEMORY);
    const auto next = f.host.begin_frame(64, 64, false);
    REQUIRE(nekomote_wallpaper_set_vk_target(
                wallpaper.get(), {next.commands, next.framebuffer, 64, 64}) == NEKOMOTE_OK);
    REQUIRE(nekomote_wallpaper_render_frame(wallpaper.get()) == NEKOMOTE_OK);
    require_same_picture(actual, f.host.capture(64, 64));
    nekomote_wallpaper_reset_gl(wallpaper.get());
    REQUIRE(nekomote_wallpaper_render_frame(wallpaper.get()) == NEKOMOTE_ERROR_RENDERER);
    REQUIRE(f.host.validation_errors == 0);
}

#endif

TEST_CASE("Cropped atlases preserve original sampling and mask UVs on GL and Vulkan",
          "[gpu][vulkan][crop]")
{
    Fixture f;
    ImageRgba8 cropped{7, 5, std::vector<std::uint8_t>(7 * 5 * 4, 0)};
    for (int y = 1; y < 4; ++y)
        for (int x = 1; x < 6; ++x)
        {
            const auto i = static_cast<std::size_t>(y * 7 + x) * 4;
            cropped.pixels[i] = static_cast<std::uint8_t>(x * 40);
            cropped.pixels[i + 1] = static_cast<std::uint8_t>(y * 60);
            cropped.pixels[i + 2] = 180;
            cropped.pixels[i + 3] = x == 1 ? 0 : (x == 2 ? 1 : 160);
        }
    const TextureRegion region{13, 11, 3, 2};
    ImageRgba8 full{13, 11, std::vector<std::uint8_t>(13 * 11 * 4, 0)};
    for (int y = 0; y < cropped.height; ++y)
        std::copy_n(cropped.pixels.data() + y * cropped.width * 4, cropped.width * 4,
                    full.pixels.data() + ((y + region.top) * full.width + region.left) * 4);
    f.upload(TextureId{1}, full);
    f.upload(TextureId{2}, cropped, region);
    // Reach crop validation using real devices, valid pixels and unused IDs.
    for (const auto invalid : {TextureRegion{13, 11, -1, 2}, TextureRegion{13, 11, 8, 2},
                               TextureRegion{13, 0, 0, 0}, TextureRegion{0, 0, 1, 0}})
    {
        REQUIRE_THROWS(f.gl->upload_texture(TextureId{3}, cropped, invalid));
        REQUIRE_THROWS(f.vk->upload_texture(TextureId{3}, cropped, invalid));
    }
    f.upload(TextureId{3}, {1, 1, {255, 255, 255, 255}});
    for (const auto composition :
         {ColorComposition::PremultipliedOver, ColorComposition::ProtectedStraight})
        for (const bool masked : {false, true})
            for (const bool outside : {false, true})
            {
                if (masked && composition == ColorComposition::PremultipliedOver)
                    continue;
                INFO("composition=" << static_cast<int>(composition) << "; mask=" << masked
                                    << "; outside=" << outside);
                auto vertices = quad(0, 0, 64, 64, outside ? -.5f : 0, outside ? -.5f : 0,
                                     outside ? 1.5f : 1, outside ? 1.5f : 1);
                std::array<DrawCommand, 1> commands{
                    {{TextureId{1}, 0, 6, BlendMode::Over,
                      composition == ColorComposition::ProtectedStraight
                          ? ColorProtection::PreserveTranslucent
                          : ColorProtection::None,
                      masked ? MaskId{1} : MaskId{}}}};
                std::array<DrawCommand, 1> sources{{{TextureId{1}, 0, 6, BlendMode::Over}}};
                const std::array<MaskDefinition, 1> masks{{{0, 1}}};
                RenderFrame frame{vertices,
                                  std::span(kIndices).first(6),
                                  commands,
                                  composition,
                                  masked ? std::span<const DrawCommand>(sources)
                                         : std::span<const DrawCommand>{},
                                  masked ? std::span<const MaskDefinition>(masks)
                                         : std::span<const MaskDefinition>{}};
                f.draw(frame);
                const auto reference = f.capture();
                commands[0].texture = TextureId{2};
                sources[0].texture = TextureId{2};
                f.draw(frame);
                const auto actual = f.capture();
                require_same_picture(reference.first, actual.first, 1);
                require_same_picture(reference.second, actual.second, 1);
                require_same_picture(actual.first, actual.second, 1);
                // Compare cropped mask + identity texture against the original
                // mask + identity texture, so stale transforms cannot pass.
                if (masked)
                {
                    commands[0].texture = TextureId{3};
                    f.draw(frame);
                    const auto mixed = f.capture();
                    sources[0].texture = TextureId{1};
                    f.draw(frame);
                    const auto mixed_reference = f.capture();
                    require_same_picture(mixed_reference.first, mixed.first, 1);
                    require_same_picture(mixed_reference.second, mixed.second, 1);
                }
            }
    REQUIRE(f.host.validation_errors == 0);
}

TEST_CASE("Scratch masks survive ordinary draws and rebuild after another mask", "[gpu][mask]")
{
    Fixture f;
    f.upload(TextureId{1}, {1, 1, {255, 0, 0, 255}});
    f.upload(TextureId{2}, {1, 1, {0, 255, 0, 255}});
    const auto left = quad(0, 0, 24, 64);
    const auto full = quad();
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
    for (const bool flip : {false, true})
    {
        render_vk::View view;
        view.flip_x = flip;
        f.draw(frame, view);
        const auto images = f.capture();
        require_same_picture(images);
        REQUIRE(pixel(images.second, flip ? 56 : 8, 32) ==
                std::array<std::uint8_t, 4>{255, 0, 0, 255});
        REQUIRE(pixel(images.second, flip ? 8 : 56, 32) ==
                std::array<std::uint8_t, 4>{0, 255, 0, 255});
    }
    // A new prepare may change mask contents while retaining the same IDs.
    masks[0].command_count = 0;
    f.draw(frame);
    REQUIRE(pixel(f.capture().second, 8, 32) == std::array<std::uint8_t, 4>{0, 255, 0, 255});
}

TEST_CASE("Vulkan background texture retirement preserves fades and reuses descriptors",
          "[gpu][vulkan][background][lifecycle]")
{
    Fixture f;
    f.upload(TextureId{1}, {1, 1, {255, 0, 0, 255}});
    f.upload(TextureId{2}, {1, 1, {0, 0, 255, 255}});
    const auto old_quad = quad();
    const auto next_quad = quad(0, 0, 64, 64, .5f, .5f, .5f, .5f, .5f);
    std::array<RenderVertex, 8> vertices{};
    std::copy(old_quad.begin(), old_quad.end(), vertices.begin());
    std::copy(next_quad.begin(), next_quad.end(), vertices.begin() + 4);
    const std::array<DrawCommand, 2> transition{
        {{TextureId{1}, 0, 6, BlendMode::Over}, {TextureId{2}, 6, 6, BlendMode::Over}}};
    f.draw({vertices, std::span(kIndices).first(12), transition});
    const auto fading = f.capture();
    require_same_picture(fading);
    f.host.wait_ready();
    REQUIRE(f.gl->release_unused_textures() == 0);
    REQUIRE(f.vk->release_unused_textures() == 0);
    const auto center = pixel(fading.second, 32, 32);
    REQUIRE(center[0] >= 127);
    REQUIRE(center[2] >= 127);

    const std::array<DrawCommand, 1> settled{{{TextureId{2}, 0, 6, BlendMode::Over}}};
    const RenderFrame settled_frame{old_quad, std::span(kIndices).first(6), settled};
    f.draw(settled_frame);
    const auto complete = f.capture();
    require_same_picture(complete);
    f.host.wait_ready();
    REQUIRE(f.gl->release_unused_textures() == 1);
    REQUIRE(f.vk->release_unused_textures() == 1);
    REQUIRE_THROWS(f.gl->prepare({vertices, std::span(kIndices).first(12), transition}));
    REQUIRE_THROWS(f.vk->prepare({vertices, std::span(kIndices).first(12), transition}));
    REQUIRE(f.gl->release_unused_textures() == 0);
    REQUIRE(f.vk->release_unused_textures() == 0);
    f.draw(settled_frame);
    const auto after = f.capture();
    REQUIRE(after.first.pixels == complete.first.pixels);
    REQUIRE(after.second.pixels == complete.second.pixels);
    REQUIRE(pixel(after.second, 32, 32) == std::array<std::uint8_t, 4>{0, 0, 255, 255});

    // More cycles than the pool's total descriptor capacity expose unreclaimed sets.
    for (int cycle = 0; cycle < 12; ++cycle)
    {
        f.upload(TextureId{1}, {1, 1, {0, 255, 0, 255}});
        f.gl->prepare(settled_frame);
        f.vk->prepare(settled_frame);
        REQUIRE(f.gl->release_unused_textures() == 1);
        REQUIRE(f.vk->release_unused_textures() == 1);
    }
    allocation_count = 0;
    track_allocations = true;
    std::size_t released = 0;
    for (int frame = 0; frame < 120; ++frame)
        released += f.vk->release_unused_textures();
    track_allocations = false;
    REQUIRE(released == 0);
    REQUIRE(allocation_count == 0);
    f.gl->prepare({});
    f.vk->prepare({});
    REQUIRE(f.gl->release_unused_textures() == 1);
    REQUIRE(f.vk->release_unused_textures() == 1);
    REQUIRE(f.host.validation_errors == 0);
}

TEST_CASE("Vulkan texture retirement retains textures referenced only by masks",
          "[gpu][vulkan][background][lifecycle][mask]")
{
    Fixture f;
    f.upload(TextureId{1}, {1, 1, {255, 255, 255, 255}});
    f.upload(TextureId{2}, {1, 1, {0, 0, 255, 255}});
    const auto vertices = quad();
    const std::array<DrawCommand, 1> commands{
        {{TextureId{2}, 0, 6, BlendMode::Over, ColorProtection::None, MaskId{1}}}};
    const std::array<DrawCommand, 1> sources{{{TextureId{1}, 0, 6, BlendMode::Over}}};
    const std::array<MaskDefinition, 1> masks{{{0, 1}}};
    f.draw({vertices, std::span(kIndices).first(6), commands, ColorComposition::ProtectedStraight,
            sources, masks});
    require_same_picture(f.capture());
    f.host.wait_ready();
    REQUIRE(f.gl->release_unused_textures() == 0);
    REQUIRE(f.vk->release_unused_textures() == 0);
    f.gl->prepare({});
    f.vk->prepare({});
    REQUIRE(f.gl->release_unused_textures() == 2);
    REQUIRE(f.vk->release_unused_textures() == 2);
    REQUIRE(f.host.validation_errors == 0);
}
