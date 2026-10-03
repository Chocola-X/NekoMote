// Implementation TU for nekomote_model. Most of the core is header-only
// constexpr; this translation unit exists to verify the library compiles and
// to host any non-trivial out-of-line definitions added in later phases.

#include <nekomote/math.hpp>
#include <nekomote/types.hpp>
#include <nekomote/units.hpp>

namespace nekomote
{

// Explicit instantiation of a small helper ensures the headers are consumed by
// the compiler even when all users are header-only, guarding against drift
// between declarations and includes.
namespace detail
{
[[maybe_unused]] constexpr bool kMathHeadersCompileCheck = []() consteval
{
    Vec2 v{1.0f, 2.0f};
    Mat3 m = Mat3::translate(1.0f, 2.0f);
    Vec2 r = transform_point(m, v);
    return r.x == 2.0f && r.y == 4.0f;
}();

[[maybe_unused]] constexpr bool kUnitsHeadersCompileCheck = []() consteval
{
    Seconds s{1.0f};
    Alpha a{0.5f};
    Radians rad = degrees_to_radians(180.0f);
    return s.value == 1.0f && a.value == 0.5f && rad.value > 3.14f;
}();
} // namespace detail

} // namespace nekomote
