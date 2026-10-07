#include "validation.hpp"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <unordered_set>

namespace nekomote::asset_io
{
namespace
{
void require(bool condition, const std::string& path)
{
    if (!condition)
        throw AssetValidationError("invalid asset: " + path);
}
template <typename T>
void finite_field(const model::Field<T>& f, const std::string& p)
{
    if (!f.has_value())
        return;
    if constexpr (std::is_arithmetic_v<T>)
        require(std::isfinite(static_cast<double>(f.data)), p);
    else
        for (auto value : f.data)
            require(std::isfinite(value), p);
}
} // namespace
void validate_physics(const model::ModelAsset& asset)
{
    finite_field(asset.author_scale, "/metadata/scale");
    if (asset.author_scale.has_value())
        require(asset.author_scale.data > 0, "/metadata/scale");
    if (!asset.spring_controls.empty() || !asset.chain_controls.empty())
        require(asset.author_scale.has_value(), "/metadata/scale required for physical data");
    const auto binding = [](const model::PhysicsBinding& value)
    {
        require(!value.source_path.empty() && !value.label.empty() && !value.anchor.empty() &&
                    !value.variable_x.empty() && !value.variable_y.empty() &&
                    value.variable_x != value.variable_y,
                value.source_path + "/physical binding");
    };
    const auto vector = [](const auto& value, const std::string& path)
    {
        for (float component : value)
            require(std::isfinite(component), path);
    };
    const auto friction = [](float value, const std::string& path)
    { require(std::isfinite(value) && value >= 0 && value <= 1, path); };
    for (const auto& value : asset.spring_controls)
    {
        binding(value.binding);
        const auto& path = value.binding.source_path;
        require(std::isfinite(value.stiffness) && value.stiffness >= 0, path + "/spring");
        friction(value.friction, path + "/friction");
        require(std::isfinite(value.gravity) && value.gravity >= 0, path + "/gravity");
        vector(value.axis_scale, path + "/axis scale");
        vector(value.position, path + "/param/p");
        vector(value.velocity, path + "/param/pv");
        vector(value.previous_anchor, path + "/param/op");
        require(std::isfinite(value.vertical_offset), path + "/param/ofs");
    }
    for (const auto& value : asset.chain_controls)
    {
        binding(value.binding);
        const auto& path = value.binding.source_path;
        require(!value.variable_middle.empty() &&
                    value.variable_middle != value.binding.variable_x &&
                    value.variable_middle != value.binding.variable_y,
                path + "/var_lrm");
        require(std::isfinite(value.stretch_stiffness) && value.stretch_stiffness >= 0,
                path + "/b_rate");
        require(std::isfinite(value.gravity) && value.gravity >= 0, path + "/gravity");
        require(std::isfinite(value.normal_velocity_damping) && value.normal_velocity_damping >= 0,
                path + "/v_bound");
        for (float f : value.friction)
            friction(f, path + "/friction");
        for (float length : value.length)
            require(std::isfinite(length) && length > 0, path + "/length");
        vector(value.scale_x, path + "/scale_x");
        vector(value.scale_y, path + "/scale_y");
        for (std::size_t i = 0; i < 2; ++i)
        {
            vector(value.rest_position[i], path + "/param/bp");
            vector(value.position[i], path + "/param/p");
            vector(value.velocity[i], path + "/param/pv");
        }
        vector(value.previous_anchor, path + "/param/op");
        for (float number : {value.bend_speed, value.bend_volume, value.vertical_offset,
                             value.bend_phase, value.bend_gain})
            require(std::isfinite(number), path + "/bend/offset");
        require(value.bend_speed >= 0 && value.bend_volume >= 0 && value.bend_gain >= 0 &&
                    value.bend_gain <= 1,
                path + "/bend ranges");
    }
}
void validate_asset(const model::ModelAsset& a)
{
    validate_physics(a);
    require(a.spec == "ems" && std::isfinite(a.version), "spec/version");
    require(a.screen_width > 0 && a.screen_height > 0 && a.screen_width <= 32768 &&
                a.screen_height <= 32768,
            "screen dimensions");
    require(!a.textures.empty() && !a.parts.empty(), "empty model");
    std::unordered_set<std::string> mirrored, declared_parameters;
    model::visit_parameter_names(a, [&](const auto& name) { declared_parameters.insert(name); });
    for (const auto& name : a.mirrored_parameters)
        require(a.mirror && !name.empty() && declared_parameters.contains(name) &&
                    mirrored.insert(name).second,
                "invalid mirrored parameter " + name);
    std::unordered_set<std::string> textures, motions;
    for (const auto& t : a.textures)
    {
        require(!t.name.empty() && textures.insert(t.name).second, "duplicate texture " + t.name);
        require(t.width > 0 && t.height > 0 && t.width <= 32768 && t.height <= 32768,
                "atlas dimensions " + t.name);
        const std::filesystem::path path{t.path};
        require(!path.empty() && !path.is_absolute(), "texture path " + t.name);
        for (const auto& s : path)
            require(s != "..", "texture traversal " + t.name);
    }
    for (const auto& [key, r] : a.icons)
    {
        const auto t = std::find_if(a.textures.begin(), a.textures.end(),
                                    [&r](const auto& tex) { return tex.name == r.texture; });
        require(t != a.textures.end(), "icon texture " + key);
        for (float v : {r.left, r.top, r.width, r.height, r.origin_x, r.origin_y})
            require(std::isfinite(v), "icon numeric " + key);
        require(r.left >= 0 && r.top >= 0 && r.width > 0 && r.height > 0 &&
                    r.left + r.width <= static_cast<float>(t->width) &&
                    r.top + r.height <= static_cast<float>(t->height),
                "icon bounds " + key);
    }
    for (const auto& p : a.parts)
        require(motions.insert(p.group + "/" + p.name).second, "duplicate motion");
    if (!a.base_group.empty())
        require(motions.contains(a.base_group + "/" + a.base_motion), "base motion");
    for (const auto& c : a.paired_clamps)
        require(!c.variable_x.empty() && !c.variable_y.empty() && c.variable_x != c.variable_y &&
                    std::isfinite(c.range_begin) && std::isfinite(c.range_end) &&
                    c.range_begin < c.range_end,
                "paired clamp " + c.label);
    for (const auto& c : a.variable_controls)
    {
        const auto path = "variable controller " + c.variable;
        require(!c.variable.empty(), path);
        for (float value : {c.begin_frame, c.end_frame, c.blink_frames, c.blink_interval_min,
                            c.blink_interval_max})
            require(std::isfinite(value), path);
        require(c.blink_frames >= 0 && c.blink_interval_min >= 0 &&
                    c.blink_interval_max >= c.blink_interval_min,
                path + "/blink timing");
        for (const auto& edge : c.edges)
            require(std::isfinite(edge[0]) && std::isfinite(edge[1]) && edge[0] <= edge[1],
                    path + "/edge");
        for (const auto& junction : c.junctions)
            for (float value : junction)
                require(std::isfinite(value), path + "/junction");
        for (const auto& option : c.options)
            require(!option.variable.empty() && std::isfinite(option.on_value) &&
                        std::isfinite(option.off_value),
                    path + "/option");
    }
    int layers = 0, frames = 0, bp = 0, cc = 0;
    auto walk = [&](auto&& self, const model::Layer& l, const model::Part& part, int depth) -> void
    {
        require(depth < 256, "layer nesting");
        ++layers;
        const auto path = part.group + "/" + part.name + "/" + l.label;
        require(l.mesh_division >= 0 && l.mesh_division <= 1024, path + "/mesh_division");
        const auto& bindings = l.parameter.empty() ? part.parameter : l.parameter;
        if (l.parameterize.has_value())
            require(l.parameterize.data >= 0 &&
                        static_cast<std::size_t>(l.parameterize.data) < bindings.size(),
                    path + "/parameterize");
        for (const auto& b : bindings)
            require(std::isfinite(b.range_begin) && std::isfinite(b.range_end) &&
                        b.range_begin <= b.range_end && b.division >= 0,
                    path + "/parameter");
        float last = -std::numeric_limits<float>::infinity();
        for (const auto& f : l.frame_list)
        {
            ++frames;
            require(std::isfinite(f.time) && f.time >= last, path + "/frame time");
            last = f.time;
            if (!f.content.has_value())
                continue;
            const auto& c = f.content.data;
            finite_field(c.coord, path + "/coord");
            finite_field(c.opa, path + "/opa");
            finite_field(c.angle, path + "/angle");
            finite_field(c.zx, path + "/zx");
            finite_field(c.zy, path + "/zy");
            finite_field(c.ox, path + "/ox");
            finite_field(c.oy, path + "/oy");
            if (c.mesh.has_value())
            {
                finite_field(c.mesh.data.bp, path + "/bp");
                finite_field(c.mesh.data.cc, path + "/cc");
                bp += c.mesh.data.bp.has_value() ? 1 : 0;
                cc += c.mesh.data.cc.has_value() ? 1 : 0;
            }
            if (c.src.has_value() && c.icon.has_value() && c.src.data != "blank")
            {
                const auto key = c.src.data + "/" + c.icon.data;
                require(textures.contains(c.src.data) ? a.icons.contains(key)
                                                      : motions.contains(key),
                        path + "/resource " + key);
            }
        }
        for (const auto& child : l.children)
            self(self, child, part, depth + 1);
    };
    for (const auto& p : a.parts)
        for (const auto& l : p.layers)
            walk(walk, l, p, 0);
    require(layers == a.total_layers && frames == a.total_frames && bp == a.bp_nonnull &&
                cc == a.cc_nonnull,
            "diagnostic counts");
    for (const auto& t : a.timelines)
        for (const auto& track : t.tracks)
        {
            float last = -std::numeric_limits<float>::infinity();
            for (const auto& k : track.keys)
            {
                require(std::isfinite(k.time) && k.time >= last, "timeline key time " + t.label);
                last = k.time;
                finite_field(k.value, "timeline value " + t.label);
                finite_field(k.easing, "timeline easing " + t.label);
            }
        }
}
} // namespace nekomote::asset_io
