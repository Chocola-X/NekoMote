#pragma once

#include <nekomote/render_frame.hpp>
#include <nekomote/runtime/geometry_scene.hpp>
#include <nlohmann/json.hpp>

#include <span>
#include <vector>

namespace nekomote::diagnostics
{
// Offline observation only. Export full native protocol without touching the
// runtime/render path; vertices are xyuv followed by straight RGBA.
inline nlohmann::json frame_json(RenderFrame frame,
                                 std::span<const runtime::TextureBinding> textures)
{
    using Json = nlohmann::json;
    Json vertices = Json::array(), commands = Json::array(), mask_commands = Json::array(),
         masks = Json::array(), resources = Json::array();
    for (const auto& v : frame.vertices)
        vertices.push_back({v.x, v.y, v.u, v.v, v.color[0], v.color[1], v.color[2], v.color[3]});
    const auto command = [](const DrawCommand& c)
    {
        return Json{{"texture", c.texture.value},
                    {"first_index", c.first_index},
                    {"index_count", c.index_count},
                    {"blend", static_cast<int>(c.blend)},
                    {"protection", static_cast<int>(c.protection)},
                    {"mask", c.mask.value}};
    };
    for (const auto& c : frame.commands)
        commands.push_back(command(c));
    for (const auto& c : frame.mask_commands)
        mask_commands.push_back(command(c));
    for (const auto& m : frame.masks)
        masks.push_back({{"first_command", m.first_command}, {"command_count", m.command_count}});
    for (const auto& t : textures)
        resources.push_back({{"id", t.id.value}, {"width", t.width}, {"height", t.height}});
    return Json{{"vertices", vertices},
                {"indices", std::vector<std::uint32_t>(frame.indices.begin(), frame.indices.end())},
                {"commands", commands},
                {"textures", resources},
                {"mask_commands", mask_commands},
                {"masks", masks},
                {"composition", static_cast<int>(frame.composition)}};
}
} // namespace nekomote::diagnostics
