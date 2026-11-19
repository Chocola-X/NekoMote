#include <catch2/catch_test_macros.hpp>

#include <limits>

#include "desktop_gaze.hpp"

using nekomote::layer_shell::detail::GazeLayout;
using nekomote::layer_shell::detail::GazeSourcePoint;

TEST_CASE("rotated fractional-scale source maps across both desktop axes")
{
    const GazeLayout source{-960, -1536, 864, 1536, true};
    const GazeLayout target{-96, 0, 1280, 720, true};
    const GazeSourcePoint point{540, 960, 1920, 1080, true};
    REQUIRE(point.inside());
    REQUIRE(point.relative_to(source, target, 2) == std::pair{-864.0, -1536.0});
    REQUIRE(point.relative_to(source, source, 3) == std::pair{1296.0, 2304.0});
}

TEST_CASE("half-open cursor bounds rank a seam without clamping hotspots")
{
    const GazeLayout source{0, 0, 100, 80, true};
    const GazeSourcePoint edge{125, 100, 125, 100, false};
    const GazeSourcePoint overlap{-5, -10, 125, 100, false};
    REQUIRE_FALSE(edge.inside());
    REQUIRE_FALSE(overlap.inside());
    REQUIRE(edge.relative_to(source, source, 2) == std::pair{200.0, 160.0});
    REQUIRE(overlap.relative_to(source, source, 2) == std::pair{-8.0, -16.0});
    REQUIRE(GazeSourcePoint{124, 99, 125, 100, false}.inside());
    REQUIRE(GazeSourcePoint{0, 0, 125, 100, true}.inside());
    REQUIRE_FALSE(GazeSourcePoint{100, 0, 125, 100, true}.inside());
}

TEST_CASE("layout readiness and wide origin arithmetic")
{
    REQUIRE_FALSE(GazeLayout{0, 0, 10, 10, false}.ready());
    REQUIRE_FALSE(GazeLayout{0, 0, 0, 10, true}.ready());
    const GazeLayout source{std::numeric_limits<int32_t>::min(), 0, 1920, 1080, true};
    const GazeLayout target{std::numeric_limits<int32_t>::max(), 0, 1920, 1080, true};
    REQUIRE(source.ready());
    const GazeSourcePoint point{0, 0, 1920, 1080, false};
    REQUIRE(point.relative_to(source, target, 2).first == -8589934590.0);
}
