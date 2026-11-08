#pragma once

#include <catch2/catch_test_macros.hpp>
#include <nekomote/render_frame.hpp>

#include <array>
#include <limits>
#include <memory>

namespace nekomote::test
{
// Each draw completes before the next mutation; the same cases exercise GL,
// GLES and Vulkan without depending on buffer readback APIs unavailable in ES.
template <typename Draw, typename Prepare, typename Reserve>
void check_index_reuse(Draw draw, Prepare prepare, Reserve reserve)
{
    std::array<RenderVertex, 8> vertices{
        {{0, 0}, {24, 0}, {24, 64}, {0, 64}, {40, 0}, {64, 0}, {64, 64}, {40, 64}}};
    std::array<std::uint32_t, 6> indices{0, 1, 2, 0, 2, 3};
    std::array<DrawCommand, 1> commands{{{TextureId{1}, 0, 6}}};
    RenderFrame frame{vertices, indices, commands};
    frame.producer = std::make_shared<FrameProducer>();
    frame.index_revision = 1;
    const auto render = [&]
    {
        ++frame.revision;
        return draw(frame).pixels;
    };
    const auto left = render();
    for (std::size_t i = 0; i < 4; ++i)
        vertices[i].color[3] = 0.5f;
    REQUIRE(render() != left); // Vertex changes still upload at fixed topology.
    for (std::size_t i = 0; i < 4; ++i)
        vertices[i].color[3] = 1;
    REQUIRE(render() == left);
    for (auto& index : indices)
        index += 4;
    ++frame.index_revision;
    const auto right = render();
    REQUIRE(right != left);
    for (auto& index : indices)
        index -= 4;
    frame.producer = std::make_shared<FrameProducer>();
    REQUIRE(render() == left); // Independent producer, colliding index revision.
    frame.index_revision = 0;
    for (auto& index : indices)
        index += 4;
    REQUIRE(render() == right);
    for (auto& index : indices)
        index -= 4;
    REQUIRE(render() == left); // Zero index revision always uploads.
    frame.index_revision = 3;
    REQUIRE(render() == left);
    const auto producer = frame.producer;
    frame.producer.reset();
    for (auto& index : indices)
        index += 4;
    REQUIRE(render() == right); // No producer disables topology reuse too.
    frame.producer = producer;
    for (auto& index : indices)
        index -= 4;
    REQUIRE(render() == left);
    frame.revision = 0;
    for (auto& index : indices)
        index += 4;
    REQUIRE(draw(frame).pixels == right);
    for (auto& index : indices)
        index -= 4;
    REQUIRE(draw(frame).pixels == left); // Zero frame revision always uploads.

    ++frame.revision;
    vertices[0].x = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_THROWS(prepare(frame));
    vertices[0].x = 0;
    REQUIRE(render() == left);
    ++frame.revision;
    indices[0] = 99;
    REQUIRE_THROWS(prepare(frame)); // Index validation remains active on reuse.
    indices[0] = 0;
    REQUIRE(render() == left);
    ++frame.revision;
    commands[0].texture = TextureId{999};
    REQUIRE_THROWS(prepare(frame));
    commands[0].texture = TextureId{1};
    REQUIRE(render() == left);

    ++frame.revision;
    frame.vertices = std::span(vertices).first(2);
    REQUIRE_THROWS(prepare(frame)); // Smaller vertex range at the same topology.
    frame.vertices = vertices;
    REQUIRE(render() == left);
    reserve();
    REQUIRE(render() == left); // Reallocated storage cannot reuse the old EBO.
}
} // namespace nekomote::test
