#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace nekomote::model
{

// Missing, explicit null and a numeric zero are distinct source states. Runtime
// compilation resolves these once; frame evaluation never parses source JSON.
enum class FieldState
{
    Missing,
    Null,
    Value
};

template <typename T>
struct Field
{
    FieldState state{FieldState::Missing};
    T data{};
    bool has_value() const
    {
        return state == FieldState::Value;
    }
    const T& value() const
    {
        return data;
    }
    const T& operator*() const
    {
        return data;
    }
};

struct IconRect
{
    std::string texture;
    float left{0}, top{0}, width{0}, height{0}, origin_x{0}, origin_y{0};
    int attr{0}, zorder{0};
};

struct TextureDef
{
    std::string name, path, format;
    int width{0}, height{0};
};

// A 32-component bp occurs in the current corpus. Its interpretation remains
// a candidate until reference experiments recorded in docs/semantics.md pass.
// cc has a different, unverified layout: retain every component, never truncate.
struct MeshData
{
    Field<std::array<float, 32>> bp;
    Field<std::vector<float>> cc;
};

struct MotionContent
{
    Field<std::uint64_t> mask;
    Field<float> time_offset;
};

struct FrameContent
{
    Field<std::array<float, 3>> coord;
    Field<std::string> icon, src;
    Field<std::uint64_t> mask;
    Field<float> opa, angle, zx, zy, ox, oy;
    Field<std::uint32_t> color;
    Field<int> bm, flip;
    Field<MotionContent> motion;
    Field<MeshData> mesh;
};

struct LayerFrame
{
    float time{0};
    int type{0};
    Field<FrameContent> content;
};

struct ParamBinding
{
    std::string id;
    float range_begin{0}, range_end{0};
    int division{0}, discretization{0};
    bool enabled{true};
};

struct Layer
{
    std::string label;
    int type{0}, coordinate{0}, mesh_combine{0}, mesh_division{0}, mesh_transform{0};
    int stencil_type{0}, original_index{0}, mesh_sync_child_mask{0};
    int export_self{1}, join_target{1}, ground_correction{0};
    int motion_independent_layer_inherit{0}, obj_tri_priority{0};
    Field<int> parameterize, shape;
    std::uint64_t inherit_mask{0};
    std::vector<int> transform_order;
    std::vector<std::string> stencil_composite_mask_layers;
    std::vector<ParamBinding> parameter;
    std::vector<LayerFrame> frame_list;
    std::vector<Layer> children;
};

struct PriorityFrame
{
    float time{0};
    int type{0};
    Field<std::vector<int>> content;
};

// A source object motion is a reusable subtree, not an independently drawn part.
struct Part
{
    std::string group, name;
    int type{0};
    float last_time{0}, loop_time{-1};
    Field<int> parameterize;
    std::vector<ParamBinding> parameter;
    std::vector<PriorityFrame> priority;
    std::unordered_map<std::string, int> layer_index_map;
    std::vector<Layer> layers;
};

struct VariableDef
{
    std::string label;
    std::vector<std::pair<int, std::string>> frames;
};

struct PairedClamp
{
    std::string label, variable_x, variable_y;
    float range_begin{0}, range_end{0};
    int type{0};
    bool enabled{true};
};

struct VariableKey
{
    float time{0};
    int type{0};
    Field<float> value, easing;
};

struct VariableTrack
{
    std::string label;
    std::vector<VariableKey> keys;
};

struct Timeline
{
    std::string label;
    int diff{0};
    float last_time{-1}, loop_begin{-1}, loop_end{-1};
    std::vector<VariableTrack> tracks;
};

// No filesystem, JSON, window or graphics types cross this boundary. Source
// archives and unsupported-field diagnostics are owned by the offline importer.
struct ModelAsset
{
    std::string spec;
    float version{0};
    int screen_width{0}, screen_height{0}, screen_origin_x{0}, screen_origin_y{0};
    std::string base_group, base_motion;
    std::vector<VariableDef> variables;
    std::vector<std::string> instant_variables;
    std::unordered_map<std::string, IconRect> icons; // qualified "texture/icon"
    std::vector<TextureDef> textures;
    std::vector<Part> parts;
    std::vector<Timeline> timelines;
    std::vector<PairedClamp> paired_clamps;
    std::vector<std::string> parts_order; // meaning unverified; not draw order
    int total_layers{0}, total_frames{0}, bp_nonnull{0}, cc_nonnull{0};
};

} // namespace nekomote::model
