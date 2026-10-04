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
};
struct Pose
{
    Vec2 translation;
    float angle{0}, depth{0}, opacity{1}; // radians; source depth; straight opacity
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
    int type{0}, division{0}, priority{0}, maximum_division{0};
    float maximum_density{0};
    bool combine{false}, unsupported{false}, has_sprites{false};
    bool exported_locator{false}, partial_locator_origin{false};
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
    return {x + 1, division - x + 1}; // cells; positive on both axes
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
        p.angle = field_or(c.angle, 0.0f) * std::numbers::pi_v<float> / 180;
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
                        field_or(c.bm, 0) != 0 ||
                        field_or(c.color, std::uint32_t{0x808080ff}) != 0x808080ff ||
                        (c.motion.has_value() && field_or(c.motion.data.time_offset, 0.0f) != 0);
        require(std::isfinite(p.translation.x) && std::isfinite(p.translation.y) &&
                    std::isfinite(p.depth) && std::isfinite(p.angle) && std::isfinite(p.opacity),
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
        require(n.type == 0 || n.type == 1 || n.type == 2 || n.type == 3 || n.type == 12, n.label,
                "unsupported layer type");
        n.division = layer.mesh_division;
        n.priority = layer.obj_tri_priority;
        n.maximum_division = n.division;
        require(n.division >= 0 && n.division <= 256, n.label, "unsupported division capacity");
        n.combine = layer.mesh_combine == 1;
        const bool unsupported_common =
            layer.coordinate != 0 || (layer.mesh_transform != 0 && layer.mesh_transform != 1) ||
            (layer.mesh_combine != 0 && layer.mesh_combine != 1) ||
            (layer.mesh_sync_child_mask != 0 && layer.mesh_sync_child_mask != 8) ||
            (!layer.transform_order.empty() &&
             layer.transform_order != std::vector<int>{0, 3, 2, 1});
        n.partial_locator_origin =
            !unsupported_common && n.exported_locator && layer.inherit_mask == 0x200020c;
        n.unsupported =
            unsupported_common || (layer.inherit_mask != 0x20007fc &&
                                   !(layer.type == 12 && layer.inherit_mask == 0x24007fc));
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
    Vec2 apply(Vec2 point) const noexcept
    {
        return {a * point.x + c * point.y + translation.x,
                b * point.x + d * point.y + translation.y};
    }
};
RigidTransform compose(const RigidTransform& parent, const RigidTransform& local) noexcept
{
    return {parent.a * local.a + parent.c * local.b, parent.b * local.a + parent.d * local.b,
            parent.a * local.c + parent.c * local.d, parent.b * local.c + parent.d * local.d,
            parent.apply(local.translation)};
}
struct State
{
    Pose pose;
    Pose transform;
    NodeId transform_parent;
    RigidTransform rigid;
    NodeId rigid_parent;
    float depth{0}, opacity{0};
    bool mesh_path{false};
    float density{0};
};
Pose interpolate(const Key& a, const Key& b, float time) noexcept
{
    Pose p = a.pose;
    if (a.type != 3 || !b.pose.active || b.time <= a.time)
        return p;
    const float t = (time - a.time) / (b.time - a.time);
    p.translation += (b.pose.translation - p.translation) * t;
    p.angle += (b.pose.angle - p.angle) * t;
    p.depth += (b.pose.depth - p.depth) * t;
    p.opacity += (b.pose.opacity - p.opacity) * t;
    for (std::size_t i = 0; i < p.patch.size(); ++i)
        p.patch[i] += (b.pose.patch[i] - p.patch[i]) * t;
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
                    textures.contains(r.texture) && r.width == std::floor(r.width) &&
                    r.height == std::floor(r.height) && (r.attr == 0 || r.attr == 1),
                name, "invalid or nonintegral sprite rectangle; source truncation is unverified");
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
            data->resources.push_back({name, ResourceKind::Motion, {}, {}, &m});
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
                    mask.members.push_back(NodeId{static_cast<std::uint32_t>(j)});
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
    std::vector<float> input, parameters;
    std::vector<State> states;
    std::vector<GeometryPrimitive> primitives;
    std::vector<RenderVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<DrawCommand> commands, mask_commands;
    std::vector<MaskDefinition> masks;
    std::vector<std::uint32_t> node_commands;
    std::vector<bool> used_masks;
    bool evaluated{false}, frame_ready{false};
    Impl(const GeometryScene& scene_owner, const CompiledData& data)
        : owner(scene_owner), scene(data), input(data.variables.size() + 1),
          parameters(input.size()), states(data.nodes.size()), node_commands(data.nodes.size()),
          used_masks(data.masks.size())
    {
        primitives.reserve(data.nodes.size());
        vertices.reserve(data.vertex_capacity);
        indices.reserve(data.index_capacity);
        commands.reserve(data.nodes.size());
        mask_commands.reserve(data.mask_command_capacity);
        masks.resize(data.masks.size());
    }
    Vec2 transform(NodeId id, Vec2 p) const noexcept
    {
        while (id.valid())
        {
            const auto& s = states[id.value];
            const auto& pose = s.transform;
            const auto& r = scene.resources[pose.resource.value];
            if (pose.mesh && r.rect.width > 0)
                p = deform_point(
                    pose.patch, {{r.rect.width, r.rect.height}, {r.rect.origin_x, r.rect.origin_y}},
                    p);
            p = s.rigid.apply(p);
            id = s.rigid_parent;
        }
        return p;
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
GeometryStatus GeometryInstance::evaluate() noexcept
{
    auto& instance = *impl_;
    instance.primitives.clear();
    instance.vertices.clear();
    instance.indices.clear();
    instance.commands.clear();
    instance.mask_commands.clear();
    std::fill(instance.masks.begin(), instance.masks.end(), MaskDefinition{});
    std::fill(instance.node_commands.begin(), instance.node_commands.end(),
              std::numeric_limits<std::uint32_t>::max());
    std::fill(instance.used_masks.begin(), instance.used_masks.end(), false);
    instance.evaluated = false;
    instance.frame_ready = false;
    std::copy(instance.input.begin(), instance.input.end(), instance.parameters.begin());
    const auto& scene = instance.scene;
    for (const auto& c : scene.clamps)
    {
        if (!c.enabled)
            continue;
        const Vec2 input{instance.parameters[c.x.value], instance.parameters[c.y.value]};
        if (input == Vec2{})
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
        float time = 0;
        if (n.binding.variable.valid() && n.binding.enabled)
        {
            const float value = instance.parameters[n.binding.variable.value];
            if (n.binding.discretization != 0 && value != std::floor(value))
                return {GeometryError::UnsupportedDiscretization, id, n.binding.variable};
            if (n.binding.end > n.binding.begin)
                time = (std::clamp(value, n.binding.begin, n.binding.end) - n.binding.begin) *
                       static_cast<float>(n.binding.division) / (n.binding.end - n.binding.begin);
        }
        const auto key = std::upper_bound(n.keys.begin(), n.keys.end(), time,
                                          [](float t, const Key& k) { return t < k.time; });
        if (key == n.keys.begin())
            continue;
        const auto& left = *(key - 1);
        state.pose = key == n.keys.end() ? left.pose : interpolate(left, *key, time);
        if (!state.pose.active)
            continue;
        if (n.has_sprites && (n.unsupported || state.pose.unsupported))
            return {GeometryError::UnsupportedTransform, id, {}};
        state.transform = state.pose;
        state.transform_parent = n.parent;
        state.depth = state.pose.depth + parent.depth;
        state.opacity = state.pose.opacity * (n.parent.valid() ? parent.opacity : 1);
        const auto& r = scene.resources[state.pose.resource.value];
        state.mesh_path = state.pose.mesh || parent.mesh_path;
        state.density = parent.density;
        if (state.pose.mesh && r.rect.width > 0 && n.division > 0)
            state.density = std::max(state.density, static_cast<float>(n.division) /
                                                        (r.rect.width + r.rect.height));
        const auto& parent_domain = scene.resources[parent.transform.resource.value];
        if (n.combine && n.parent.valid() && same_domain(r, parent_domain))
        {
            if (state.pose.translation != Vec2{} || state.pose.angle != 0)
                return {GeometryError::UnsupportedCombination, id, {}};
            state.transform = parent.transform;
            add_patch_displacement(state.transform.patch, state.pose.patch);
            state.transform.mesh = state.transform.mesh || state.pose.mesh;
            state.transform_parent = parent.transform_parent;
        }
        // A canonical identity BP still requests tessellation/density, but its
        // coordinate map is exactly neutral. Avoid polynomial roundoff shifting
        // integer pixel bounds across a mask-region floor/ceil boundary.
        if (state.transform.patch == identity_patch())
            state.transform.mesh = false;
        // Source locator observations distinguish parent-first Float32 rigid
        // accumulation from repeated point translations. Cache each rigid run
        // once; a nonlinear mesh is a boundary and must never be crossed.
        const float cosine = std::cos(state.transform.angle);
        const float sine = std::sin(state.transform.angle);
        state.rigid = {cosine, sine, -sine, cosine, state.transform.translation};
        state.rigid_parent = state.transform_parent;
        if (state.transform_parent.valid())
        {
            const auto& transform_parent = instance.states[state.transform_parent.value];
            if (!transform_parent.transform.mesh)
            {
                state.rigid = compose(transform_parent.rigid, state.rigid);
                state.rigid_parent = transform_parent.rigid_parent;
            }
        }
        int division = n.division;
        if (division == 0 && n.priority == 2 && r.kind == ResourceKind::Sprite)
            // Priority-two automatic density is the maximum active ancestor
            // division/domain-perimeter, scaled to the sprite perimeter; minimum 2.
            // Verified with original PSB size boundaries and different parent domains.
            division =
                state.density > 0
                    ? std::max(2, static_cast<int>((r.rect.width + r.rect.height) * state.density))
                    : 0;
        if (n.type == 0 && r.kind == ResourceKind::Sprite && state.opacity > 0)
            instance.primitives.push_back(
                {id, r.texture, n.label, r.name, &r.rect, state.opacity, state.depth, division});
    }
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
    return impl_->transform(node, {atlas_pixel.x - r.rect.left - r.rect.origin_x,
                                   atlas_pixel.y - r.rect.top - r.rect.origin_y});
}
LocatorSample GeometryInstance::sample_locator(NodeId node) const noexcept
{
    const auto& instance = *impl_;
    if (!node.valid() || node.value >= instance.scene.nodes.size() ||
        !instance.scene.nodes[node.value].exported_locator)
        return {{GeometryError::InvalidLocator, node, {}}};
    if (!instance.evaluated)
        return {{GeometryError::UnsupportedTransform, node, {}}};
    if (!instance.states[node.value].pose.active)
        return {};
    for (NodeId ancestor = node; ancestor.valid();
         ancestor = instance.scene.nodes[ancestor.value].parent)
    {
        const auto& n = instance.scene.nodes[ancestor.value];
        const auto& pose = instance.states[ancestor.value].pose;
        const bool known_partial_origin = ancestor == node && n.partial_locator_origin &&
                                          pose.translation == Vec2{} && pose.angle == 0 &&
                                          !pose.mesh;
        if ((n.unsupported && !known_partial_origin) || pose.unsupported)
            return {{GeometryError::UnsupportedTransform, ancestor, {}}};
    }
    return {{}, LocatorState::Active, instance.transform(node, {})};
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
    std::fill(instance.node_commands.begin(), instance.node_commands.end(),
              std::numeric_limits<std::uint32_t>::max());
    std::fill(instance.used_masks.begin(), instance.used_masks.end(), false);
    if (!instance.evaluated)
        return {GeometryError::UnsupportedTransform, {}, {}};
    for (const auto& p : instance.primitives)
        if (p.mesh_division == 0 && instance.states[p.node.value].mesh_path)
            return {GeometryError::UnsupportedTessellation, p.node, {}};
    for (const auto& p : instance.primitives)
    {
        const auto& r = *p.rect;
        const auto [nx, ny] = instance.states[p.node.value].mesh_path ? grid(p.mesh_division, r)
                                                                      : std::pair<int, int>{1, 1};
        const auto base = static_cast<std::uint32_t>(instance.vertices.size());
        const auto first = static_cast<std::uint32_t>(instance.indices.size());
        const auto& texture = instance.scene.textures[p.texture.value - 1];
        for (int y = 0; y <= ny; ++y)
            for (int x = 0; x <= nx; ++x)
            {
                const float local_x = static_cast<float>(x) / static_cast<float>(nx) * r.width;
                const float local_y = static_cast<float>(y) / static_cast<float>(ny) * r.height;
                const float u = r.left + local_x;
                const float v = r.top + local_y;
                // Atlas packing affects UV only. Adding a large atlas offset and
                // subtracting it before deformation loses local subpixel precision.
                const auto point =
                    instance.transform(p.node, {local_x - r.origin_x, local_y - r.origin_y});
                instance.vertices.push_back({point.x,
                                             point.y,
                                             u / static_cast<float>(texture.width),
                                             v / static_cast<float>(texture.height),
                                             {1, 1, 1, p.opacity}});
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
        if (mask.valid())
            instance.used_masks[mask.value - 1] = true;
        instance.node_commands[p.node.value] = static_cast<std::uint32_t>(instance.commands.size());
        instance.commands.push_back(
            {p.texture, first, static_cast<std::uint32_t>(instance.indices.size()) - first,
             BlendMode::Over,
             r.attr == 1 ? ColorProtection::PreserveTranslucent : ColorProtection::None, mask});
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
            const auto command_index = instance.node_commands[node.value];
            const auto& state = instance.states[node.value];
            const auto& resource = instance.scene.resources[state.pose.resource.value];
            if (state.pose.active && state.opacity > 0 && resource.kind == ResourceKind::Blank &&
                instance.scene.nodes[node.value].type == 0)
            {
                instance.commands.clear();
                return {GeometryError::UnsupportedMask, node, {}};
            }
            if (command_index == std::numeric_limits<std::uint32_t>::max())
                continue;
            auto command = instance.commands[command_index];
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
