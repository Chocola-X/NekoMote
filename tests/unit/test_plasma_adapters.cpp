#include <catch2/catch_test_macros.hpp>

#include "pointer_geometry.hpp"
#include "visibility_state.hpp"

using namespace nekomote::plasma;

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

TEST_CASE("Only fully obscured visibility suspends playback", "[plasma]")
{
    CHECK_FALSE(suspends_playback(VisibilityState::Unobscured));
    CHECK_FALSE(suspends_playback(VisibilityState::PartiallyObscured));
    CHECK(suspends_playback(VisibilityState::FullyObscured));
}
