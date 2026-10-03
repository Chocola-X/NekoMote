#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <iosfwd>

namespace nekomote {

// Strongly typed identifiers.
//
// All entity ids are wrapped in a templated Id<Tag> so distinct kinds (nodes,
// textures, parameters, instances, ...) cannot be silently mixed even though
// they share an underlying integer. The integer is 0-based; the default-constructed
// id is the invalid sentinel and valid() returns false for it. Producers must
// hand out non-zero ids; 0 is reserved as "no id".
//
// Ids are stable, opaque values: they do not encode generation counts or slots.
// Lifetime and validity tracking is the responsibility of the owning container
// (see docs/conventions.md).

template <typename Tag>
struct Id {
    using value_type = std::uint32_t;

    value_type value{0};

    constexpr Id() noexcept = default;
    explicit constexpr Id(value_type v) noexcept : value(v) {}

    constexpr bool valid() const noexcept { return value != 0; }

    constexpr bool operator==(Id other) const noexcept { return value == other.value; }
    constexpr bool operator!=(Id other) const noexcept { return value != other.value; }
    constexpr bool operator<(Id other) const noexcept { return value < other.value; }
    constexpr bool operator>(Id other) const noexcept { return value > other.value; }
};

struct NodeIdTag {};
struct TextureIdTag {};
struct ParamIdTag {};
struct InstanceIdTag {};
struct MeshIdTag {};
struct TimelineIdTag {};
struct SurfaceIdTag {};  // output surface / render target

using NodeId = Id<NodeIdTag>;
using TextureId = Id<TextureIdTag>;
using ParamId = Id<ParamIdTag>;
using InstanceId = Id<InstanceIdTag>;
using MeshId = Id<MeshIdTag>;
using TimelineId = Id<TimelineIdTag>;
using SurfaceId = Id<SurfaceIdTag>;

// Monotonic id generator. Not thread-safe; owners are single-threaded by
// contract (AGENTS.md §7: initial single-threaded evaluation).
template <typename Tag>
class IdGenerator {
public:
    using id_type = Id<Tag>;

    IdGenerator() noexcept = default;

    id_type next() noexcept {
        // Skip 0 so every issued id is valid().
        ++cursor_;
        return id_type{cursor_};
    }

    void reset() noexcept { cursor_ = 0; }

private:
    typename id_type::value_type cursor_{0};
};

}  // namespace nekomote

namespace std {

template <typename Tag>
struct hash<nekomote::Id<Tag>> {
    size_t operator()(nekomote::Id<Tag> id) const noexcept {
        return hash<typename nekomote::Id<Tag>::value_type>{}(id.value);
    }
};

}  // namespace std
