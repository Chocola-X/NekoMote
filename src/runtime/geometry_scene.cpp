#include <nekomote/runtime/deformation.hpp>
#include <nekomote/runtime/geometry_scene.hpp>

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cmath>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace nekomote::runtime
{
namespace
{
struct ResourceTag
{
};
using ResourceId = Id<ResourceTag>;
enum class ResourceKind
{
    None,
    Sprite,
    Blank,
    Motion
};
struct Resource
{
    std::string name;
    ResourceKind kind{ResourceKind::None};
    TextureId texture;
    model::IconRect rect;
    const model::Part* motion{nullptr}; // compile-only; cleared before returning the scene
    bool parameter_driven{false};
};
struct Pose
{
    Vec2 translation;
    float angle_degrees{0}, depth{0}, opacity{1}; // source degrees; depth; straight opacity
    Patch patch{identity_patch()};
    ResourceId resource;
    bool mesh{false}, active{false}, unsupported{false};
};
struct Key
{
    float time{0};
    int type{0};
    Pose pose;
};
struct Binding
{
    ParamId variable;
    float begin{0}, end{0};
    int division{0}, discretization{0};
    bool enabled{false};
};
struct Node
{
    std::string label, source_label;
    std::uint32_t scope{0};
    NodeId parent;
    MaskId mask;
    int stencil_type{0};
    std::vector<std::string> mask_sources;
    ResourceId guard;
    Binding binding;
    std::vector<Key> keys;
    int type{0}, division{0}, priority{0}, maximum_division{0}, child_sync{0};
    float maximum_density{0};
    bool combine{false}, unsupported{false}, has_sprites{false}, mask_source{false};
    bool exported_locator{false}, partial_locator_origin{false}, neutral_locator_origin{false};
    bool inherit_shape{true}; // inheritMask bit 25; false = 親変形無視, drop parent mesh_path
};
struct Clamp
{
    ParamId x, y;
    float begin{0}, end{0};
    int type{0};
    bool enabled{true};
};
struct CompiledMask
{
    NodeId node;
    std::vector<NodeId> members;
    bool resolved{true};
};
struct CompiledData
{
    std::vector<VariableSymbol> variables;
    std::vector<TextureBinding> textures;
    std::vector<Resource> resources{1};
    std::vector<Node> nodes{1};
    std::vector<Clamp> clamps;
    std::vector<CompiledMask> masks;
    std::vector<GeometryLocator> locators;
    std::size_t mask_command_capacity{0};
    std::vector<std::string> diagnostics;
    std::size_t vertex_capacity{0}, index_capacity{0};
};
template <typename T>
T field_or(const model::Field<T>& field, T fallback)
{
    return field.has_value() ? field.data : fallback;
}
void require(bool condition, const std::string& path, const char* message)
{
    if (!condition)
        throw std::runtime_error(path + ": " + message);
}
model::IconRect blank_rect(std::string_view text)
{
    std::array<float, 4> values{};
    for (std::size_t i = 0; i < values.size(); ++i)
    {
        const auto end = text.find(':');
        const auto token = text.substr(0, end);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), values[i]);
        require(parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size() &&
                    std::isfinite(values[i]),
                std::string(text), "invalid blank domain");
        require((i == 3) == (end == std::string_view::npos), std::string(text),
                "blank domain requires four components");
        if (end != std::string_view::npos)
            text.remove_prefix(end + 1);
    }
    require(values[0] > 0 && values[1] > 0, std::string(text), "nonpositive blank domain");
    model::IconRect r;
    r.width = values[0];
    r.height = values[1];
    r.origin_x = values[2];
    r.origin_y = values[3];
    return r;
}
bool same_domain(const Resource& a, const Resource& b) noexcept
{
    return a.rect.width > 0 && b.rect.width > 0 && a.rect.width == b.rect.width &&
           a.rect.height == b.rect.height && a.rect.origin_x == b.rect.origin_x &&
           a.rect.origin_y == b.rect.origin_y;
}
std::pair<int, int> grid(int division, const model::IconRect& r) noexcept
{
    const auto x = static_cast<int>(static_cast<float>(division) * r.width / (r.width + r.height));
    return {std::max(2, x + 1), std::max(2, division - x + 1)};
}
struct Compiler
{
    CompiledData& data;
    std::unordered_map<std::string, ResourceId> resources;
    std::unordered_map<std::string, ParamId> variables;
    std::vector<const model::Part*> stack;
    std::uint32_t next_scope{0};

    ParamId variable(const std::string& name)
    {
        require(!name.empty(), "parameter", "empty variable name");
        const auto found = variables.find(name);
        if (found != variables.end())
            return found->second;
        const ParamId id{static_cast<std::uint32_t>(data.variables.size() + 1)};
        variables.emplace(name, id);
        data.variables.push_back({id, name});
        return id;
    }
    ResourceId resource(const model::FrameContent& content, const std::string& path)
    {
        if (!content.src.has_value() || !content.icon.has_value())
            return {};
        const auto name = content.src.data + "/" + content.icon.data;
        const auto found = resources.find(name);
        if (found != resources.end())
            return found->second;
        require(content.src.data == "blank", path, "unresolved resource");
        Resource r;
        r.name = name;
        r.kind = ResourceKind::Blank;
        r.rect = blank_rect(content.icon.data);
        const ResourceId id{static_cast<std::uint32_t>(data.resources.size())};
        data.resources.push_back(std::move(r));
        resources.emplace(name, id);
        return id;
    }
    Pose pose(const model::LayerFrame& frame, const std::string& path)
    {
        Pose p;
        if (!frame.content.has_value() || frame.type == 0)
            return p;
        p.active = true;
        const auto& c = frame.content.data;
        const auto coord = field_or(c.coord, std::array<float, 3>{});
        p.translation = {coord[0], coord[1]};
        p.depth = coord[2];
        p.angle_degrees = field_or(c.angle, 0.0f);
        p.opacity = field_or(c.opa, 255.0f) / 255;
        p.resource = resource(c, path);
        if (c.mesh.has_value())
        {
            require(!c.mesh.data.cc.has_value(), path, "cc deformation is unverified");
            if (c.mesh.data.bp.has_value())
            {
                p.mesh = true;
                const auto& raw = c.mesh.data.bp.data;
                for (std::size_t i = 0; i < p.patch.size(); ++i)
                    p.patch[i] = {raw[i * 2], raw[i * 2 + 1]};
            }
        }
        p.unsupported = frame.type == 1 || c.zx.has_value() || c.zy.has_value() ||
                        c.ox.has_value() || c.oy.has_value() || field_or(c.flip, 0) != 0 ||
                        field_or(c.color, std::uint32_t{0x808080ff}) != 0x808080ff ||
                        (c.motion.has_value() && field_or(c.motion.data.time_offset, 0.0f) != 0 &&
                         !data.resources[p.resource.value].parameter_driven);
        require(std::isfinite(p.translation.x) && std::isfinite(p.translation.y) &&
                    std::isfinite(p.depth) && std::isfinite(p.angle_degrees) &&
                    std::isfinite(p.opacity),
                path, "nonfinite pose");
        for (const auto point : p.patch)
            require(std::isfinite(point.x) && std::isfinite(point.y), path, "nonfinite patch");
        return p;
    }
    void motion(const model::Part& part, NodeId parent, ResourceId guard, const std::string& path,
                std::size_t depth = 0)
    {
        require(depth < 256, path, "expanded layer nesting too deep");
        require(stack.size() < 128 && std::find(stack.begin(), stack.end(), &part) == stack.end(),
                path, "cyclic or too deep motion references");
        stack.push_back(&part);
        const auto scope = ++next_scope;
        for (const auto& layer : part.layers)
            node(layer, part, parent, guard, path, depth + 1, scope);
        stack.pop_back();
    }
    void node(const model::Layer& layer, const model::Part& part, NodeId parent, ResourceId guard,
              const std::string& parent_path, std::size_t depth, std::uint32_t scope)
    {
        require(depth < 256, parent_path, "expanded layer nesting too deep");
        require(data.nodes.size() < 100000, parent_path, "expanded scene too large");
        Node n;
        n.label = parent_path + "/" + layer.label;
        n.source_label = layer.label;
        n.scope = scope;
        n.stencil_type = layer.stencil_type;
        n.mask_sources = layer.stencil_composite_mask_layers;
        require(layer.type != 12 || layer.stencil_type == 5, n.label, "unverified stencil type");
        n.parent = parent;
        n.guard = guard;
        n.type = layer.type;
        n.exported_locator = layer.type == 1 && layer.export_self == 1;
        n.neutral_locator_origin =
            n.exported_locator && layer.children.empty() &&
            std::all_of(layer.frame_list.begin(), layer.frame_list.end(),
                        [](const auto& frame)
                        {
                            if (!frame.content.has_value())
                                return true;
                            const auto& c = frame.content.data;
                            return field_or(c.coord, std::array<float, 3>{}) ==
                                       std::array<float, 3>{} &&
                                   field_or(c.angle, 0.0f) == 0 && !c.mesh.has_value() &&
                                   !c.src.has_value() && !c.icon.has_value() && !c.zx.has_value() &&
                                   !c.zy.has_value() && !c.ox.has_value() && !c.oy.has_value();
                        });
        require(n.type == 0 || n.type == 1 || n.type == 2 || n.type == 3 || n.type == 12, n.label,
                "unsupported layer type");
        n.division = layer.mesh_division;
        n.priority = layer.obj_tri_priority;
        n.maximum_division = n.division;
        require(n.division >= 0 && n.division <= 256, n.label, "unsupported division capacity");
        n.combine = layer.mesh_combine == 1;
        n.child_sync = layer.mesh_sync_child_mask;
        const bool unsupported_common =
            layer.coordinate != 0 || (layer.mesh_transform != 0 && layer.mesh_transform != 1) ||
            (layer.mesh_combine != 0 && layer.mesh_combine != 1) ||
            (layer.mesh_sync_child_mask != 0 && layer.mesh_sync_child_mask != 1 &&
             layer.mesh_sync_child_mask != 8) ||
            (!layer.transform_order.empty() &&
             layer.transform_order != std::vector<int>{0, 3, 2, 1});
        n.inherit_shape = (layer.inherit_mask & 0x2000000) != 0;
        n.partial_locator_origin =
            !unsupported_common && n.exported_locator && layer.inherit_mask == 0x200020c;
        // type=2 + inheritMask=0x7fc is a 親変形無視 reference node: it provides
        // a coordinate frame whose subtree drops the parent's shape deformation
        // (mesh_path) while keeping position/rotation inheritance. Source labels
        // end with _親変形無視; the bit pattern is the structural signal.
        n.unsupported =
            unsupported_common || (layer.inherit_mask != 0x20007fc &&
                                   !(layer.type == 12 && layer.inherit_mask == 0x24007fc) &&
                                   !(layer.type == 2 && layer.inherit_mask == 0x7fc));
        const auto binding =
            layer.parameterize.has_value() ? layer.parameterize : part.parameterize;
        const auto& parameters = layer.parameter.empty() ? part.parameter : layer.parameter;
        if (binding.has_value())
        {
            require(binding.data >= 0 && static_cast<std::size_t>(binding.data) < parameters.size(),
                    n.label, "invalid parameter binding");
            const auto& b = parameters[static_cast<std::size_t>(binding.data)];
            n.binding = {variable(b.id), b.range_begin,    b.range_end,
                         b.division,     b.discretization, b.enabled};
            require(std::isfinite(b.range_begin) && std::isfinite(b.range_end) &&
                        b.range_begin <= b.range_end && b.division >= 0,
                    n.label, "invalid parameter range");
        }
        std::set<ResourceId> motions;
        float previous = -std::numeric_limits<float>::infinity();
        for (const auto& f : layer.frame_list)
        {
            require(std::isfinite(f.time) && f.time >= previous && f.type >= 0 && f.type <= 3,
                    n.label, "unsupported frame type/time");
            previous = f.time;
            auto p = pose(f, n.label);
            const auto& r = data.resources[p.resource.value];
            if (r.kind == ResourceKind::Sprite && layer.type == 0)
            {
                n.has_sprites = true;
            }
            if (r.kind == ResourceKind::Motion)
                motions.insert(p.resource);
            n.keys.push_back({f.time, f.type, std::move(p)});
        }
        const NodeId id{static_cast<std::uint32_t>(data.nodes.size())};
        data.nodes.push_back(std::move(n));
        const auto path = data.nodes[id.value].label;
        // Recursive expansion can reallocate nodes/resources. Borrow no element.
        for (const auto resource_id : motions)
        {
            const auto* nested = data.resources[resource_id.value].motion;
            motion(*nested, id, resource_id, path + "/" + nested->group + "/" + nested->name,
                   depth + 1);
        }
        for (const auto& child : layer.children)
            node(child, part, id, {}, path, depth + 1, scope);
    }
};
struct RigidTransform
{
    float a{1}, b{0}, c{0}, d{1};
    Vec2 translation{};
    Vec2 apply(double x, double y) const noexcept
    {
        return {static_cast<float>(static_cast<double>(a) * x + static_cast<double>(c) * y +
                                   static_cast<double>(translation.x)),
                static_cast<float>(static_cast<double>(b) * x + static_cast<double>(d) * y +
                                   static_cast<double>(translation.y))};
    }
};
RigidTransform compose(const RigidTransform& parent, const RigidTransform& local) noexcept
{
    const auto product = [](float a, float b, float c, float d) noexcept
    {
        return static_cast<float>(static_cast<double>(a) * static_cast<double>(b) +
                                  static_cast<double>(c) * static_cast<double>(d));
    };
    return {product(parent.a, local.a, parent.c, local.b),
            product(parent.b, local.a, parent.d, local.b),
            product(parent.a, local.c, parent.c, local.d),
            product(parent.b, local.c, parent.d, local.d),
            parent.apply(local.translation.x, local.translation.y)};
}
struct State
{
    Pose pose;
    Pose transform;
    NodeId transform_parent, curve_parent;
    int child_sync{0};
    RigidTransform world;
    Patch world_patch{};
    Vec2 domain_corner{};
    std::array<float, 4> domain_inverse{};
    float depth{0}, opacity{0};
    bool mesh_path{false};
    bool branch_selected{false};
    float density{0};
};
Pose interpolate(const Key& a, const Key& b, double time) noexcept
{
    Pose p = a.pose;
    if (a.type != 3 || !b.pose.active || b.time <= a.time)
        return p;
    // SDK store observations preserve double left/right weighted sums until
    // one Float32 save; subtracting the keys first loses verified corner bits.
    // Evidence: tests/fixtures/scene_blend_reference.json and full jump frames.
    const double t = (static_cast<double>(time) - static_cast<double>(a.time)) /
                     (static_cast<double>(b.time) - static_cast<double>(a.time));
    const auto blend = [t](float left, float right) noexcept
    {
        return static_cast<float>(static_cast<double>(left) * (1.0 - t) +
                                  static_cast<double>(right) * t);
    };
    p.translation = {blend(a.pose.translation.x, b.pose.translation.x),
                     blend(a.pose.translation.y, b.pose.translation.y)};
    p.angle_degrees = blend(a.pose.angle_degrees, b.pose.angle_degrees);
    p.depth = blend(a.pose.depth, b.pose.depth);
    p.opacity = blend(a.pose.opacity, b.pose.opacity);
    for (std::size_t i = 0; i < p.patch.size(); ++i)
        p.patch[i] = {blend(a.pose.patch[i].x, b.pose.patch[i].x),
                      blend(a.pose.patch[i].y, b.pose.patch[i].y)};
    p.mesh = p.mesh || (t > 0 && b.pose.mesh);
    p.unsupported = p.unsupported || (t > 0 && b.pose.unsupported);
    return p;
}
} // namespace
struct GeometryScene::Impl : CompiledData
{
};
GeometryScene::GeometryScene(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
GeometryScene::~GeometryScene() = default;
std::span<const VariableSymbol> GeometryScene::variables() const noexcept
{
    return impl_->variables;
}
std::span<const GeometryLocator> GeometryScene::locators() const noexcept
{
    return impl_->locators;
}
std::span<const TextureBinding> GeometryScene::textures() const noexcept
{
    return impl_->textures;
}
std::span<const std::string> GeometryScene::diagnostics() const noexcept
{
    return impl_->diagnostics;
}
ParamId GeometryScene::find_variable(std::string_view name) const noexcept
{
    for (const auto& v : impl_->variables)
        if (v.name == name)
            return v.id;
    return {};
}
std::size_t GeometryScene::node_count() const noexcept
{
    return impl_->nodes.size() - 1;
}
std::string_view GeometryScene::node_label(NodeId id) const noexcept
{
    return id.valid() && id.value < impl_->nodes.size() ? impl_->nodes[id.value].label
                                                        : std::string_view{};
}
GeometryCapacity GeometryScene::frame_capacity() const noexcept
{
    return {impl_->vertex_capacity, impl_->index_capacity, impl_->nodes.size(),
            impl_->mask_command_capacity, impl_->masks.size()};
}
GeometryCompileResult compile_geometry_scene(const model::ModelAsset& asset)
{
    GeometryCompileResult result;
    try
    {
        auto data = std::make_unique<GeometryScene::Impl>();
        Compiler compiler{*data, {}, {}, {}};
        std::unordered_map<std::string, TextureId> textures;
        for (const auto& t : asset.textures)
        {
            const TextureId id{static_cast<std::uint32_t>(data->textures.size() + 1)};
            data->textures.push_back({id, t.name, t.path, t.width, t.height});
            require(t.width > 0 && t.height > 0, t.name, "invalid texture dimensions");
            require(textures.emplace(t.name, id).second, t.name, "duplicate texture");
        }
        std::vector<std::string> names;
        names.reserve(asset.icons.size());
        for (const auto& [name, rect] : asset.icons)
        {
            static_cast<void>(rect);
            names.push_back(name);
        }
        std::sort(names.begin(), names.end());
        for (const auto& name : names)
        {
            const auto& r = asset.icons.at(name);
            require(
                r.width > 0 && r.height > 0 && std::isfinite(r.width) && std::isfinite(r.height) &&
                    textures.contains(r.texture),
                name, "invalid sprite rectangle");
            const ResourceId id{static_cast<std::uint32_t>(data->resources.size())};
            data->resources.push_back(
                {name, ResourceKind::Sprite, textures.at(r.texture), r, nullptr});
            compiler.resources.emplace(name, id);
        }
        for (const auto& m : asset.parts)
        {
            const auto name = m.group + "/" + m.name;
            const ResourceId id{static_cast<std::uint32_t>(data->resources.size())};
            require(compiler.resources.emplace(name, id).second, name, "duplicate resource");
            const bool parameter_driven =
                !m.layers.empty() &&
                std::all_of(m.layers.begin(), m.layers.end(),
                            [&](const auto& layer)
                            {
                                const auto binding = layer.parameterize.has_value()
                                                         ? layer.parameterize
                                                         : m.parameterize;
                                const auto& parameters =
                                    layer.parameter.empty() ? m.parameter : layer.parameter;
                                return binding.has_value() && binding.data >= 0 &&
                                       static_cast<std::size_t>(binding.data) < parameters.size() &&
                                       parameters[static_cast<std::size_t>(binding.data)].enabled;
                            });
            // A parameterized motion root owns its sample time; the reference's
            // authored offset must not overwrite it. Fraise pupil offsets 20/40
            // are inert in controlled source captures. Free clocks stay rejected.
            data->resources.push_back({name, ResourceKind::Motion, {}, {}, &m, parameter_driven});
        }
        for (const auto& v : asset.variables)
            compiler.variable(v.label);
        for (const auto& control : asset.variable_controls)
        {
            compiler.variable(control.variable);
            if (!control.talk_variable.empty())
                compiler.variable(control.talk_variable);
            for (const auto& option : control.options)
                compiler.variable(option.variable);
        }
        for (const auto& timeline : asset.timelines)
            for (const auto& track : timeline.tracks)
                compiler.variable(track.label);
        for (const auto& c : asset.paired_clamps)
            data->clamps.push_back({compiler.variable(c.variable_x),
                                    compiler.variable(c.variable_y), c.range_begin, c.range_end,
                                    c.type, c.enabled});
        for (const auto& control : asset.spring_controls)
        {
            compiler.variable(control.binding.variable_x);
            compiler.variable(control.binding.variable_y);
        }
        for (const auto& control : asset.chain_controls)
        {
            compiler.variable(control.binding.variable_x);
            compiler.variable(control.binding.variable_y);
            compiler.variable(control.variable_middle);
        }
        const auto base = compiler.resources.find(asset.base_group + "/" + asset.base_motion);
        require(base != compiler.resources.end(), "base", "missing base motion");
        require(data->resources[base->second.value].kind == ResourceKind::Motion, "base",
                "not a motion");
        compiler.motion(*data->resources[base->second.value].motion, {}, {},
                        asset.base_group + "/" + asset.base_motion);
        // Resolve labels within each expanded motion scope, not across instances.
        for (std::size_t i = 1; i < data->nodes.size(); ++i)
        {
            auto& node = data->nodes[i];
            node.mask = data->nodes[node.parent.value].mask;
            if (node.type != 12)
                continue;
            node.mask = MaskId{static_cast<std::uint32_t>(data->masks.size() + 1)};
            CompiledMask mask{NodeId{static_cast<std::uint32_t>(i)}, {}};
            std::vector<NodeId> roots;
            for (const auto& name : node.mask_sources)
            {
                // "add_mask" is a stencil composite operation marker, not a
                // node label. It selects additive mask composition in the
                // render pass (stencil INCR-style) but contributes no source
                // geometry. Skip it during source resolution.
                if (name == "add_mask")
                    continue;
                NodeId root;
                for (std::size_t j = 1; j < data->nodes.size(); ++j)
                    if (data->nodes[j].scope == node.scope && data->nodes[j].source_label == name)
                    {
                        require(!root.valid(), node.label, "ambiguous stencil source label");
                        root = NodeId{static_cast<std::uint32_t>(j)};
                    }
                if (!root.valid())
                {
                    mask.resolved = false;
                    data->diagnostics.push_back(node.label + "/" + name +
                                                ": unresolved mask source; reject if selected");
                }
                else
                    roots.push_back(root);
            }
            require(!node.mask_sources.empty(), node.label, "empty stencil source list");
            for (std::size_t j = 1; j < data->nodes.size(); ++j)
            {
                auto ancestor = NodeId{static_cast<std::uint32_t>(j)};
                while (ancestor.valid() &&
                       std::find(roots.begin(), roots.end(), ancestor) == roots.end())
                    ancestor = data->nodes[ancestor.value].parent;
                if (ancestor.valid())
                {
                    mask.members.push_back(NodeId{static_cast<std::uint32_t>(j)});
                    data->nodes[j].mask_source = true;
                }
            }
            data->mask_command_capacity += mask.members.size();
            require(data->mask_command_capacity <= 1000000, node.label, "mask capacity exceeded");
            data->masks.push_back(std::move(mask));
        }
        for (std::size_t i = data->nodes.size() - 1; i > 0; --i)
        {
            const auto& n = data->nodes[i];
            if (n.has_sprites)
                data->nodes[n.parent.value].has_sprites = true;
        }
        for (std::size_t i = 1; i < data->nodes.size(); ++i)
        {
            auto& n = data->nodes[i];
            n.maximum_density = data->nodes[n.parent.value].maximum_density;
            for (const auto& key : n.keys)
            {
                const auto& r = data->resources[key.pose.resource.value];
                if (key.pose.mesh && r.rect.width > 0 && n.division > 0)
                    n.maximum_density =
                        std::max(n.maximum_density,
                                 static_cast<float>(n.division) / (r.rect.width + r.rect.height));
            }
            if (!n.has_sprites || n.type != 0)
                continue;
            if (n.division == 0 && n.priority == 2)
                for (const auto& key : n.keys)
                {
                    const auto& r = data->resources[key.pose.resource.value];
                    if (r.kind == ResourceKind::Sprite)
                        n.maximum_division =
                            std::max(n.maximum_division,
                                     std::max(2, static_cast<int>((r.rect.width + r.rect.height) *
                                                                  n.maximum_density)));
                }
            require(n.maximum_division <= 256, n.label, "automatic division capacity exceeded");
            const auto cells = static_cast<std::size_t>(n.maximum_division + 2);
            data->vertex_capacity += cells * cells;
            data->index_capacity += cells * cells * 6;
        }
        require(data->vertex_capacity <= 1000000 && data->index_capacity <= 6000000, "scene",
                "geometry capacity exceeded");
        for (auto& r : data->resources)
            r.motion = nullptr;
        for (std::size_t i = 1; i < data->nodes.size(); ++i)
            if (data->nodes[i].exported_locator)
                data->locators.push_back(
                    {NodeId{static_cast<std::uint32_t>(i)}, data->nodes[i].source_label});
        result.scene = std::unique_ptr<const GeometryScene>(new GeometryScene(std::move(data)));
    }
    catch (const std::runtime_error& e)
    {
        result.errors.push_back(e.what());
    }
    return result;
}
struct GeometryInstance::Impl
{
    const GeometryScene& owner;
    const CompiledData& scene;
    std::vector<float> input;
    std::vector<double> parameters;
    std::vector<State> states;
    std::vector<GeometryPrimitive> primitives;
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawCommand> commands, mask_commands;
    std::vector<MaskDefinition> masks;
    std::vector<DrawCommand> node_commands;
    std::vector<bool> used_masks, needed_mask_sources;
    bool evaluated{false}, frame_ready{false};
    Vec2 origin{};
    float scale{1};
    Impl(const GeometryScene& scene_owner, const CompiledData& data)
        : owner(scene_owner), scene(data), input(data.variables.size() + 1),
          parameters(input.size()), states(data.nodes.size()), node_commands(data.nodes.size()),
          used_masks(data.masks.size()), needed_mask_sources(data.nodes.size())
    {
        primitives.reserve(data.nodes.size());
        vertices.reserve(data.vertex_capacity);
        indices.reserve(data.index_capacity);
        commands.reserve(data.nodes.size());
        mask_commands.reserve(data.mask_command_capacity);
        masks.resize(data.masks.size());
    }
    Vec2 transform(NodeId id, double x, double y) const noexcept
    {
        return deform(id, states[id.value].world.apply(x, y));
    }
    Vec2 deform(NodeId id, Vec2 p) const noexcept
    {
        while (id.valid())
        {
            const auto& s = states[id.value];
            if (s.transform.mesh)
                p = warp(s, p);
            id = s.curve_parent;
        }
        return p;
    }
    static Vec2 warp(const State& s, Vec2 p) noexcept
    {
        const double x = static_cast<double>(p.x) - static_cast<double>(s.domain_corner.x);
        const double y = static_cast<double>(p.y) - static_cast<double>(s.domain_corner.y);
        const Vec2 uv{static_cast<float>(x * static_cast<double>(s.domain_inverse[0]) +
                                         y * static_cast<double>(s.domain_inverse[2])),
                      static_cast<float>(x * static_cast<double>(s.domain_inverse[1]) +
                                         y * static_cast<double>(s.domain_inverse[3]))};
        return evaluate_patch(s.world_patch, uv);
    }
};
GeometryInstance::GeometryInstance(const GeometryScene& scene)
    : impl_(std::make_unique<Impl>(scene, *scene.impl_))
{
}
GeometryInstance::~GeometryInstance() = default;
const GeometryScene& GeometryInstance::scene() const noexcept
{
    return impl_->owner;
}
GeometryStatus GeometryInstance::set_parameter(ParamId id, float value) noexcept
{
    if (!id.valid() || id.value >= impl_->input.size())
        return {GeometryError::InvalidParameter, {}, id};
    if (!std::isfinite(value))
        return {GeometryError::NonfiniteParameter, {}, id};
    impl_->input[id.value] = value;
    impl_->evaluated = false;
    impl_->frame_ready = false;
    return {};
}
GeometryStatus GeometryInstance::set_origin(Vec2 origin) noexcept
{
    if (!std::isfinite(origin.x) || !std::isfinite(origin.y))
        return {GeometryError::NonfiniteParameter, {}, {}};
    impl_->origin = origin;
    impl_->evaluated = false;
    impl_->frame_ready = false;
    return {};
}
GeometryStatus GeometryInstance::set_scale(float scale) noexcept
{
    const float inverse = 1.0f / scale;
    if (!std::isfinite(scale) || scale <= 0 || !std::isfinite(inverse) || inverse <= 0)
        return {GeometryError::InvalidScale, {}, {}};
    impl_->scale = scale;
    impl_->evaluated = false;
    impl_->frame_ready = false;
    return {};
}
float GeometryInstance::scale() const noexcept
{
    return impl_->scale;
}
GeometryStatus GeometryInstance::evaluate() noexcept
{
    auto& instance = *impl_;
    instance.primitives.clear();
    instance.vertices.clear();
    instance.indices.clear();
    instance.commands.clear();
    instance.mask_commands.clear();
    std::fill(instance.masks.begin(), instance.masks.end(), MaskDefinition{});
    std::fill(instance.node_commands.begin(), instance.node_commands.end(), DrawCommand{});
    std::fill(instance.used_masks.begin(), instance.used_masks.end(), false);
    instance.evaluated = false;
    instance.frame_ready = false;
    std::copy(instance.input.begin(), instance.input.end(), instance.parameters.begin());
    const auto& scene = instance.scene;
    instance.states[0] = {};
    instance.states[0].world.translation = instance.origin;
    instance.states[0].world.a = instance.scale;
    instance.states[0].world.d = instance.scale;
    for (const auto& c : scene.clamps)
    {
        if (!c.enabled)
            continue;
        const ParameterPair input{instance.parameters[c.x.value], instance.parameters[c.y.value]};
        if (input.x == 0 && input.y == 0)
            continue;
        if ((c.type != 0 && c.type != 1) || c.begin != -c.end || c.end <= 0)
            return {GeometryError::UnsupportedClamp, {}, c.x};
        const auto output = c.type == 0 ? clamp_pair(input, c.end) : clamp_circle(input, c.end);
        instance.parameters[c.x.value] = output.x;
        instance.parameters[c.y.value] = output.y;
    }
    for (std::size_t i = 1; i < scene.nodes.size(); ++i)
    {
        const NodeId id{static_cast<std::uint32_t>(i)};
        const auto& n = scene.nodes[i];
        auto& state = instance.states[i];
        state = {};
        const auto& parent = instance.states[n.parent.value];
        if (n.parent.valid() &&
            (!parent.pose.active || (n.guard.valid() && n.guard != parent.pose.resource)))
            continue;
        state.branch_selected = true;
        float time = 0;
        if (n.binding.variable.valid() && n.binding.enabled)
        {
            const double value = instance.parameters[n.binding.variable.value];
            if (n.binding.discretization != 0 && value != std::floor(value))
                return {GeometryError::UnsupportedDiscretization, id, n.binding.variable};
            if (n.binding.end > n.binding.begin)
                time = static_cast<float>(
                    (static_cast<double>(std::clamp(value, static_cast<double>(n.binding.begin),
                                                    static_cast<double>(n.binding.end))) -
                     static_cast<double>(n.binding.begin)) *
                    static_cast<double>(n.binding.division) /
                    (static_cast<double>(n.binding.end) - static_cast<double>(n.binding.begin)));
        }
        const auto key =
            std::upper_bound(n.keys.begin(), n.keys.end(), time, [](double t, const Key& k)
                             { return t < static_cast<double>(k.time); });
        if (key == n.keys.begin())
            continue;
        const auto& left = *(key - 1);
        state.pose = key == n.keys.end() ? left.pose : interpolate(left, *key, time);
        if (!state.pose.active)
            continue;
        if (n.has_sprites && (n.unsupported || state.pose.unsupported))
            return {GeometryError::UnsupportedTransform, id, {}};
        state.transform = state.pose;
        state.child_sync = n.child_sync;
        state.transform_parent = n.parent;
        state.depth = state.pose.depth + parent.depth;
        state.opacity = state.pose.opacity * (n.parent.valid() ? parent.opacity : 1);
        const auto& r = scene.resources[state.pose.resource.value];
        state.mesh_path = state.pose.mesh || (n.inherit_shape && parent.mesh_path);
        state.density = parent.density;
        if (state.pose.mesh && r.rect.width > 0 && n.division > 0)
            state.density = std::max(state.density, static_cast<float>(n.division) /
                                                        (r.rect.width + r.rect.height));
        const auto& parent_domain = scene.resources[parent.transform.resource.value];
        if (n.combine && n.parent.valid() && same_domain(r, parent_domain))
        {
            if (state.pose.translation != Vec2{} || state.pose.angle_degrees != 0)
                return {GeometryError::UnsupportedCombination, id, {}};
            // A position-synchronized parent has already changed the helper's
            // rigid origin. Its patch must not also be added to the helper.
            // Source captures: docs/validation/p6-child-synchronization.json.
            if (parent.child_sync != 1)
            {
                state.transform = parent.transform;
                state.child_sync = parent.child_sync;
                // The source saves each sum from the current helper toward
                // its nearest parent, rather than reusing the parent aggregate.
                // Different Float32 addition order changes physical anchors.
                // See docs/validation/p6-transform-precision.json.
                state.transform.patch = state.pose.patch;
                for (NodeId ancestor = n.parent; ancestor != parent.transform_parent;
                     ancestor = scene.nodes[ancestor.value].parent)
                    add_patch_displacement(state.transform.patch,
                                           instance.states[ancestor.value].pose.patch);
                state.transform.mesh = state.transform.mesh || state.pose.mesh;
                state.transform_parent = parent.transform_parent;
            }
        }
        // Rigid parents accumulate before any curve. Curves are evaluated in
        // placed world coordinates, with saved control points and saved UVs.
        // Local deformation followed by Float32 translations changes physics
        // anchors. See docs/validation/p6-world-deformation.json.
        // Interpolate degrees first; the source saves negative-angle wrapping
        // before double trigonometry and the final Float32 matrix coefficients.
        float degrees = std::fmod(state.transform.angle_degrees, 360.0f);
        if (degrees < 0)
            degrees += 360.0f;
        const double radians = static_cast<double>(degrees) * (std::numbers::pi / 180.0);
        const float cosine = static_cast<float>(std::cos(radians));
        const float sine = static_cast<float>(std::sin(radians));
        const RigidTransform local{cosine, sine, -sine, cosine, state.transform.translation};
        const auto& transform_parent = instance.states[state.transform_parent.value];
        state.world = compose(transform_parent.world, local);
        state.curve_parent = transform_parent.child_sync == 8 ? state.transform_parent
                                                              : transform_parent.curve_parent;
        // Mask one bends the direct child origin, leaving its geometry rigid.
        // Mask eight retains the curve in the descendant vertex chain.
        if (transform_parent.child_sync == 1 && transform_parent.transform.mesh)
            state.world.translation = Impl::warp(transform_parent, state.world.translation);
        if (state.transform.mesh)
        {
            const auto& domain = scene.resources[state.transform.resource.value].rect;
            const double a = static_cast<double>(state.world.a) * static_cast<double>(domain.width);
            const double b = static_cast<double>(state.world.b) * static_cast<double>(domain.width);
            const double c =
                static_cast<double>(state.world.c) * static_cast<double>(domain.height);
            const double d =
                static_cast<double>(state.world.d) * static_cast<double>(domain.height);
            const double determinant = a * d - b * c;
            state.domain_inverse = {
                static_cast<float>(d / determinant), static_cast<float>(-b / determinant),
                static_cast<float>(-c / determinant), static_cast<float>(a / determinant)};
            const auto world_point = [&](double x, double y) noexcept
            {
                return Vec2{static_cast<float>(static_cast<double>(state.world.a) * x +
                                               static_cast<double>(state.world.c) * y +
                                               static_cast<double>(state.world.translation.x)),
                            static_cast<float>(static_cast<double>(state.world.b) * x +
                                               static_cast<double>(state.world.d) * y +
                                               static_cast<double>(state.world.translation.y))};
            };
            state.domain_corner = world_point(-static_cast<double>(domain.origin_x),
                                              -static_cast<double>(domain.origin_y));
            const double offset_x =
                -static_cast<double>(state.world.a) * static_cast<double>(domain.origin_x) -
                static_cast<double>(state.world.c) * static_cast<double>(domain.origin_y) +
                static_cast<double>(state.world.translation.x);
            const double offset_y =
                -static_cast<double>(state.world.b) * static_cast<double>(domain.origin_x) -
                static_cast<double>(state.world.d) * static_cast<double>(domain.origin_y) +
                static_cast<double>(state.world.translation.y);
            // The domain matrix is saved before transforming controls, while
            // its corner translation retains the transient double value.
            const double saved_a = static_cast<float>(a), saved_b = static_cast<float>(b);
            const double saved_c = static_cast<float>(c), saved_d = static_cast<float>(d);
            for (std::size_t j = 0; j < state.world_patch.size(); ++j)
                state.world_patch[j] = {
                    static_cast<float>(saved_a * static_cast<double>(state.transform.patch[j].x) +
                                       saved_c * static_cast<double>(state.transform.patch[j].y) +
                                       offset_x),
                    static_cast<float>(saved_b * static_cast<double>(state.transform.patch[j].x) +
                                       saved_d * static_cast<double>(state.transform.patch[j].y) +
                                       offset_y)};
        }
        int division = n.division;
        if (division == 0 && n.priority == 2 && r.kind == ResourceKind::Sprite)
            // Priority-two automatic density is the maximum active ancestor
            // division/domain-perimeter, scaled to the sprite perimeter. Each grid axis
            // has at least two cells, even when the requested division truncates to zero.
            // Verified with original PSB size boundaries and different parent domains.
            division = state.density > 0
                           ? static_cast<int>((r.rect.width + r.rect.height) * state.density)
                           : 0;
        if (n.type == 0 && r.kind == ResourceKind::Sprite && (state.opacity > 0 || n.mask_source))
            instance.primitives.push_back({id,
                                           r.texture,
                                           n.label,
                                           r.name,
                                           &r.rect,
                                           state.opacity,
                                           state.depth,
                                           division,
                                           {}});
    }
    // Transparent helpers can supply visible stencil alpha; retain only those
    // needed by a currently visible masked draw. No colour command is emitted.
    std::fill(instance.needed_mask_sources.begin(), instance.needed_mask_sources.end(), false);
    for (const auto& primitive : instance.primitives)
    {
        const auto mask = scene.nodes[primitive.node.value].mask;
        if (primitive.opacity > 0 && mask.valid())
            instance.used_masks[mask.value - 1] = true;
    }
    for (std::size_t i = 0; i < scene.masks.size(); ++i)
        if (instance.used_masks[i])
            for (const auto source : scene.masks[i].members)
                instance.needed_mask_sources[source.value] = true;
    std::erase_if(
        instance.primitives, [&](const auto& primitive)
        { return primitive.opacity <= 0 && !instance.needed_mask_sources[primitive.node.value]; });
    std::sort(instance.primitives.begin(), instance.primitives.end(),
              [](const auto& a, const auto& b)
              { return a.depth == b.depth ? a.node < b.node : a.depth < b.depth; });
    instance.evaluated = true;
    return {};
}
std::span<const GeometryPrimitive> GeometryInstance::primitives() const noexcept
{
    if (!impl_->evaluated)
        return {};
    return impl_->primitives;
}
Vec2 GeometryInstance::sample_atlas_point(NodeId node, Vec2 atlas_pixel) const noexcept
{
    assert(impl_->evaluated && node.valid() && node.value < impl_->states.size());
    const auto& state = impl_->states[node.value];
    const auto& r = impl_->scene.resources[state.pose.resource.value];
    assert(state.pose.active && r.kind == ResourceKind::Sprite);
    return impl_->transform(node,
                            static_cast<double>(atlas_pixel.x) - static_cast<double>(r.rect.left) -
                                static_cast<double>(r.rect.origin_x),
                            static_cast<double>(atlas_pixel.y) - static_cast<double>(r.rect.top) -
                                static_cast<double>(r.rect.origin_y));
}
LocatorSample GeometryInstance::sample_locator(NodeId node) const noexcept
{
    const auto& instance = *impl_;
    if (!node.valid() || node.value >= instance.scene.nodes.size() ||
        !instance.scene.nodes[node.value].exported_locator)
        return {{GeometryError::InvalidLocator, node, {}}};
    if (!instance.evaluated)
        return {{GeometryError::UnsupportedTransform, node, {}}};
    const auto& state = instance.states[node.value];
    if (!state.branch_selected)
        return {};
    // A zero-origin exported marker remains a locator when its own display
    // frame is empty. Resource-branch selection is a separate condition.
    const bool empty_origin =
        !state.pose.active && instance.scene.nodes[node.value].neutral_locator_origin;
    if (!state.pose.active && !empty_origin)
        return {{GeometryError::UnsupportedTransform, node, {}}};
    for (NodeId ancestor = node; ancestor.valid();
         ancestor = instance.scene.nodes[ancestor.value].parent)
    {
        const auto& n = instance.scene.nodes[ancestor.value];
        const auto& pose = instance.states[ancestor.value].pose;
        const bool known_partial_origin = ancestor == node && n.partial_locator_origin &&
                                          pose.translation == Vec2{} && pose.angle_degrees == 0 &&
                                          !pose.mesh;
        if ((n.unsupported && !known_partial_origin) || pose.unsupported)
            return {{GeometryError::UnsupportedTransform, ancestor, {}}};
    }
    return {
        {},
        LocatorState::Active,
        instance.transform(empty_origin ? instance.scene.nodes[node.value].parent : node, 0, 0)};
}
std::optional<GeometryNodeObservation> GeometryInstance::observe_node(NodeId id) const noexcept
{
    const auto& instance = *impl_;
    if (!instance.evaluated || !id.valid() || id.value >= instance.states.size())
        return std::nullopt;
    const auto& s = instance.states[id.value];
    GeometryNodeObservation result;
    result.parent = instance.scene.nodes[id.value].parent;
    result.transform_parent = s.transform_parent;
    result.curve_parent = s.curve_parent;
    result.active = s.pose.active;
    result.mesh = s.transform.mesh;
    result.world = {
        s.world.a, s.world.b, s.world.c, s.world.d, s.world.translation.x, s.world.translation.y};
    result.domain_corner = s.domain_corner;
    result.domain_inverse = s.domain_inverse;
    result.unit_patch = s.transform.patch;
    result.world_patch = s.world_patch;
    if (s.transform.resource.valid())
    {
        const auto& r = instance.scene.resources[s.transform.resource.value].rect;
        result.domain_size = {r.width, r.height};
        result.domain_origin = {r.origin_x, r.origin_y};
    }
    return result;
}
GeometryStatus GeometryInstance::build_frame() noexcept
{
    auto& instance = *impl_;
    instance.frame_ready = false;
    instance.vertices.clear();
    instance.indices.clear();
    instance.commands.clear();
    instance.mask_commands.clear();
    std::fill(instance.masks.begin(), instance.masks.end(), MaskDefinition{});
    std::fill(instance.node_commands.begin(), instance.node_commands.end(), DrawCommand{});
    std::fill(instance.used_masks.begin(), instance.used_masks.end(), false);
    if (!instance.evaluated)
        return {GeometryError::UnsupportedTransform, {}, {}};
    for (const auto& p : instance.primitives)
        if (p.mesh_division == 0 && instance.states[p.node.value].mesh_path &&
            instance.scene.nodes[p.node.value].priority != 2)
            return {GeometryError::UnsupportedTessellation, p.node, {}};
    for (auto& p : instance.primitives)
    {
        const auto& r = *p.rect;
        const auto [nx, ny] = instance.states[p.node.value].mesh_path ? grid(p.mesh_division, r)
                                                                      : std::pair<int, int>{1, 1};
        const auto base = static_cast<std::uint32_t>(instance.vertices.size());
        const auto first = static_cast<std::uint32_t>(instance.indices.size());
        const auto& texture = instance.scene.textures[p.texture.value - 1];
        const auto& world = instance.states[p.node.value].world;
        const double left = -static_cast<double>(r.origin_x);
        const double top = -static_cast<double>(r.origin_y);
        // Author tessellation saves the four rigid world corners first, then
        // interpolates their rows in double and saves each point before curves.
        // Directly transforming local grid points loses this Float32 boundary.
        const auto corner = [&](double x, double y) noexcept
        {
            const auto saved = world.apply(x, y);
            return std::array<double, 2>{saved.x, saved.y};
        };
        const auto top_left = corner(left, top);
        const auto top_right = corner(left + static_cast<double>(r.width), top);
        const auto bottom_left = corner(left, top + static_cast<double>(r.height));
        const auto bottom_right =
            corner(left + static_cast<double>(r.width), top + static_cast<double>(r.height));
        for (int y = 0; y <= ny; ++y)
        {
            const double row = static_cast<double>(y) / ny;
            const double row_left_x = (1 - row) * top_left[0] + row * bottom_left[0];
            const double row_left_y = (1 - row) * top_left[1] + row * bottom_left[1];
            const double row_right_x = (1 - row) * top_right[0] + row * bottom_right[0];
            const double row_right_y = (1 - row) * top_right[1] + row * bottom_right[1];
            for (int x = 0; x <= nx; ++x)
            {
                // Atlas coordinates use the original local grid ratios in
                // double, independently of the saved rigid corner interpolation.
                // UV packing has its own Float32 boundary.
                const double local_x = static_cast<double>(x) / nx * static_cast<double>(r.width);
                const double local_y = static_cast<double>(y) / ny * static_cast<double>(r.height);
                const float u = static_cast<float>(static_cast<double>(r.left) + local_x);
                // Source atlas V is saved from the lower edge before converting
                // to our top-origin UV. Reversing the save order changes the
                // texture sample along translucent edges.
                const float bottom_v = static_cast<float>(static_cast<double>(texture.height) -
                                                          static_cast<double>(r.top) - local_y);
                const float v = static_cast<float>(texture.height) - bottom_v;
                // Atlas packing affects UV only. Adding a large atlas offset and
                // subtracting it before deformation loses local subpixel precision.
                const double column = static_cast<double>(x) / nx;
                const Vec2 rigid_point{
                    static_cast<float>((1 - column) * row_left_x + column * row_right_x),
                    static_cast<float>((1 - column) * row_left_y + column * row_right_y)};
                const auto point = instance.deform(p.node, rigid_point);
                instance.vertices.push_back({point.x,
                                             point.y,
                                             u / static_cast<float>(texture.width),
                                             v / static_cast<float>(texture.height),
                                             {1, 1, 1, p.opacity}});
            }
        }
        for (int y = 0; y < ny; ++y)
            for (int x = 0; x < nx; ++x)
            {
                const auto a = base + static_cast<std::uint32_t>(y * (nx + 1) + x);
                const auto b = a + 1, c = a + static_cast<std::uint32_t>(nx + 1), d = c + 1;
                for (const auto index : {a, b, c, b, d, c})
                    instance.indices.push_back(index);
            }
        const auto mask = instance.scene.nodes[p.node.value].mask;
        if (mask.valid() && p.opacity > 0)
            instance.used_masks[mask.value - 1] = true;
        const DrawCommand command{p.texture,
                                  first,
                                  static_cast<std::uint32_t>(instance.indices.size()) - first,
                                  BlendMode::Over,
                                  r.attr == 1 ? ColorProtection::PreserveTranslucent
                                              : ColorProtection::None,
                                  mask};
        p.draw = command;
        instance.node_commands[p.node.value] = command;
        if (p.opacity > 0)
            instance.commands.push_back(command);
    }
    for (std::size_t i = 0; i < instance.scene.masks.size(); ++i)
    {
        if (!instance.used_masks[i])
            continue;
        const auto& source = instance.scene.masks[i];
        if (!source.resolved)
        {
            instance.commands.clear();
            return {GeometryError::UnsupportedMask, source.node, {}};
        }
        auto& definition = instance.masks[i];
        definition.first_command = static_cast<std::uint32_t>(instance.mask_commands.size());
        for (const auto node : source.members)
        {
            auto command = instance.node_commands[node.value];
            // Blank helpers contribute no sampled alpha. A resolved source
            // with no sprite commands remains an empty, cleared mask, as in
            // Fraise's special Eye poses; it is not an unresolved source.
            if (command.index_count == 0)
                continue;
            if (command.mask.valid())
            {
                instance.commands.clear();
                return {GeometryError::UnsupportedMask, node, {}};
            }
            command.protection = ColorProtection::None;
            instance.mask_commands.push_back(command);
        }
        definition.command_count =
            static_cast<std::uint32_t>(instance.mask_commands.size()) - definition.first_command;
    }
    instance.frame_ready = true;
    return {};
}
RenderFrame GeometryInstance::frame() const noexcept
{
    if (!impl_->frame_ready)
        return {};
    return {impl_->vertices,      impl_->indices,
            impl_->commands,      ColorComposition::ProtectedStraight,
            impl_->mask_commands, impl_->masks};
}
} // namespace nekomote::runtime
