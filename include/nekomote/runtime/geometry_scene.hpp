#pragma once

#include <nekomote/math.hpp>
#include <nekomote/model/asset.hpp>
#include <nekomote/render_frame.hpp>
#include <nekomote/runtime/deformation.hpp>

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nekomote::runtime
{
struct VariableSymbol
{
    ParamId id;
    std::string name;
};
struct TextureBinding
{
    TextureId id;
    std::string name, path;
    int width{0}, height{0};
};
struct GeometryPrimitive
{
    NodeId node;
    TextureId texture;
    std::string_view label, source;
    const model::IconRect* rect{nullptr}; // scene-owned; valid while the scene lives
    float opacity{0}, depth{0};
    int mesh_division{0};
    // Valid after successful build_frame until the next evaluation. Includes
    // opacity-zero mask sources omitted from frame.commands.
    DrawCommand draw;
};
struct GeometryLocator
{
    NodeId node;
    std::string_view name; // scene-owned source label; bind IDs at loading time
};
enum class GeometryError
{
    None,
    InvalidParameter,
    NonfiniteParameter,
    UnsupportedClamp,
    UnsupportedDiscretization,
    UnsupportedTransform,
    UnsupportedCombination,
    UnsupportedTessellation,
    UnsupportedMask,
    InvalidLocator,
    InvalidScale
};
struct GeometryStatus
{
    GeometryError error{GeometryError::None};
    NodeId node;
    ParamId parameter;
    explicit operator bool() const noexcept
    {
        return error == GeometryError::None;
    }
};
struct GeometryCapacity
{
    std::size_t vertices{0}, indices{0}, commands{0}, mask_commands{0}, masks{0};
};
enum class LocatorState
{
    Inactive,
    Active
};
struct LocatorSample
{
    GeometryStatus status;
    LocatorState state{LocatorState::Inactive};
    Vec2 position{}; // placed model space; independent of final view/projection and GPU
};
// Read-only diagnostic snapshot, copied on explicit request. Does not feed
// evaluation or expose imported JSON/GPU state. Invalid/unevaluated IDs reject.
struct GeometryNodeObservation
{
    NodeId parent, transform_parent, curve_parent;
    bool active{false}, mesh{false};
    std::array<float, 6> world{}; // a,b,c,d,translation.x,translation.y
    Vec2 domain_corner{}, domain_size{}, domain_origin{};
    std::array<float, 4> domain_inverse{};
    Patch unit_patch{}, world_patch{};
};
class GeometryInstance;
struct GeometryCompileResult;
// Diagnostic static scene, not a certified player. Source bindings/resources
// are resolved once. No source JSON or graphics dependencies enter evaluation.
class GeometryScene
{
  public:
    ~GeometryScene();
    GeometryScene(const GeometryScene&) = delete;
    GeometryScene& operator=(const GeometryScene&) = delete;
    std::span<const VariableSymbol> variables() const noexcept;
    std::span<const TextureBinding> textures() const noexcept;
    std::span<const GeometryLocator> locators() const noexcept;
    std::span<const std::string> diagnostics() const noexcept;
    ParamId find_variable(std::string_view name) const noexcept; // load/diagnostics only
    std::size_t node_count() const noexcept;
    GeometryCapacity frame_capacity() const noexcept;
    std::string_view node_label(NodeId id) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit GeometryScene(std::unique_ptr<Impl> impl);
    friend class GeometryInstance;
    friend GeometryCompileResult compile_geometry_scene(const model::ModelAsset& asset);
};
struct GeometryCompileResult
{
    std::unique_ptr<const GeometryScene> scene;
    std::vector<std::string> errors;
};
GeometryCompileResult compile_geometry_scene(const model::ModelAsset& asset);
// Scene must outlive this instance. Instances own all variable/pose/frame state;
// calls are single-threaded. Spans expire at the next evaluate/build_frame call.
class GeometryInstance
{
  public:
    explicit GeometryInstance(const GeometryScene& scene);
    ~GeometryInstance();
    GeometryInstance(const GeometryInstance&) = delete;
    GeometryInstance& operator=(const GeometryInstance&) = delete;
    const GeometryScene& scene() const noexcept;
    GeometryStatus set_parameter(ParamId id, float value) noexcept;
    // Model placement, in model units (Y down), participates in Float32 parent
    // matrix accumulation. Rendering view/projection remains a separate step.
    // Placement retains its presented direction for mirrored assets; frame X
    // is pre-inverted to cancel the later GPU flip. Changes invalidate geometry
    // and locator samples; default is zero.
    GeometryStatus set_origin(Vec2 origin) noexcept;
    // Uniform model scale participates in world matrices, independently of
    // placement and the render view. Positive finite scale/inverse required.
    GeometryStatus set_scale(float scale) noexcept;
    float scale() const noexcept;
    GeometryStatus evaluate() noexcept; // fixed variable pose, time=0; no heap allocation
    std::span<const GeometryPrimitive> primitives() const noexcept;
    // node must come from this instance's current primitives(). Samples an icon
    // in atlas pixel coordinates; useful for geometry evidence and inspection.
    Vec2 sample_atlas_point(NodeId node, Vec2 atlas_pixel) const noexcept;
    // ID must come from scene.locators(). Inactive resource branches are explicit.
    // Valid after evaluate(); never substitutes zero for unverified inheritance.
    // Supports full inheritance and verified zero-local-origin type-one markers
    // with mask 0x200020c.
    LocatorSample sample_locator(NodeId node) const noexcept;
    // Static diagnostic frame. Supports verified union masks and protected straight
    // composition; unsupported selected mask sources/nesting fail explicitly.
    // Unknown automatic tessellation explicitly fails instead of drawing a quad.
    GeometryStatus build_frame() noexcept;
    RenderFrame frame() const noexcept;
    std::optional<GeometryNodeObservation> observe_node(NodeId id) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::runtime
