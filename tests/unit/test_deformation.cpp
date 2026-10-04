#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/runtime/deformation.hpp>

#include <array>
#include <cmath>

namespace
{
void near(auto actual, nekomote::Vec2 expected, float tolerance = 0.0001f)
{
    REQUIRE_THAT(actual.x, Catch::Matchers::WithinAbs(expected.x, tolerance));
    REQUIRE_THAT(actual.y, Catch::Matchers::WithinAbs(expected.y, tolerance));
}
} // namespace
TEST_CASE("Patch evaluation preserves neutral and extrapolated normalized points", "[runtime]")
{
    using namespace nekomote::runtime;
    const auto neutral = identity_patch();
    near(evaluate_patch(neutral, {-0.25f, 1.25f}), {-0.25f, 1.25f});
    near(evaluate_patch(neutral, {1.25f, -0.25f}), {1.25f, -0.25f});
    auto curve = neutral;
    curve[0].x += 0.64f;
    // The top-left control has weight 1/64 at the domain center.
    near(evaluate_patch(curve, {0.5f, 0.5f}), {0.51f, 0.5f});
    near(evaluate_patch(curve, {0, 0}), {0.64f, 0});
    auto helper = neutral;
    for (auto& point : helper)
        point.y += 0.25f;
    add_patch_displacement(curve, helper);
    near(evaluate_patch(curve, {0.5f, 0.5f}), {0.51f, 0.75f});
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
        near(clamp_pair({input.x, input.y}, 30), expected);
        near(clamp_pair({-input.y, input.x}, 30), {-expected.y, expected.x});
    }
    near(clamp_pair({0, 0}, 30), {0, 0});
    near(clamp_pair({12, 0}, 30), {12, 0});
    near(clamp_pair({0, -12}, 30), {0, -12});
    near(clamp_pair({90, -90}, 30), {21.2132034f, -21.2132034f});
}
TEST_CASE("Physical paired limit preserves the original vector direction", "[runtime]")
{
    using namespace nekomote::runtime;
    // Frozen Math observations and actual geometry from original WebGL PSB.
    // (60,15) distinguishes radial normalization from per-axis clipping.
    // See docs/validation/p6-circle-clamp.json.
    near(clamp_circle({30, 30}, 30), {21.2132034f, 21.2132034f});
    near(clamp_circle({30, 15}, 30), {26.8328157f, 13.4164076f});
    near(clamp_circle({60, 15}, 30), {29.1042747f, 7.2760687f});
    near(clamp_circle({-30, -30}, 30), {-21.2132034f, -21.2132034f});
    near(clamp_circle({15, 15}, 30), {15, 15}, 0);
    near(clamp_circle({15, -20}, 30), {15, -20}, 0);
    near(clamp_circle({0, -18.1898746f}, 30), {0, -18.1898746f}, 0);
    near(clamp_circle({30, 0}, 30), {30, 0}, 0);
    near(clamp_circle({0, 0}, 30), {0, 0}, 0);
}
