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
    Multiply,
    Screen
};
struct DrawCommand
{
    TextureId texture;
    std::uint32_t first_index{0}, index_count{0};
    BlendMode blend{BlendMode::Over};
};
// Non-owning views; caller keeps storage alive until prepare returns.
struct RenderFrame
{
    std::span<const RenderVertex> vertices;
    std::span<const std::uint32_t> indices;
    std::span<const DrawCommand> commands;
};
} // namespace nekomote
