#pragma once

#include <nekomote/image.hpp>
#include <nekomote/render_frame.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace nekomote::render_gl
{
using GlProc = void (*)();
using GlLoader = GlProc (*)(const char*);
// Native framebuffer handle is restricted to this host/backend boundary.
// Renderer owns no window/context. Every method and destruction require the
// same current GL context on its owning graphics thread. Host restores state
// when sharing a context; this backend owns the state during render calls.
struct Target
{
    std::uint32_t framebuffer{0};
    int width{0}, height{0};
};
struct View
{
    float origin_x{0}, origin_y{0}, scale{1};
    std::array<float, 4> clear{0, 0, 0, 0}; // premultiplied target clear
};
class Renderer
{
  public:
    explicit Renderer(GlLoader loader);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // Reserves capacity once; prepare rejects larger frames instead of allocating.
    void reserve(std::size_t vertices, std::size_t indices, std::size_t commands,
                 std::size_t textures);
    void upload_texture(TextureId id, const ImageRgba8& image);
    void prepare(RenderFrame frame);
    void render(Target target, const View& view);
    // Diagnostic GPU readback only; omitted from render/benchmark paths.
    ImageRgba8 capture(Target target);
    std::string diagnostics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::render_gl
