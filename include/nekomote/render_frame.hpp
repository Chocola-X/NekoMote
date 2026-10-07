#pragma once

#include <nekomote/types.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace nekomote
{
// Pixel/model XY with Y down; top-left normalized UV; straight colour/opacity.
struct RenderVertex
{
    float x{0}, y{0}, u{0}, v{0};
    std::array<float, 4> color{1, 1, 1, 1};
};
enum class BlendMode
{
    Over,
    Add,
    // Opacity-weighted RGB multiply preserving destination alpha. Supported in
    // ProtectedStraight with no color protection; alpha is composited once at
    // the whole-model boundary. Source evidence: docs/v6-visual-report.md.
    Multiply,
    Screen
};
enum class ColorProtection
{
    None,
    PreserveTranslucent
};
enum class ColorComposition
{
    PremultipliedOver,
    ProtectedStraight
};
struct MaskTag
{
};
using MaskId = Id<MaskTag>;
struct DrawCommand
{
    TextureId texture;
    std::uint32_t first_index{0}, index_count{0};
    BlendMode blend{BlendMode::Over};
    ColorProtection protection{ColorProtection::None};
    MaskId mask{};
};
// Union of sampled alpha, cleared to zero for every application. Mask commands
// use texture alpha independently of vertex colour/opacity and share the colour
// geometry buffers. IDs are one-based indices
// into RenderFrame::masks; zero means no mask. Current raster contract: five-bit
// stored coverage, bilinear sampling, floor-power-of-two output dimensions, and
// a two-output-pixel border for intersected source/content-group clipping.
// Commands sharing an ID share a content clip. A runtime referenced motion
// owns that clip; separate motions retain separate IDs even with equal sources.
struct MaskDefinition
{
    std::uint32_t first_command{0}, command_count{0};
};
// Non-owning views; caller keeps storage alive until prepare returns.
struct RenderFrame
{
    std::span<const RenderVertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const DrawCommand> commands;
    ColorComposition composition{ColorComposition::PremultipliedOver};
    std::span<const DrawCommand> mask_commands{};
    std::span<const MaskDefinition> masks{};
};
} // namespace nekomote
