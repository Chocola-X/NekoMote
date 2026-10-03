#pragma once

#include <cmath>
#include <cstdint>

namespace nekomote
{

// Coordinate-space conventions (authoritative: docs/conventions.md):
//
//   model space   - per-model local coordinates. Origin and Y axis direction
//                   are confirmed against source assets in P1; until then code
//                   must not assume a particular orientation. Treat the model
//                   space as "as authored in the imported asset".
//
//   uv space      - [0,1]^2, origin top-left, Y down (image convention).
//                   OpenGL upload preserves image row order; shaders sample top-left
//                   UVs directly. Only diagnostic readback flips framebuffer rows.
//
//   screen space  - pixels, origin top-left, Y down. render_gl converts to NDC.
//
//   ndc           - OpenGL normalized device coords, [-1,1]^2, origin center,
//                   Y up.
//
// Matrix conventions:
//
//   - Column-major storage (OpenGL convention). Mat3 stores 9 floats in
//     column-major order: [m00 m10 m20 | m01 m11 m21 | m02 m12 m22].
//   - Transforms compose left-to-right as M = T * R * S; vectors are column
//     vectors transformed as v' = M * v.
//   - 2D affine uses Mat3 with homogeneous row [0 0 1]; the third row is kept
//     identity for pure 2D affine but stored fully to allow projection later.

struct Vec2
{
    float x{0.0f};
    float y{0.0f};

    constexpr Vec2() noexcept = default;
    constexpr Vec2(float x_, float y_) noexcept : x(x_), y(y_)
    {
    }

    constexpr Vec2 operator+(Vec2 o) const noexcept
    {
        return {x + o.x, y + o.y};
    }
    constexpr Vec2 operator-(Vec2 o) const noexcept
    {
        return {x - o.x, y - o.y};
    }
    constexpr Vec2 operator*(float s) const noexcept
    {
        return {x * s, y * s};
    }
    constexpr Vec2 operator/(float s) const noexcept
    {
        return {x / s, y / s};
    }

    constexpr Vec2& operator+=(Vec2 o) noexcept
    {
        x += o.x;
        y += o.y;
        return *this;
    }
    constexpr Vec2& operator-=(Vec2 o) noexcept
    {
        x -= o.x;
        y -= o.y;
        return *this;
    }

    constexpr bool operator==(Vec2 o) const noexcept
    {
        return x == o.x && y == o.y;
    }
    constexpr bool operator!=(Vec2 o) const noexcept
    {
        return !(*this == o);
    }

    constexpr float dot(Vec2 o) const noexcept
    {
        return x * o.x + y * o.y;
    }
    constexpr float length_squared() const noexcept
    {
        return x * x + y * y;
    }
    float length() const noexcept
    {
        return std::sqrt(length_squared());
    }
};

constexpr Vec2 operator*(float s, Vec2 v) noexcept
{
    return {s * v.x, s * v.y};
}

// 2D affine transform, column-major 3x3. Element access is (row, col).
struct Mat3
{
    // m[col*3 + row]
    float e[9]{1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};

    constexpr Mat3() noexcept = default;

    constexpr float operator()(int row, int col) const noexcept
    {
        return e[col * 3 + row];
    }
    constexpr float& operator()(int row, int col) noexcept
    {
        return e[col * 3 + row];
    }

    constexpr bool operator==(Mat3 o) const noexcept
    {
        for (int i = 0; i < 9; ++i)
        {
            if (e[i] != o.e[i])
                return false;
        }
        return true;
    }

    static constexpr Mat3 identity() noexcept
    {
        return Mat3{};
    }

    static constexpr Mat3 translate(float tx, float ty) noexcept
    {
        Mat3 m{};
        m(0, 2) = tx;
        m(1, 2) = ty;
        return m;
    }

    static constexpr Mat3 scale(float sx, float sy) noexcept
    {
        Mat3 m{};
        m(0, 0) = sx;
        m(1, 1) = sy;
        return m;
    }

    static Mat3 rotate(float radians) noexcept
    {
        Mat3 m{};
        float c = std::cos(radians);
        float s = std::sin(radians);
        m(0, 0) = c;
        m(0, 1) = -s;
        m(1, 0) = s;
        m(1, 1) = c;
        return m;
    }
};

// Matrix * matrix (column-major).
constexpr Mat3 operator*(Mat3 a, Mat3 b) noexcept
{
    Mat3 r{};
    for (int col = 0; col < 3; ++col)
    {
        for (int row = 0; row < 3; ++row)
        {
            float sum = 0.0f;
            for (int k = 0; k < 3; ++k)
            {
                sum += a(row, k) * b(k, col);
            }
            r(row, col) = sum;
        }
    }
    return r;
}

// Transform a 2D point (homogeneous w=1) by an affine Mat3.
constexpr Vec2 transform_point(Mat3 m, Vec2 p) noexcept
{
    return {
        m(0, 0) * p.x + m(0, 1) * p.y + m(0, 2),
        m(1, 0) * p.x + m(1, 1) * p.y + m(1, 2),
    };
}

// Transform a 2D direction (w=0, ignores translation).
constexpr Vec2 transform_direction(Mat3 m, Vec2 d) noexcept
{
    return {
        m(0, 0) * d.x + m(0, 1) * d.y,
        m(1, 0) * d.x + m(1, 1) * d.y,
    };
}

} // namespace nekomote
