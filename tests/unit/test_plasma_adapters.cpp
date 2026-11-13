#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vector>

#include "idle_fps.hpp"
#include "occlusion_geometry.hpp"
#include "pointer_geometry.hpp"

using namespace nekomote::plasma;
using Catch::Matchers::WithinAbs;

TEST_CASE("X11 pointer mapping applies window origin and device pixel ratio", "[plasma]")
{
    const PointerPointF shifted = x11_local_logical({2500, 300}, {1920, 0}, {1920, 1080}, 1.0);
    CHECK(shifted.x == 580.0);
    CHECK(shifted.y == 300.0);

    const PointerPointF hidpi = x11_local_logical({2000, 100}, {1920, 0}, {1920, 1080}, 2.0);
    CHECK(hidpi.x == 40.0);
    CHECK(hidpi.y == 50.0);

    const PointerPointF clamped = x11_local_logical({9999, -50}, {1920, 0}, {1920, 1080}, 1.0);
    CHECK(clamped.x == 1920.0);
    CHECK(clamped.y == 0.0);
}

TEST_CASE("Wayland pointer mapping clamps to the output and window", "[plasma]")
{
    const PointerPointF on_screen =
        wayland_local_logical({2400, 300}, {1920, 0, 1920, 1080}, {1920, 1080});
    CHECK(on_screen.x == 480.0);
    CHECK(on_screen.y == 300.0);

    const PointerPointF left =
        wayland_local_logical({100, 500}, {1920, 0, 1920, 1080}, {1920, 1080});
    CHECK(left.x == 0.0);
    CHECK(left.y == 500.0);

    const PointerPointF outside =
        wayland_local_logical({9999, 9999}, {1920, 0, 1920, 1080}, {1280, 720});
    CHECK(outside.x == 1280.0);
    CHECK(outside.y == 720.0);
}

TEST_CASE("Occlusion coverage counts panels and maximized windows", "[plasma]")
{
    const OcclusionRect screen{0, 0, 1920, 1080};
    const std::vector<OcclusionRect> panel{{0, 1018, 1920, 1080}};
    CHECK_FALSE(fully_covered(panel, screen));

    const std::vector<OcclusionRect> maximized{{0, 0, 1920, 1018}, {0, 1018, 1920, 1080}};
    CHECK(fully_covered(maximized, screen));
    CHECK_THAT(covered_fraction(maximized, screen), WithinAbs(1.0, 1e-9));

    const std::vector<OcclusionRect> half{{0, 0, 960, 1080}};
    CHECK_FALSE(fully_covered(half, screen));
    CHECK_THAT(covered_fraction(half, screen), WithinAbs(0.5, 1e-9));
}

TEST_CASE("Occlusion coverage handles overlap and the tolerance edge", "[plasma]")
{
    const OcclusionRect screen{0, 0, 1000, 1000};
    const std::vector<OcclusionRect> overlap{{0, 0, 600, 1000}, {400, 0, 1000, 1000}};
    CHECK(fully_covered(overlap, screen));
    CHECK_THAT(covered_fraction(overlap, screen), WithinAbs(1.0, 1e-9));

    const std::vector<OcclusionRect> almost{{0, 0, 996, 1000}};
    CHECK(fully_covered(almost, screen));

    const std::vector<OcclusionRect> too_small{{0, 0, 994, 1000}};
    CHECK_FALSE(fully_covered(too_small, screen));
}

TEST_CASE("Frame extents expand and clip to the screen", "[plasma]")
{
    const OcclusionRect bounds{0, 0, 1920, 1080};
    const auto rect = intersect(expand({0, 36, 1280, 658}, 1, 1, 36, 1), bounds);
    CHECK(rect.left == 0);
    CHECK(rect.top == 0);
    CHECK(rect.right == 1281);
    CHECK(rect.bottom == 659);
}

TEST_CASE("Idle frame limit respects configured caps", "[plasma]")
{
    CHECK(idle_fps_limit(60, false) == 60);
    CHECK(idle_fps_limit(60, true) == 30);
    CHECK(idle_fps_limit(144, true) == 30);
    CHECK(idle_fps_limit(30, true) == 30);
    CHECK(idle_fps_limit(10, false) == 10);
    CHECK(idle_fps_limit(0, true) == 0);
}
