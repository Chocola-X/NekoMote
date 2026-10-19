#pragma once

#include <nekomote/model/asset.hpp>
#include <nlohmann/json.hpp>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>
#include <type_traits>

namespace nekomote::model
{
using Json = nlohmann::json;
template <typename T>
T checked_value(const Json& j)
{
    if constexpr (std::is_arithmetic_v<T> && !std::is_same_v<T, bool>)
    {
        if constexpr (std::is_integral_v<T>)
        {
            if (!j.is_number_integer())
                throw std::runtime_error("asset integer has wrong type");
        }
        else if (!j.is_number())
            throw std::runtime_error("asset number has wrong type");
        const long double value = j.get<long double>();
        if (!std::isfinite(value) ||
            value < static_cast<long double>(std::numeric_limits<T>::lowest()) ||
            value > static_cast<long double>(std::numeric_limits<T>::max()))
            throw std::runtime_error("asset number is out of range");
        return j.get<T>();
    }
    else if constexpr (std::is_same_v<T, std::string>)
    {
        return j.get<T>();
    }
    else if constexpr (requires { typename T::mapped_type; })
    {
        if (!j.is_object())
            throw std::runtime_error("asset map has wrong type");
        T result;
        for (auto it = j.begin(); it != j.end(); ++it)
            result.emplace(it.key(), checked_value<typename T::mapped_type>(it.value()));
        return result;
    }
    else if constexpr (requires(T x) { x.push_back(typename T::value_type{}); })
    {
        if (!j.is_array())
            throw std::runtime_error("asset array has wrong type");
        T result;
        result.reserve(j.size());
        for (const auto& v : j)
            result.push_back(checked_value<typename T::value_type>(v));
        return result;
    }
    else if constexpr (requires {
                           typename T::first_type;
                           typename T::second_type;
                       })
    {
        if (!j.is_array() || j.size() != 2)
            throw std::runtime_error("asset pair has wrong shape");
        return {checked_value<typename T::first_type>(j[0]),
                checked_value<typename T::second_type>(j[1])};
    }
    else if constexpr (requires {
                           std::tuple_size<T>::value;
                           typename T::value_type;
                       })
    {
        if (!j.is_array() || j.size() != std::tuple_size<T>::value)
            throw std::runtime_error("asset array has wrong length");
        T result;
        for (std::size_t i = 0; i < j.size(); ++i)
            result[i] = checked_value<typename T::value_type>(j[i]);
        return result;
    }
    else
        return j.get<T>();
}
template <typename T>
inline void to_json(Json& j, const Field<T>& f)
{
    j = Json{{"state", static_cast<int>(f.state)}};
    if (f.has_value())
        j["value"] = f.data;
}
template <typename T>
inline void from_json(const Json& j, Field<T>& f)
{
    const int state = j.at("state").get<int>();
    if (state < 0 || state > 2)
        throw std::runtime_error("invalid field state");
    f.state = static_cast<FieldState>(state);
    if (f.has_value())
        f.data = checked_value<T>(j.at("value"));
}
inline void to_json(Json& j, const IconRect& v)
{
    j = Json{{"texture", v.texture},   {"left", v.left},     {"top", v.top},
             {"width", v.width},       {"height", v.height}, {"origin_x", v.origin_x},
             {"origin_y", v.origin_y}, {"attr", v.attr},     {"zorder", v.zorder}};
}
inline void from_json(const Json& j, IconRect& v)
{
    v.texture = checked_value<decltype(v.texture)>(j.at("texture"));
    v.left = checked_value<decltype(v.left)>(j.at("left"));
    v.top = checked_value<decltype(v.top)>(j.at("top"));
    v.width = checked_value<decltype(v.width)>(j.at("width"));
    v.height = checked_value<decltype(v.height)>(j.at("height"));
    v.origin_x = checked_value<decltype(v.origin_x)>(j.at("origin_x"));
    v.origin_y = checked_value<decltype(v.origin_y)>(j.at("origin_y"));
    v.attr = checked_value<decltype(v.attr)>(j.at("attr"));
    v.zorder = checked_value<decltype(v.zorder)>(j.at("zorder"));
}

inline void to_json(Json& j, const TextureDef& v)
{
    j = Json{{"name", v.name},
             {"path", v.path},
             {"format", v.format},
             {"width", v.width},
             {"height", v.height}};
}
inline void from_json(const Json& j, TextureDef& v)
{
    v.name = checked_value<decltype(v.name)>(j.at("name"));
    v.path = checked_value<decltype(v.path)>(j.at("path"));
    v.format = checked_value<decltype(v.format)>(j.at("format"));
    v.width = checked_value<decltype(v.width)>(j.at("width"));
    v.height = checked_value<decltype(v.height)>(j.at("height"));
}

inline void to_json(Json& j, const MeshData& v)
{
    j = Json{{"bp", v.bp}, {"cc", v.cc}};
}
inline void from_json(const Json& j, MeshData& v)
{
    v.bp = checked_value<decltype(v.bp)>(j.at("bp"));
    v.cc = checked_value<decltype(v.cc)>(j.at("cc"));
}

inline void to_json(Json& j, const MotionContent& v)
{
    j = Json{{"mask", v.mask}, {"time_offset", v.time_offset}};
}
inline void from_json(const Json& j, MotionContent& v)
{
    v.mask = checked_value<decltype(v.mask)>(j.at("mask"));
    v.time_offset = checked_value<decltype(v.time_offset)>(j.at("time_offset"));
}

inline void to_json(Json& j, const FrameContent& v)
{
    j = Json{{"coord", v.coord}, {"icon", v.icon},     {"src", v.src},     {"mask", v.mask},
             {"opa", v.opa},     {"angle", v.angle},   {"zx", v.zx},       {"zy", v.zy},
             {"ox", v.ox},       {"oy", v.oy},         {"color", v.color}, {"bm", v.bm},
             {"flip", v.flip},   {"motion", v.motion}, {"mesh", v.mesh}};
}
inline void from_json(const Json& j, FrameContent& v)
{
    v.coord = checked_value<decltype(v.coord)>(j.at("coord"));
    v.icon = checked_value<decltype(v.icon)>(j.at("icon"));
    v.src = checked_value<decltype(v.src)>(j.at("src"));
    v.mask = checked_value<decltype(v.mask)>(j.at("mask"));
    v.opa = checked_value<decltype(v.opa)>(j.at("opa"));
    v.angle = checked_value<decltype(v.angle)>(j.at("angle"));
    v.zx = checked_value<decltype(v.zx)>(j.at("zx"));
    v.zy = checked_value<decltype(v.zy)>(j.at("zy"));
    v.ox = checked_value<decltype(v.ox)>(j.at("ox"));
    v.oy = checked_value<decltype(v.oy)>(j.at("oy"));
    v.color = checked_value<decltype(v.color)>(j.at("color"));
    v.bm = checked_value<decltype(v.bm)>(j.at("bm"));
    v.flip = checked_value<decltype(v.flip)>(j.at("flip"));
    v.motion = checked_value<decltype(v.motion)>(j.at("motion"));
    v.mesh = checked_value<decltype(v.mesh)>(j.at("mesh"));
}

inline void to_json(Json& j, const LayerFrame& v)
{
    j = Json{{"time", v.time}, {"type", v.type}, {"content", v.content}};
}
inline void from_json(const Json& j, LayerFrame& v)
{
    v.time = checked_value<decltype(v.time)>(j.at("time"));
    v.type = checked_value<decltype(v.type)>(j.at("type"));
    v.content = checked_value<decltype(v.content)>(j.at("content"));
}

inline void to_json(Json& j, const ParamBinding& v)
{
    j = Json{{"id", v.id},
             {"range_begin", v.range_begin},
             {"range_end", v.range_end},
             {"division", v.division},
             {"discretization", v.discretization},
             {"enabled", v.enabled}};
}
inline void from_json(const Json& j, ParamBinding& v)
{
    v.id = checked_value<decltype(v.id)>(j.at("id"));
    v.range_begin = checked_value<decltype(v.range_begin)>(j.at("range_begin"));
    v.range_end = checked_value<decltype(v.range_end)>(j.at("range_end"));
    v.division = checked_value<decltype(v.division)>(j.at("division"));
    v.discretization = checked_value<decltype(v.discretization)>(j.at("discretization"));
    v.enabled = checked_value<decltype(v.enabled)>(j.at("enabled"));
}

inline void to_json(Json& j, const Layer& v)
{
    j = Json{{"label", v.label},
             {"type", v.type},
             {"coordinate", v.coordinate},
             {"mesh_combine", v.mesh_combine},
             {"mesh_division", v.mesh_division},
             {"mesh_transform", v.mesh_transform},
             {"stencil_type", v.stencil_type},
             {"original_index", v.original_index},
             {"mesh_sync_child_mask", v.mesh_sync_child_mask},
             {"export_self", v.export_self},
             {"join_target", v.join_target},
             {"ground_correction", v.ground_correction},
             {"motion_independent_layer_inherit", v.motion_independent_layer_inherit},
             {"obj_tri_priority", v.obj_tri_priority},
             {"parameterize", v.parameterize},
             {"shape", v.shape},
             {"inherit_mask", v.inherit_mask},
             {"transform_order", v.transform_order},
             {"stencil_composite_mask_layers", v.stencil_composite_mask_layers},
             {"parameter", v.parameter},
             {"frame_list", v.frame_list},
             {"children", v.children}};
}
inline void from_json(const Json& j, Layer& v)
{
    v.label = checked_value<decltype(v.label)>(j.at("label"));
    v.type = checked_value<decltype(v.type)>(j.at("type"));
    v.coordinate = checked_value<decltype(v.coordinate)>(j.at("coordinate"));
    v.mesh_combine = checked_value<decltype(v.mesh_combine)>(j.at("mesh_combine"));
    v.mesh_division = checked_value<decltype(v.mesh_division)>(j.at("mesh_division"));
    v.mesh_transform = checked_value<decltype(v.mesh_transform)>(j.at("mesh_transform"));
    v.stencil_type = checked_value<decltype(v.stencil_type)>(j.at("stencil_type"));
    v.original_index = checked_value<decltype(v.original_index)>(j.at("original_index"));
    v.mesh_sync_child_mask =
        checked_value<decltype(v.mesh_sync_child_mask)>(j.at("mesh_sync_child_mask"));
    v.export_self = checked_value<decltype(v.export_self)>(j.at("export_self"));
    v.join_target = checked_value<decltype(v.join_target)>(j.at("join_target"));
    v.ground_correction = checked_value<decltype(v.ground_correction)>(j.at("ground_correction"));
    v.motion_independent_layer_inherit =
        checked_value<decltype(v.motion_independent_layer_inherit)>(
            j.at("motion_independent_layer_inherit"));
    v.obj_tri_priority = checked_value<decltype(v.obj_tri_priority)>(j.at("obj_tri_priority"));
    v.parameterize = checked_value<decltype(v.parameterize)>(j.at("parameterize"));
    v.shape = checked_value<decltype(v.shape)>(j.at("shape"));
    v.inherit_mask = checked_value<decltype(v.inherit_mask)>(j.at("inherit_mask"));
    v.transform_order = checked_value<decltype(v.transform_order)>(j.at("transform_order"));
    v.stencil_composite_mask_layers = checked_value<decltype(v.stencil_composite_mask_layers)>(
        j.at("stencil_composite_mask_layers"));
    v.parameter = checked_value<decltype(v.parameter)>(j.at("parameter"));
    v.frame_list = checked_value<decltype(v.frame_list)>(j.at("frame_list"));
    v.children = checked_value<decltype(v.children)>(j.at("children"));
}

inline void to_json(Json& j, const PriorityFrame& v)
{
    j = Json{{"time", v.time}, {"type", v.type}, {"content", v.content}};
}
inline void from_json(const Json& j, PriorityFrame& v)
{
    v.time = checked_value<decltype(v.time)>(j.at("time"));
    v.type = checked_value<decltype(v.type)>(j.at("type"));
    v.content = checked_value<decltype(v.content)>(j.at("content"));
}

inline void to_json(Json& j, const Part& v)
{
    j = Json{{"group", v.group},
             {"name", v.name},
             {"type", v.type},
             {"last_time", v.last_time},
             {"loop_time", v.loop_time},
             {"parameterize", v.parameterize},
             {"parameter", v.parameter},
             {"priority", v.priority},
             {"layer_index_map", v.layer_index_map},
             {"layers", v.layers}};
}
inline void from_json(const Json& j, Part& v)
{
    v.group = checked_value<decltype(v.group)>(j.at("group"));
    v.name = checked_value<decltype(v.name)>(j.at("name"));
    v.type = checked_value<decltype(v.type)>(j.at("type"));
    v.last_time = checked_value<decltype(v.last_time)>(j.at("last_time"));
    v.loop_time = checked_value<decltype(v.loop_time)>(j.at("loop_time"));
    v.parameterize = checked_value<decltype(v.parameterize)>(j.at("parameterize"));
    v.parameter = checked_value<decltype(v.parameter)>(j.at("parameter"));
    v.priority = checked_value<decltype(v.priority)>(j.at("priority"));
    v.layer_index_map = checked_value<decltype(v.layer_index_map)>(j.at("layer_index_map"));
    v.layers = checked_value<decltype(v.layers)>(j.at("layers"));
}

inline void to_json(Json& j, const VariableDef& v)
{
    j = Json{{"label", v.label}, {"frames", v.frames}};
}
inline void from_json(const Json& j, VariableDef& v)
{
    v.label = checked_value<decltype(v.label)>(j.at("label"));
    v.frames = checked_value<decltype(v.frames)>(j.at("frames"));
}

inline void to_json(Json& j, const PairedClamp& v)
{
    j = Json{{"label", v.label},           {"variable_x", v.variable_x},
             {"variable_y", v.variable_y}, {"range_begin", v.range_begin},
             {"range_end", v.range_end},   {"type", v.type},
             {"enabled", v.enabled}};
}
inline void from_json(const Json& j, PairedClamp& v)
{
    v.label = checked_value<decltype(v.label)>(j.at("label"));
    v.variable_x = checked_value<decltype(v.variable_x)>(j.at("variable_x"));
    v.variable_y = checked_value<decltype(v.variable_y)>(j.at("variable_y"));
    v.range_begin = checked_value<decltype(v.range_begin)>(j.at("range_begin"));
    v.range_end = checked_value<decltype(v.range_end)>(j.at("range_end"));
    v.type = checked_value<decltype(v.type)>(j.at("type"));
    v.enabled = checked_value<decltype(v.enabled)>(j.at("enabled"));
}

inline void to_json(Json& j, const VariableKey& v)
{
    j = Json{{"time", v.time}, {"type", v.type}, {"value", v.value}, {"easing", v.easing}};
}
inline void from_json(const Json& j, VariableKey& v)
{
    v.time = checked_value<decltype(v.time)>(j.at("time"));
    v.type = checked_value<decltype(v.type)>(j.at("type"));
    v.value = checked_value<decltype(v.value)>(j.at("value"));
    v.easing = checked_value<decltype(v.easing)>(j.at("easing"));
}

inline void to_json(Json& j, const VariableTrack& v)
{
    j = Json{{"label", v.label}, {"keys", v.keys}};
}
inline void from_json(const Json& j, VariableTrack& v)
{
    v.label = checked_value<decltype(v.label)>(j.at("label"));
    v.keys = checked_value<decltype(v.keys)>(j.at("keys"));
}

inline void to_json(Json& j, const Timeline& v)
{
    j = Json{{"label", v.label},           {"diff", v.diff},         {"last_time", v.last_time},
             {"loop_begin", v.loop_begin}, {"loop_end", v.loop_end}, {"tracks", v.tracks}};
}
inline void from_json(const Json& j, Timeline& v)
{
    v.label = checked_value<decltype(v.label)>(j.at("label"));
    v.diff = checked_value<decltype(v.diff)>(j.at("diff"));
    v.last_time = checked_value<decltype(v.last_time)>(j.at("last_time"));
    v.loop_begin = checked_value<decltype(v.loop_begin)>(j.at("loop_begin"));
    v.loop_end = checked_value<decltype(v.loop_end)>(j.at("loop_end"));
    v.tracks = checked_value<decltype(v.tracks)>(j.at("tracks"));
}

inline void to_json(Json& j, const SelectorOption& v)
{
    j = Json{{"variable", v.variable}, {"on_value", v.on_value}, {"off_value", v.off_value}};
}
inline void from_json(const Json& j, SelectorOption& v)
{
    v.variable = checked_value<decltype(v.variable)>(j.at("variable"));
    v.on_value = checked_value<decltype(v.on_value)>(j.at("on_value"));
    v.off_value = checked_value<decltype(v.off_value)>(j.at("off_value"));
}
inline void to_json(Json& j, const VariableControl& v)
{
    j = Json{{"kind", static_cast<int>(v.kind)},
             {"variable", v.variable},
             {"talk_variable", v.talk_variable},
             {"enabled", v.enabled},
             {"blink_enabled", v.blink_enabled},
             {"begin_frame", v.begin_frame},
             {"end_frame", v.end_frame},
             {"blink_frames", v.blink_frames},
             {"blink_interval_min", v.blink_interval_min},
             {"blink_interval_max", v.blink_interval_max},
             {"edges", v.edges},
             {"junctions", v.junctions},
             {"options", v.options}};
}
inline void from_json(const Json& j, VariableControl& v)
{
    const int kind = checked_value<int>(j.at("kind"));
    if (kind < 0 || kind > static_cast<int>(VariableControlKind::Selector))
        throw std::runtime_error("invalid variable controller kind");
    v.kind = static_cast<VariableControlKind>(kind);
    v.variable = checked_value<decltype(v.variable)>(j.at("variable"));
    v.talk_variable = checked_value<decltype(v.talk_variable)>(j.at("talk_variable"));
    v.enabled = checked_value<decltype(v.enabled)>(j.at("enabled"));
    v.blink_enabled = checked_value<decltype(v.blink_enabled)>(j.at("blink_enabled"));
    v.begin_frame = checked_value<decltype(v.begin_frame)>(j.at("begin_frame"));
    v.end_frame = checked_value<decltype(v.end_frame)>(j.at("end_frame"));
    v.blink_frames = checked_value<decltype(v.blink_frames)>(j.at("blink_frames"));
    v.blink_interval_min =
        checked_value<decltype(v.blink_interval_min)>(j.at("blink_interval_min"));
    v.blink_interval_max =
        checked_value<decltype(v.blink_interval_max)>(j.at("blink_interval_max"));
    v.edges = checked_value<decltype(v.edges)>(j.at("edges"));
    v.junctions = checked_value<decltype(v.junctions)>(j.at("junctions"));
    v.options = checked_value<decltype(v.options)>(j.at("options"));
}

inline void to_json(Json& j, const PhysicsBinding& v)
{
    j = Json{{"source_path", v.source_path}, {"label", v.label},
             {"anchor", v.anchor},           {"preset", v.preset},
             {"variable_x", v.variable_x},   {"variable_y", v.variable_y},
             {"enabled", v.enabled},         {"unsupported_fields", v.unsupported_fields}};
}
inline void from_json(const Json& j, PhysicsBinding& v)
{
    v.source_path = checked_value<decltype(v.source_path)>(j.at("source_path"));
    v.label = checked_value<decltype(v.label)>(j.at("label"));
    v.anchor = checked_value<decltype(v.anchor)>(j.at("anchor"));
    v.preset = checked_value<decltype(v.preset)>(j.at("preset"));
    v.variable_x = checked_value<decltype(v.variable_x)>(j.at("variable_x"));
    v.variable_y = checked_value<decltype(v.variable_y)>(j.at("variable_y"));
    v.enabled = checked_value<decltype(v.enabled)>(j.at("enabled"));
    v.unsupported_fields =
        checked_value<decltype(v.unsupported_fields)>(j.at("unsupported_fields"));
}
inline void to_json(Json& j, const SpringControl& v)
{
    j = Json{{"binding", v.binding},
             {"stiffness", v.stiffness},
             {"friction", v.friction},
             {"gravity", v.gravity},
             {"axis_scale", v.axis_scale},
             {"vertical_offset", v.vertical_offset},
             {"position", v.position},
             {"velocity", v.velocity},
             {"previous_anchor", v.previous_anchor}};
}
inline void from_json(const Json& j, SpringControl& v)
{
    v.binding = checked_value<decltype(v.binding)>(j.at("binding"));
    v.stiffness = checked_value<decltype(v.stiffness)>(j.at("stiffness"));
    v.friction = checked_value<decltype(v.friction)>(j.at("friction"));
    v.gravity = checked_value<decltype(v.gravity)>(j.at("gravity"));
    v.axis_scale = checked_value<decltype(v.axis_scale)>(j.at("axis_scale"));
    v.vertical_offset = checked_value<decltype(v.vertical_offset)>(j.at("vertical_offset"));
    v.position = checked_value<decltype(v.position)>(j.at("position"));
    v.velocity = checked_value<decltype(v.velocity)>(j.at("velocity"));
    v.previous_anchor = checked_value<decltype(v.previous_anchor)>(j.at("previous_anchor"));
}
inline void to_json(Json& j, const ChainControl& v)
{
    j = Json{{"kind", static_cast<int>(v.kind)},
             {"binding", v.binding},
             {"variable_middle", v.variable_middle},
             {"stretch_stiffness", v.stretch_stiffness},
             {"gravity", v.gravity},
             {"normal_velocity_damping", v.normal_velocity_damping},
             {"friction", v.friction},
             {"length", v.length},
             {"scale_x", v.scale_x},
             {"scale_y", v.scale_y},
             {"bend_speed", v.bend_speed},
             {"bend_volume", v.bend_volume},
             {"vertical_effect", v.vertical_effect},
             {"rest_position", v.rest_position},
             {"position", v.position},
             {"velocity", v.velocity},
             {"previous_anchor", v.previous_anchor},
             {"vertical_offset", v.vertical_offset},
             {"bend_phase", v.bend_phase},
             {"bend_gain", v.bend_gain}};
}
inline void from_json(const Json& j, ChainControl& v)
{
    const auto kind = checked_value<int>(j.at("kind"));
    if (kind < 0 || kind > static_cast<int>(ChainKind::Parts))
        throw std::runtime_error("invalid physical chain kind");
    v.kind = static_cast<ChainKind>(kind);
    v.binding = checked_value<decltype(v.binding)>(j.at("binding"));
    v.variable_middle = checked_value<decltype(v.variable_middle)>(j.at("variable_middle"));
    v.stretch_stiffness = checked_value<decltype(v.stretch_stiffness)>(j.at("stretch_stiffness"));
    v.gravity = checked_value<decltype(v.gravity)>(j.at("gravity"));
    v.normal_velocity_damping =
        checked_value<decltype(v.normal_velocity_damping)>(j.at("normal_velocity_damping"));
    v.friction = checked_value<decltype(v.friction)>(j.at("friction"));
    v.length = checked_value<decltype(v.length)>(j.at("length"));
    v.scale_x = checked_value<decltype(v.scale_x)>(j.at("scale_x"));
    v.scale_y = checked_value<decltype(v.scale_y)>(j.at("scale_y"));
    v.bend_speed = checked_value<decltype(v.bend_speed)>(j.at("bend_speed"));
    v.bend_volume = checked_value<decltype(v.bend_volume)>(j.at("bend_volume"));
    v.vertical_effect = checked_value<decltype(v.vertical_effect)>(j.at("vertical_effect"));
    v.rest_position = checked_value<decltype(v.rest_position)>(j.at("rest_position"));
    v.position = checked_value<decltype(v.position)>(j.at("position"));
    v.velocity = checked_value<decltype(v.velocity)>(j.at("velocity"));
    v.previous_anchor = checked_value<decltype(v.previous_anchor)>(j.at("previous_anchor"));
    v.vertical_offset = checked_value<decltype(v.vertical_offset)>(j.at("vertical_offset"));
    v.bend_phase = checked_value<decltype(v.bend_phase)>(j.at("bend_phase"));
    v.bend_gain = checked_value<decltype(v.bend_gain)>(j.at("bend_gain"));
}

inline void to_json(Json& j, const ModelAsset& v)
{
    j = Json{{"spec", v.spec},
             {"version", v.version},
             {"screen_width", v.screen_width},
             {"screen_height", v.screen_height},
             {"screen_origin_x", v.screen_origin_x},
             {"screen_origin_y", v.screen_origin_y},
             {"base_group", v.base_group},
             {"base_motion", v.base_motion},
             {"variables", v.variables},
             {"instant_variables", v.instant_variables},
             {"icons", v.icons},
             {"textures", v.textures},
             {"parts", v.parts},
             {"timelines", v.timelines},
             {"variable_controls", v.variable_controls},
             {"paired_clamps", v.paired_clamps},
             {"author_scale", v.author_scale},
             {"spring_controls", v.spring_controls},
             {"chain_controls", v.chain_controls},
             {"parts_order", v.parts_order},
             {"total_layers", v.total_layers},
             {"total_frames", v.total_frames},
             {"bp_nonnull", v.bp_nonnull},
             {"cc_nonnull", v.cc_nonnull},
             {"mirror", v.mirror},
             {"mirrored_parameters", v.mirrored_parameters}};
}
inline void from_json(const Json& j, ModelAsset& v)
{
    v.spec = checked_value<decltype(v.spec)>(j.at("spec"));
    v.version = checked_value<decltype(v.version)>(j.at("version"));
    v.screen_width = checked_value<decltype(v.screen_width)>(j.at("screen_width"));
    v.screen_height = checked_value<decltype(v.screen_height)>(j.at("screen_height"));
    v.screen_origin_x = checked_value<decltype(v.screen_origin_x)>(j.at("screen_origin_x"));
    v.screen_origin_y = checked_value<decltype(v.screen_origin_y)>(j.at("screen_origin_y"));
    v.base_group = checked_value<decltype(v.base_group)>(j.at("base_group"));
    v.base_motion = checked_value<decltype(v.base_motion)>(j.at("base_motion"));
    v.variables = checked_value<decltype(v.variables)>(j.at("variables"));
    v.instant_variables = checked_value<decltype(v.instant_variables)>(j.at("instant_variables"));
    v.icons = checked_value<decltype(v.icons)>(j.at("icons"));
    v.textures = checked_value<decltype(v.textures)>(j.at("textures"));
    v.parts = checked_value<decltype(v.parts)>(j.at("parts"));
    v.timelines = checked_value<decltype(v.timelines)>(j.at("timelines"));
    v.variable_controls = checked_value<decltype(v.variable_controls)>(j.at("variable_controls"));
    v.paired_clamps = checked_value<decltype(v.paired_clamps)>(j.at("paired_clamps"));
    v.author_scale = checked_value<decltype(v.author_scale)>(j.at("author_scale"));
    v.spring_controls = checked_value<decltype(v.spring_controls)>(j.at("spring_controls"));
    v.chain_controls = checked_value<decltype(v.chain_controls)>(j.at("chain_controls"));

    v.parts_order = checked_value<decltype(v.parts_order)>(j.at("parts_order"));
    v.total_layers = checked_value<decltype(v.total_layers)>(j.at("total_layers"));
    v.total_frames = checked_value<decltype(v.total_frames)>(j.at("total_frames"));
    v.bp_nonnull = checked_value<decltype(v.bp_nonnull)>(j.at("bp_nonnull"));
    v.cc_nonnull = checked_value<decltype(v.cc_nonnull)>(j.at("cc_nonnull"));
    v.mirror = checked_value<bool>(j.at("mirror"));
    v.mirrored_parameters =
        checked_value<decltype(v.mirrored_parameters)>(j.at("mirrored_parameters"));
}

} // namespace nekomote::model
