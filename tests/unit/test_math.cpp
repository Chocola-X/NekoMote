#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <nekomote/math.hpp>

using namespace nekomote;

TEST_CASE("Vec2 arithmetic", "[math]")
{
    Vec2 a{1.0f, 2.0f};
    Vec2 b{3.0f, 4.0f};

    REQUIRE(a + b == Vec2{4.0f, 6.0f});
    REQUIRE(b - a == Vec2{2.0f, 2.0f});
    REQUIRE(a * 2.0f == Vec2{2.0f, 4.0f});
    REQUIRE(2.0f * a == Vec2{2.0f, 4.0f});
    REQUIRE(b / 2.0f == Vec2{1.5f, 2.0f});
}

TEST_CASE("Vec2 dot and length", "[math]")
{
    Vec2 a{3.0f, 4.0f};
    REQUIRE(a.dot({1.0f, 0.0f}) == 3.0f);
    REQUIRE(a.length_squared() == 25.0f);
    REQUIRE_THAT(a.length(), Catch::Matchers::WithinAbs(5.0f, 1e-6f));
}

TEST_CASE("Mat3 identity is neutral", "[math]")
{
    Mat3 I = Mat3::identity();
    Vec2 p{5.0f, -2.0f};
    REQUIRE(transform_point(I, p) == p);
    REQUIRE(transform_direction(I, p) == p);
}

TEST_CASE("Mat3 translate", "[math]")
{
    Mat3 t = Mat3::translate(3.0f, -1.0f);
    Vec2 p{1.0f, 1.0f};
    REQUIRE(transform_point(t, p) == Vec2{4.0f, 0.0f});
    // Translation does not affect directions.
    REQUIRE(transform_direction(t, p) == p);
}

TEST_CASE("Mat3 scale", "[math]")
{
    Mat3 s = Mat3::scale(2.0f, 0.5f);
    Vec2 p{3.0f, 4.0f};
    REQUIRE(transform_point(s, p) == Vec2{6.0f, 2.0f});
    REQUIRE(transform_direction(s, p) == Vec2{6.0f, 2.0f});
}

TEST_CASE("Mat3 rotate 90 degrees", "[math]")
{
    Mat3 r = Mat3::rotate(3.14159265358979323846f / 2.0f);
    Vec2 p{1.0f, 0.0f};
    Vec2 q = transform_point(r, p);
    REQUIRE_THAT(q.x, Catch::Matchers::WithinAbs(0.0f, 1e-5f));
    REQUIRE_THAT(q.y, Catch::Matchers::WithinAbs(1.0f, 1e-5f));
}

TEST_CASE("Mat3 compose TRS equals T*R*S", "[math]")
{
    Mat3 t = Mat3::translate(10.0f, 20.0f);
    Mat3 r = Mat3::rotate(0.5f);
    Mat3 s = Mat3::scale(2.0f, 3.0f);
    Mat3 composed = t * r * s;

    Vec2 p{1.0f, 1.0f};
    Vec2 via_separate = transform_point(t, transform_point(r, transform_point(s, p)));
    Vec2 via_composed = transform_point(composed, p);
    REQUIRE_THAT(via_composed.x, Catch::Matchers::WithinAbs(via_separate.x, 1e-5f));
    REQUIRE_THAT(via_composed.y, Catch::Matchers::WithinAbs(via_separate.y, 1e-5f));
}
