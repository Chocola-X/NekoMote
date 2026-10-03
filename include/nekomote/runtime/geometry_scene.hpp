#pragma once

#include <nekomote/math.hpp>
#include <nekomote/model/asset.hpp>
#include <nekomote/render_frame.hpp>

#include <cstddef>
#include <memory>
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
    UnsupportedMask
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
    std::span<const std::string> diagnostics() const noexcept;
    ParamId find_variable(std::string_view name) const noexcept; // load/diagnostics only
    std::size_t node_count() const noexcept;
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
    GeometryStatus set_parameter(ParamId id, float value) noexcept;
    GeometryStatus evaluate() noexcept; // fixed variable pose, time=0; no heap allocation
    std::span<const GeometryPrimitive> primitives() const noexcept;
    // node must come from this instance's current primitives(). Samples an icon
    // in atlas pixel coordinates; useful for geometry evidence and inspection.
    Vec2 sample_atlas_point(NodeId node, Vec2 atlas_pixel) const noexcept;
    // Static diagnostic frame. Supports verified union masks and protected straight
    // composition; unsupported selected mask sources/nesting fail explicitly.
    // Unknown automatic tessellation explicitly fails instead of drawing a quad.
    GeometryStatus build_frame() noexcept;
    RenderFrame frame() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::runtime
