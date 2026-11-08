#pragma once

#include <nekomote/model/asset.hpp>
#include <nekomote/runtime/geometry_scene.hpp>

#include <utility>

namespace nekomote::test
{
inline runtime::GeometryCompileResult sprite_scene()
{
    model::ModelAsset asset;
    asset.base_group = "rig";
    asset.base_motion = "main";
    asset.textures.push_back({"atlas", "unused.png", "RGBA8", 8, 8});
    asset.icons.emplace("atlas/a", model::IconRect{"atlas", 0, 0, 8, 8, 0, 0});
    model::FrameContent content;
    content.src = {model::FieldState::Value, "atlas"};
    content.icon = {model::FieldState::Value, "a"};
    model::Layer layer;
    layer.label = "sprite";
    layer.type = 0;
    layer.inherit_mask = 0x20007fc;
    layer.frame_list.push_back({0, 2, {model::FieldState::Value, content}});
    model::Part part;
    part.group = "rig";
    part.name = "main";
    part.layers.push_back(std::move(layer));
    asset.parts.push_back(std::move(part));
    return runtime::compile_geometry_scene(asset);
}
} // namespace nekomote::test
