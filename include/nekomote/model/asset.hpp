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

// Controller rules are asset data, not inferred from a variable's name or its
// editor marker list. Expression edge/node topology routes value transitions.
enum class VariableControlKind
{
    Linear,
    Immediate,
    Expression,
    Eye,
    Mouth,
    Selector
};
struct SelectorOption
{
    std::string variable;
    float on_value{0}, off_value{0};
};
struct VariableControl
{
    VariableControlKind kind{VariableControlKind::Linear};
    std::string variable, talk_variable;
    bool enabled{true}, blink_enabled{false};
    float begin_frame{0}, end_frame{0};
    float blink_frames{0}, blink_interval_min{0}, blink_interval_max{0};
    std::vector<std::array<float, 2>> edges;
    std::vector<std::vector<float>> junctions;
    std::vector<SelectorOption> options;
};

// Offline-normalized physical data. Preserving it does not certify the solver.
// All coordinates, lengths and saved vectors use author units (Y down). Source
// z components and previous anchors are retained until their semantics close.
struct PhysicsBinding
{
    std::string source_path, label, anchor, preset, variable_x, variable_y;
    bool enabled{true};
    std::vector<std::string> unsupported_fields;
};
struct SpringControl
{
    PhysicsBinding binding;
    float stiffness{0}, friction{0}, gravity{0};
    std::array<float, 2> axis_scale{};
    float vertical_offset{0};
    std::array<float, 3> position{}, velocity{}, previous_anchor{};
};
enum class ChainKind
{
    Hair,
    Parts
};
struct ChainControl
{
    PhysicsBinding binding;
    ChainKind kind{ChainKind::Hair};
    std::string variable_middle;
    float stretch_stiffness{0}, gravity{0}, normal_velocity_damping{0};
    std::array<float, 2> friction{}, length{}, scale_x{}, scale_y{};
    float bend_speed{0}, bend_volume{0};
    int vertical_effect{0};
    std::array<std::array<float, 3>, 2> rest_position{}, position{}, velocity{};
    std::array<float, 3> previous_anchor{};
    float vertical_offset{0}, bend_phase{0}, bend_gain{0};
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
    std::vector<VariableControl> variable_controls;
    std::vector<PairedClamp> paired_clamps;
    Field<float> author_scale;
    std::vector<SpringControl> spring_controls;
    std::vector<ChainControl> chain_controls;
    std::vector<std::string> parts_order; // meaning unverified; not draw order
    int total_layers{0}, total_frames{0}, bp_nonnull{0}, cc_nonnull{0};
};

} // namespace nekomote::model
