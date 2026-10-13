#pragma once

#include <nekomote/image.hpp>
#include <nekomote/render_frame.hpp>

#include <array>
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
    float alpha{1};                         // global alpha multiplier for all drawn fragments
    bool flip_x{false};                     // mirror model horizontally about output center
};
enum class TargetContents
{
    Clear,
    Preserve
};
struct Capacity
{
    std::size_t vertices{0}, indices{0}, commands{0}, textures{0};
    std::size_t mask_commands{0}, masks{0};
};
class Renderer
{
  public:
    // Accepts desktop OpenGL 3.3 core or OpenGL ES 3.0 (e.g. Qt Wayland).
    explicit Renderer(GlLoader loader);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    // Reserves capacity once; prepare rejects larger frames instead of allocating.
    void reserve(Capacity capacity);
    void upload_texture(TextureId id, ImageViewRgba8 image, TextureRegion region = {});
    void prepare(RenderFrame frame);
    // Explicit loading/resize boundary. ProtectedStraight needs a matching
    // RGBA8 intermediate. Repeated unchanged dimensions retain GPU storage.
    void resize_output(int width, int height);
    // Preserve composites over existing target pixels. The protected model
    // intermediate is always independently cleared, regardless of this choice.
    void render(Target target, const View& view, TargetContents contents = TargetContents::Clear);
    // Diagnostic GPU readback only; omitted from render/benchmark paths.
    ImageRgba8 capture(Target target);
    std::string diagnostics() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::render_gl
