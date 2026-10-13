#pragma once

#include <nekomote/application/session.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <numbers>
#include <span>

#include "backend.hpp"

namespace nekomote::player
{
// Explicit diagnostic only. Fill/rings follow the session's actual eligible regions;
// no mesh inference, input injection, texture readback or per-frame allocation.
class TouchOverlay
{
  public:
    explicit TouchOverlay(BackendKind kind
#if defined(NEKOMOTE_HAS_VULKAN)
                          ,
                          const render_vk::DeviceContext* context = nullptr
#endif
                          )
        : renderer_(kind
#if defined(NEKOMOTE_HAS_VULKAN)
                    ,
                    context
#endif
          )
    {
        renderer_.reserve({vertices_.size(), indices_.size(), commands_.size(), 1});
        renderer_.upload_texture(TextureId{1}, ImageRgba8{1, 1, {255, 255, 255, 255}});
        for (std::size_t i = 0; i < kSegments; ++i)
        {
            const float angle = static_cast<float>(i) * 2 * std::numbers::pi_v<float> /
                                static_cast<float>(kSegments);
            circle_[i] = {std::cos(angle), std::sin(angle)};
        }
    }
#if defined(NEKOMOTE_HAS_VULKAN)
    void set_target(render_vk::Target target, VkHost& host)
    {
        renderer_.set_target(target, host);
    }
#endif
    void render(const application::SessionInstance& session, int width, int height)
    {
        const auto regions = session.touch_regions();
        std::size_t vertex_count = 0, index_count = 0, command_count = 0;
        for (std::size_t zone = 0; zone < regions.size(); ++zone)
        {
            if (!regions[zone])
                continue;
            const auto& region = *regions[zone];
            auto fill_color = kColors[zone];
            fill_color[3] = .12f;
            const auto fill_base = static_cast<std::uint32_t>(vertex_count);
            const auto fill_index = static_cast<std::uint32_t>(index_count);
            vertices_[vertex_count++] = {region.center.x, region.center.y, .5f, .5f, fill_color};
            for (std::size_t i = 0; i < kSegments; ++i)
            {
                vertices_[vertex_count++] = {region.center.x + circle_[i].x * region.radius.x,
                                             region.center.y + circle_[i].y * region.radius.y, .5f,
                                             .5f, fill_color};
                for (const auto index :
                     {fill_base, fill_base + 1 + static_cast<std::uint32_t>(i),
                      fill_base + 1 + static_cast<std::uint32_t>((i + 1) % kSegments)})
                    indices_[index_count++] = index;
            }
            commands_[command_count++] = {TextureId{1}, fill_index,
                                          static_cast<std::uint32_t>(kSegments * 3)};
            const auto base = static_cast<std::uint32_t>(vertex_count);
            const auto first_index = static_cast<std::uint32_t>(index_count);
            for (std::size_t i = 0; i < kSegments; ++i)
            {
                const auto p = circle_[i];
                for (const float border : {-1.0f, 1.0f})
                {
                    const float rx = std::max(0.0f, region.radius.x + border);
                    const float ry = std::max(0.0f, region.radius.y + border);
                    vertices_[vertex_count++] = {region.center.x + p.x * rx,
                                                 region.center.y + p.y * ry, .5f, .5f,
                                                 kColors[zone]};
                }
                const auto a = base + static_cast<std::uint32_t>(i * 2);
                const auto b = base + static_cast<std::uint32_t>((i + 1) % kSegments * 2);
                for (const auto index : {a, b, a + 1, a + 1, b, b + 1})
                    indices_[index_count++] = index;
            }
            commands_[command_count++] = {TextureId{1}, first_index,
                                          static_cast<std::uint32_t>(kSegments * 6)};
        }
        renderer_.prepare({std::span(vertices_).first(vertex_count),
                           std::span(indices_).first(index_count),
                           std::span(commands_).first(command_count)});
        renderer_.render({static_cast<float>(width) * .5f, static_cast<float>(height) * .5f, 1},
                         BackendContents::Preserve, width, height);
    }

  private:
    static constexpr std::size_t kSegments = 128;
    // TouchZone priority: bust red, eye yellow, face green, head cyan, pant purple.
    static constexpr std::array<std::array<float, 4>, model::kTouchZoneCount> kColors{
        {{1, .2f, .2f, .9f},
         {1, .9f, .1f, .9f},
         {.2f, 1, .3f, .9f},
         {.1f, .8f, 1, .9f},
         {.8f, .3f, 1, .9f}}};
    BackendRenderer renderer_;
    std::array<Vec2, kSegments> circle_;
    std::array<RenderVertex, model::kTouchZoneCount*(kSegments * 3 + 1)> vertices_;
    std::array<std::uint32_t, model::kTouchZoneCount * kSegments * 9> indices_;
    std::array<DrawCommand, model::kTouchZoneCount * 2> commands_;
};
} // namespace nekomote::player
