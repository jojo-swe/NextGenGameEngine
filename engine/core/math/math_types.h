#pragma once

#include "engine/core/types.h"
#include <cmath>
#include <immintrin.h>

namespace nge::math {

// ─── Scalar constants ────────────────────────────────────────────────────
inline constexpr f32 PI        = 3.14159265358979323846f;
inline constexpr f32 TWO_PI    = 6.28318530717958647692f;
inline constexpr f32 HALF_PI   = 1.57079632679489661923f;
inline constexpr f32 INV_PI    = 0.31830988618379067154f;
inline constexpr f32 DEG2RAD   = PI / 180.0f;
inline constexpr f32 RAD2DEG   = 180.0f / PI;
inline constexpr f32 EPSILON   = 1e-6f;
inline constexpr f32 FLOAT_MAX = 3.402823466e+38f;

// ─── Scalar functions ────────────────────────────────────────────────────
inline f32 Sqrt(f32 x)  { return std::sqrt(x); }
inline f32 InvSqrt(f32 x) { return 1.0f / std::sqrt(x); }
inline f32 Abs(f32 x)   { return std::abs(x); }
inline f32 Sin(f32 x)   { return std::sin(x); }
inline f32 Cos(f32 x)   { return std::cos(x); }
inline f32 Tan(f32 x)   { return std::tan(x); }
inline f32 Asin(f32 x)  { return std::asin(x); }
inline f32 Acos(f32 x)  { return std::acos(x); }
inline f32 Atan2(f32 y, f32 x) { return std::atan2(y, x); }
inline f32 Floor(f32 x) { return std::floor(x); }
inline f32 Ceil(f32 x)  { return std::ceil(x); }
inline f32 Fmod(f32 x, f32 y) { return std::fmod(x, y); }
inline f32 Exp(f32 x)   { return std::exp(x); }
inline f32 Log(f32 x)   { return std::log(x); }
inline f32 Log2(f32 x)  { return std::log2(x); }
inline f32 Pow(f32 base, f32 exp) { return std::pow(base, exp); }

template <typename T>
constexpr T Min(T a, T b) { return a < b ? a : b; }

template <typename T>
constexpr T Max(T a, T b) { return a > b ? a : b; }

template <typename T>
constexpr T Clamp(T val, T lo, T hi) { return Min(Max(val, lo), hi); }

inline f32 Lerp(f32 a, f32 b, f32 t) { return a + t * (b - a); }

inline bool NearZero(f32 x) { return Abs(x) < EPSILON; }
inline bool NearEqual(f32 a, f32 b) { return Abs(a - b) < EPSILON; }

// ─── Basic vector types (for interop and fallback) ───────────────────────
// The primary transform system uses PGA motors, but these are needed for
// vertex data, colors, UV coordinates, etc.
struct Vec2 {
    f32 x = 0, y = 0;

    constexpr Vec2() = default;
    constexpr Vec2(f32 x_, f32 y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(f32 s) const { return {x * s, y * s}; }
    constexpr f32 Dot(Vec2 o) const { return x * o.x + y * o.y; }
    f32 Length() const { return Sqrt(x * x + y * y); }
    Vec2 Normalized() const { f32 l = Length(); return {x / l, y / l}; }
};

struct Vec3 {
    f32 x = 0, y = 0, z = 0;

    constexpr Vec3() = default;
    constexpr Vec3(f32 x_, f32 y_, f32 z_) : x(x_), y(y_), z(z_) {}

    constexpr Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    constexpr Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    constexpr Vec3 operator*(f32 s) const { return {x * s, y * s, z * s}; }
    constexpr Vec3 operator-() const { return {-x, -y, -z}; }
    constexpr f32 Dot(Vec3 o) const { return x * o.x + y * o.y + z * o.z; }
    constexpr Vec3 Cross(Vec3 o) const {
        return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
    }
    f32 Length() const { return Sqrt(x * x + y * y + z * z); }
    f32 LengthSq() const { return x * x + y * y + z * z; }
    Vec3 Normalized() const { f32 l = Length(); return {x / l, y / l, z / l}; }
};

struct Vec4 {
    f32 x = 0, y = 0, z = 0, w = 0;

    constexpr Vec4() = default;
    constexpr Vec4(f32 x_, f32 y_, f32 z_, f32 w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(Vec3 v, f32 w_) : x(v.x), y(v.y), z(v.z), w(w_) {}

    constexpr Vec4 operator+(Vec4 o) const { return {x + o.x, y + o.y, z + o.z, w + o.w}; }
    constexpr Vec4 operator-(Vec4 o) const { return {x - o.x, y - o.y, z - o.z, w - o.w}; }
    constexpr Vec4 operator*(f32 s) const { return {x * s, y * s, z * s, w * s}; }
    constexpr f32 Dot(Vec4 o) const { return x * o.x + y * o.y + z * o.z + w * o.w; }
};

// ─── 4x4 Matrix (for GPU upload / projection; PGA handles transforms) ───
struct Mat4 {
    f32 m[4][4] = {};

    constexpr Mat4() = default;

    static constexpr Mat4 Identity() {
        Mat4 r;
        r.m[0][0] = 1; r.m[1][1] = 1; r.m[2][2] = 1; r.m[3][3] = 1;
        return r;
    }

    static Mat4 Perspective(f32 fovY, f32 aspect, f32 nearZ, f32 farZ) {
        Mat4 r;
        f32 tanHalfFov = Tan(fovY * 0.5f);
        r.m[0][0] = 1.0f / (aspect * tanHalfFov);
        r.m[1][1] = 1.0f / tanHalfFov;
        r.m[2][2] = farZ / (nearZ - farZ);
        r.m[2][3] = -1.0f;
        r.m[3][2] = (nearZ * farZ) / (nearZ - farZ);
        return r;
    }

    static Mat4 InfinitePerspective(f32 fovY, f32 aspect, f32 nearZ) {
        Mat4 r;
        f32 tanHalfFov = Tan(fovY * 0.5f);
        r.m[0][0] = 1.0f / (aspect * tanHalfFov);
        r.m[1][1] = 1.0f / tanHalfFov;
        r.m[2][2] = -1.0f;
        r.m[2][3] = -1.0f;
        r.m[3][2] = -nearZ;
        return r;
    }

    static Mat4 Ortho(f32 left, f32 right, f32 bottom, f32 top, f32 nearZ, f32 farZ) {
        Mat4 r;
        r.m[0][0] = 2.0f / (right - left);
        r.m[1][1] = 2.0f / (top - bottom);
        r.m[2][2] = 1.0f / (nearZ - farZ);
        r.m[3][0] = -(right + left) / (right - left);
        r.m[3][1] = -(top + bottom) / (top - bottom);
        r.m[3][2] = nearZ / (nearZ - farZ);
        r.m[3][3] = 1.0f;
        return r;
    }

    constexpr Mat4 operator*(const Mat4& b) const {
        Mat4 r;
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j) {
                r.m[i][j] = 0;
                for (int k = 0; k < 4; ++k)
                    r.m[i][j] += m[i][k] * b.m[k][j];
            }
        return r;
    }

    constexpr Vec4 operator*(Vec4 v) const {
        return {
            m[0][0]*v.x + m[0][1]*v.y + m[0][2]*v.z + m[0][3]*v.w,
            m[1][0]*v.x + m[1][1]*v.y + m[1][2]*v.z + m[1][3]*v.w,
            m[2][0]*v.x + m[2][1]*v.y + m[2][2]*v.z + m[2][3]*v.w,
            m[3][0]*v.x + m[3][1]*v.y + m[3][2]*v.z + m[3][3]*v.w
        };
    }

    const f32* Ptr() const { return &m[0][0]; }
};

} // namespace nge::math
