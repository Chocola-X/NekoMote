#pragma once

#include <nekomote/runtime/geometry_scene.hpp>
#include <nekomote/runtime/parameter.hpp>
#include <nekomote/runtime/spring.hpp>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace nekomote::runtime
{
enum class PhysicsError
{
    None,
    InvalidInput,
    ForeignScene,
    InvalidAnchor,
    InactiveAnchor,
    AmbiguousAnchor,
    NonfiniteState
};
inline constexpr std::size_t kNoPhysicsController = std::numeric_limits<std::size_t>::max();
struct PhysicsStatus
{
    PhysicsError error{PhysicsError::None};
    std::size_t controller{kNoPhysicsController};
    GeometryStatus geometry;
    explicit operator bool() const noexcept
    {
        return error == PhysicsError::None;
    }
};
struct PhysicsInputs
{
    // SDK amplitude is an output parameter, not an enable flag. External
    // displacement uses author units in the presented model direction (before
    // View projection). Mirroring flips geometry locators into this space, not
    // the supplied displacement or stored physical snapshots.
    // Placed scene anchors use the saved inverse
    // combined model/author scale. Springs subtract saved absolute anchors;
    // chains use absolute anchors plus their initial/force offset. The first
    // author tick initializes the anchor and skips external displacement.
    float bust_amplitude{1};
    Vec2 bust_displacement{};
    float hair_amplitude{1};
    Vec2 hair_displacement{};
    float parts_amplitude{1};
    Vec2 parts_displacement{};
};
class PhysicsInstance;
struct PhysicsCompileResult;
// Compiles enabled physical controllers and binds scene IDs once. Currently
// supports verified springs and vertical_effect=0 two-particle chains. Chains
// without an exported anchor use a fixed zero anchor, as observed in source
// models. Existing anchors require one active branch. Ambiguous anchors reject.
// Scene must outlive this program and all instances. Source assets may unload.
class PhysicsProgram
{
  public:
    ~PhysicsProgram();
    PhysicsProgram(const PhysicsProgram&) = delete;
    PhysicsProgram& operator=(const PhysicsProgram&) = delete;
    std::size_t controller_count() const noexcept;
    std::string_view controller_source(std::size_t index) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    explicit PhysicsProgram(std::unique_ptr<Impl> impl);
    friend class PhysicsInstance;
    friend PhysicsCompileResult compile_physics(const model::ModelAsset&, const GeometryScene&);
};
struct PhysicsCompileResult
{
    std::unique_ptr<const PhysicsProgram> program;
    std::vector<std::string> errors;
};
struct SpringObservation
{
    SpringState state;
    Vec2 anchor;
};
PhysicsCompileResult compile_physics(const model::ModelAsset& asset, const GeometryScene& scene);
// Instance owns all integrator/anchor/output state, program must outlive it.
// Caller advances animation, binds parameters and evaluates anchors first, then
// calls step once per author tick. All anchors are sampled before any state is
// advanced. Apply values() afterward and reevaluate geometry for drawing.
// Failed steps leave states/tick unchanged and expose no stale output. Single
// thread; no frame allocations, string lookup, I/O or graph mutation.
class PhysicsInstance
{
  public:
    explicit PhysicsInstance(const PhysicsProgram& program);
    ~PhysicsInstance();
    PhysicsInstance(const PhysicsInstance&) = delete;
    PhysicsInstance& operator=(const PhysicsInstance&) = delete;
    // Optional interaction response is a signed normalized screen-direction pose
    // on each axis, in [-1, 1]. It blends bound outputs toward their authored
    // parameter endpoints after source integration, without changing its state.
    // Zero preserves source output exactly. Unreferenced outputs and ranges
    // whose intersection does not contain zero retain their source output.
    PhysicsStatus step(const GeometryInstance& anchors, PhysicsInputs inputs = {},
                       Vec2 drag_response = {}) noexcept;
    void reset() noexcept;
    std::span<const ParameterValue> values() const noexcept;
    std::uint64_t tick_count() const noexcept;
    std::optional<SpringObservation> observe_spring(std::size_t index) const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace nekomote::runtime
