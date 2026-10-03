#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <nekomote/units.hpp>

using namespace nekomote;

TEST_CASE("Seconds arithmetic", "[units]") {
    Seconds a{1.0f};
    Seconds b{0.5f};
    REQUIRE((a + b).value == 1.5f);
    REQUIRE((a - b).value == 0.5f);
    REQUIRE((a * 2.0f).value == 2.0f);
    REQUIRE((a / 2.0f).value == 0.5f);
    REQUIRE(a == Seconds{1.0f});
    REQUIRE(b < a);
}

TEST_CASE("FixedStep validity", "[units]") {
    FixedStep zero{};
    REQUIRE_FALSE(zero.valid());

    FixedStep step{1.0f / 60.0f};
    REQUIRE(step.valid());
}

TEST_CASE("Alpha multiply", "[units]") {
    Alpha a{0.5f};
    Alpha b{0.4f};
    REQUIRE((a * b).value == 0.2f);
}

TEST_CASE("degrees_to_radians", "[units]") {
    Radians r = degrees_to_radians(180.0f);
    REQUIRE_THAT(r.value, Catch::Matchers::WithinAbs(3.14159265358979323846f, 1e-5f));

    Radians zero = degrees_to_radians(0.0f);
    REQUIRE(zero.value == 0.0f);
}
