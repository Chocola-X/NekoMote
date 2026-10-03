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
        throw std::runtime_error("invalid asset: " + path);
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
void validate_asset(const model::ModelAsset& a)
{
    require(a.spec == "ems" && std::isfinite(a.version), "spec/version");
    require(a.screen_width > 0 && a.screen_height > 0 && a.screen_width <= 32768 &&
                a.screen_height <= 32768,
            "screen dimensions");
    require(!a.textures.empty() && !a.parts.empty(), "empty model");
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
