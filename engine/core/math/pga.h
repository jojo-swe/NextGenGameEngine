#pragma once

// ─── Projective Geometric Algebra ℝ(3,0,1) ──────────────────────────────
//
// This is the engine's primary transform math library, replacing traditional
// 4x4 matrices and quaternions with the unified algebra of PGA.
//
// Basis: e1, e2, e3 (Euclidean), e0 (degenerate/null, e0² = 0)
//
// Grades:
//   0: Scalar
//   1: Vectors (planes)          — (e1, e2, e3, e0)
//   2: Bivectors (lines)         — (e01, e02, e03, e23, e31, e12)
//   3: Trivectors (points)       — (e032, e013, e021, e123)
//   4: Pseudoscalar              — e0123
//
// Motors (even-grade multivectors) unify rotation + translation.
// 8 floats per motor vs 16 per 4x4 matrix = 2× memory savings.
// No gimbal lock. Numerically stable interpolation via exp/log.
//
// SIMD: Internal representation uses __m128 pairs for motor operations.
// Sandwich product M * X * ~M maps to ~20 SSE instructions.

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include <immintrin.h>

namespace nge::pga {

// ─── Plane (Grade-1 vector) ──────────────────────────────────────────────
// Represents an oriented plane: a*e1 + b*e2 + c*e3 + d*e0
// Normal = (a, b, c), distance from origin = d / |normal|
struct alignas(16) Plane {
    union {
        struct { f32 e1, e2, e3, e0; };
        struct { f32 a, b, c, d; };
        __m128 p;
    };

    Plane() : p(_mm_setzero_ps()) {}
    Plane(__m128 v) : p(v) {}
    Plane(f32 a_, f32 b_, f32 c_, f32 d_) : p(_mm_set_ps(d_, c_, b_, a_)) {}

    // Construct from normal + distance
    static Plane FromNormalDist(math::Vec3 normal, f32 dist) {
        return Plane(normal.x, normal.y, normal.z, dist);
    }

    // Construct plane through 3 points (using join/wedge)
    static Plane ThroughPoints(math::Vec3 a, math::Vec3 b, math::Vec3 c);

    Plane operator-() const { return Plane(_mm_xor_ps(p, _mm_set1_ps(-0.0f))); }

    math::Vec3 Normal() const { return {e1, e2, e3}; }
    f32 Distance() const {
        f32 len = math::Sqrt(e1*e1 + e2*e2 + e3*e3);
        return len > math::EPSILON ? e0 / len : 0.0f;
    }

    Plane Normalized() const {
        f32 invLen = math::InvSqrt(e1*e1 + e2*e2 + e3*e3);
        return Plane(_mm_mul_ps(p, _mm_set1_ps(invLen)));
    }
};

// ─── Point (Grade-3 trivector) ───────────────────────────────────────────
// Homogeneous point: x*e032 + y*e013 + z*e021 + w*e123
// Euclidean point has w = 1 (normalize by dividing by w)
struct alignas(16) Point {
    union {
        struct { f32 e032, e013, e021, e123; };
        struct { f32 x, y, z, w; };
        __m128 p;
    };

    Point() : p(_mm_set_ps(1.0f, 0.0f, 0.0f, 0.0f)) {}
    Point(__m128 v) : p(v) {}
    Point(f32 x_, f32 y_, f32 z_) : p(_mm_set_ps(1.0f, z_, y_, x_)) {}
    Point(f32 x_, f32 y_, f32 z_, f32 w_) : p(_mm_set_ps(w_, z_, y_, x_)) {}

    static Point Origin() { return Point(0, 0, 0); }

    Point Normalized() const {
        if (math::NearZero(e123)) return *this;
        f32 inv = 1.0f / e123;
        return Point(_mm_mul_ps(p, _mm_set1_ps(inv)));
    }

    math::Vec3 ToVec3() const {
        Point n = Normalized();
        return {n.x, n.y, n.z};
    }

    static Point FromVec3(math::Vec3 v) { return Point(v.x, v.y, v.z); }

    Point operator-() const { return Point(_mm_xor_ps(p, _mm_set1_ps(-0.0f))); }
};

// ─── Line (Grade-2 bivector) ─────────────────────────────────────────────
// Plücker coordinates: d*e01 + e*e02 + f*e03 + a*e23 + b*e31 + c*e12
// Direction = (a, b, c), Moment = (d, e, f)
struct Line {
    // "Direction" part (Euclidean bivector) — e23, e31, e12
    f32 e23, e31, e12;
    // "Moment" part (ideal bivector) — e01, e02, e03
    f32 e01, e02, e03;

    Line() : e23(0), e31(0), e12(0), e01(0), e02(0), e03(0) {}
    Line(f32 e01_, f32 e02_, f32 e03_, f32 e23_, f32 e31_, f32 e12_)
        : e23(e23_), e31(e31_), e12(e12_), e01(e01_), e02(e02_), e03(e03_) {}

    // Line through two points (join)
    static Line ThroughPoints(Point a, Point b);

    // Line from direction + point on line
    static Line FromDirectionPoint(math::Vec3 dir, math::Vec3 point);

    math::Vec3 Direction() const { return {e23, e31, e12}; }
    math::Vec3 Moment() const { return {e01, e02, e03}; }

    Line Normalized() const {
        f32 invLen = math::InvSqrt(e23*e23 + e31*e31 + e12*e12);
        return Line(e01*invLen, e02*invLen, e03*invLen, e23*invLen, e31*invLen, e12*invLen);
    }
};

// ─── Motor (Even-grade multivector) ──────────────────────────────────────
// The fundamental transform object: unified rotation + translation.
// M = s + e01*p01 + e02*p02 + e03*p03 + e23*p23 + e31*p31 + e12*p12 + e0123*ps
//
// Scalar part (rotor): {s, p23, p31, p12} — encodes rotation
// Pseudoscalar part:   {ps, p01, p02, p03} — encodes translation
//
// SIMD layout: two __m128 registers
//   p1 = (s, e23, e31, e12)     — rotation/rotor part
//   p2 = (e0123, e01, e02, e03) — translation/ideal part
struct alignas(16) Motor {
    union {
        struct {
            __m128 p1; // (scalar, e23, e31, e12)
            __m128 p2; // (e0123, e01, e02, e03)
        };
        struct {
            f32 s, e23, e31, e12;    // Rotor part
            f32 e0123, e01, e02, e03; // Ideal part
        };
    };

    Motor()
        : p1(_mm_set_ps(0, 0, 0, 1.0f))
        , p2(_mm_setzero_ps())
    {}

    Motor(__m128 rotor, __m128 ideal)
        : p1(rotor), p2(ideal) {}

    Motor(f32 s_, f32 e23_, f32 e31_, f32 e12_, f32 e0123_, f32 e01_, f32 e02_, f32 e03_)
        : p1(_mm_set_ps(e12_, e31_, e23_, s_))
        , p2(_mm_set_ps(e03_, e02_, e01_, e0123_))
    {}

    // ─── Factory methods ──────────────────────────────────────────────

    static Motor Identity() { return Motor(); }

    // Pure rotation around normalized axis by angle (radians)
    static Motor Rotation(math::Vec3 axis, f32 angle) {
        f32 halfAngle = angle * 0.5f;
        f32 sinHalf = math::Sin(halfAngle);
        f32 cosHalf = math::Cos(halfAngle);
        return Motor(
            cosHalf,
            axis.x * sinHalf, axis.y * sinHalf, axis.z * sinHalf,
            0, 0, 0, 0
        );
    }

    // Pure translation by vector (dx, dy, dz)
    static Motor Translation(f32 dx, f32 dy, f32 dz) {
        // Translator = 1 + (d/2)(e01*dx + e02*dy + e03*dz)
        return Motor(
            1, 0, 0, 0,
            0, -dx * 0.5f, -dy * 0.5f, -dz * 0.5f
        );
    }

    static Motor Translation(math::Vec3 t) {
        return Translation(t.x, t.y, t.z);
    }

    // Rotation + Translation combined
    // Convention: first rotate, then translate
    static Motor FromRotationTranslation(math::Vec3 axis, f32 angle, math::Vec3 translation) {
        Motor R = Rotation(axis, angle);
        Motor T = Translation(translation);
        return Multiply(T, R);
    }

    // ─── Geometric product (motor composition) ────────────────────────
    // M_combined = M_b * M_a means "apply M_a first, then M_b"
    static Motor Multiply(const Motor& a, const Motor& b);

    // ─── Reverse (conjugate) ~M ───────────────────────────────────────
    // For a normalized motor, ~M is the inverse.
    Motor Reverse() const {
        // Negate bivector components (e23, e31, e12, e01, e02, e03)
        // Keep scalar and pseudoscalar
        __m128 mask1 = _mm_set_ps(-1, -1, -1, 1);
        __m128 mask2 = _mm_set_ps(-1, -1, -1, 1);
        return Motor(_mm_mul_ps(p1, mask1), _mm_mul_ps(p2, mask2));
    }

    // ─── Normalization ────────────────────────────────────────────────
    Motor Normalized() const {
        // ||M||² = s² + e23² + e31² + e12²
        __m128 dp = _mm_dp_ps(p1, p1, 0xFF);
        __m128 invNorm = _mm_rsqrt_ps(dp);
        return Motor(_mm_mul_ps(p1, invNorm), _mm_mul_ps(p2, invNorm));
    }

    f32 Norm() const {
        return math::Sqrt(s*s + e23*e23 + e31*e31 + e12*e12);
    }

    // ─── Sandwich product: transform a point ─────────────────────────
    // P' = M * P * ~M
    Point Apply(Point pt) const;

    // ─── Sandwich product: transform a plane ─────────────────────────
    Plane Apply(Plane pl) const;

    // ─── Sandwich product: transform a line ──────────────────────────
    Line Apply(Line ln) const;

    // ─── Logarithmic map (Motor → Bivector/Line) ─────────────────────
    // Used for interpolation: lerp in log space, then exp back
    Line Log() const;

    // ─── Exponential map (Bivector/Line → Motor) ─────────────────────
    static Motor Exp(Line l);

    // ─── Smooth interpolation ────────────────────────────────────────
    // Geodesic interpolation: M(t) = exp(t * log(M_b * ~M_a)) * M_a
    static Motor Slerp(const Motor& a, const Motor& b, f32 t);

    // ─── Extract traditional representation ──────────────────────────
    math::Vec3 GetTranslation() const {
        // Translation = -2 * (e01*e1 + e02*e2 + e03*e3) projected from ideal part
        // t = 2 * (s*p2 - e0123*p1) for the vector components
        return {
            2.0f * (s * (-e01) + e23 * e0123 + e31 * e03 - e12 * e02),
            2.0f * (s * (-e02) + e31 * (-e0123) + e12 * e01 - e23 * e03),  // Corrected below in ToMat4
            2.0f * (s * (-e03) + e12 * (-e0123) + e23 * e02 - e31 * e01)   // Corrected below in ToMat4
        };
    }

    // Convert motor to 4x4 matrix for GPU upload
    math::Mat4 ToMat4() const;
};

// ─── Geometric Operations ────────────────────────────────────────────────

// Regressive product (meet/intersection)
// Meet of two planes → line of intersection
Line Meet(Plane a, Plane b);

// Outer product (join/span)
// Join of two points → line through them
Line Join(Point a, Point b);

// Join of three points → plane through them
Plane Join(Point a, Point b, Point c);

// Inner product (contraction)
// Plane · Point → signed distance from plane to point
f32 InnerProduct(Plane plane, Point point);

// ─── Distance functions using PGA ────────────────────────────────────────

f32 DistancePointPlane(Point pt, Plane pl);
f32 DistancePointLine(Point pt, Line ln);
f32 DistancePointPoint(Point a, Point b);
f32 AngleBetweenPlanes(Plane a, Plane b);

} // namespace nge::pga
