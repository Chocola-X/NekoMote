#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/deformation.hpp>

#include <array>
#include <cmath>

namespace
{
void near(nekomote::Vec2 actual, nekomote::Vec2 expected, float tolerance = 0.0001f)
{
    REQUIRE_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, tolerance));
    REQUIRE_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, tolerance));
}
} // namespace
TEST_CASE("Patch domain origins and extrapolated points retain model coordinates", "[runtime]")
{
    using namespace nekomote::runtime;
    const PatchDomain domain{{128, 64}, {64, 16}};
    const auto neutral = identity_patch();
    near(deform_point(neutral, domain, {-96, 64}), {-96, 64});
    near(deform_point(neutral, domain, {96, -32}), {96, -32});
    auto curve = neutral;
    curve[0].x += 0.64f;
    // The top-left control has weight 1/64 at the domain center.
    near(deform_point(curve, domain, {0, 16}), {1.28f, 16});
    near(deform_point(curve, domain, {-64, -16}), {17.92f, -16});
    auto helper = neutral;
    for (auto& point : helper)
        point.y += 0.25f;
    add_patch_displacement(curve, helper);
    near(deform_point(curve, domain, {0, 16}), {1.28f, 32});
}
TEST_CASE("Paired control matches original WebGL position observations", "[runtime]")
{
    using namespace nekomote;
    using namespace nekomote::runtime;
    // Frozen original PSB observations, mouth UV(4044,323), fixture disabled
    // physics, origin(0,400), 800x1080. See docs/validation/p3-geometry.json.
    const std::array<std::pair<Vec2, Vec2>, 6> observations{{
        {{15, -20}, {14.2152134f, -18.9536133f}},
        {{10, 10}, {10.6066017f, 10.6066040f}},
        {{30, 30}, {21.2132034f, 21.2132080f}},
        {{30, 15}, {26.8328178f, 13.4164124f}},
        {{5, 20}, {4.9611282f, 19.8445129f}},
        {{15, 15}, {15, 15}},
    }};
    for (const auto& [input, expected] : observations)
    {
        near(clamp_pair(input, 30), expected);
        near(clamp_pair({-input.y, input.x}, 30), {-expected.y, expected.x});
    }
    near(clamp_pair({0, 0}, 30), {0, 0});
    near(clamp_pair({12, 0}, 30), {12, 0});
    near(clamp_pair({0, -12}, 30), {0, -12});
    near(clamp_pair({90, -90}, 30), {21.2132034f, -21.2132034f});
}
