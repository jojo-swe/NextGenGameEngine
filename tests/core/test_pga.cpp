#include <gtest/gtest.h>
#include "engine/core/math/pga.h"
#include <cmath>

using namespace nge;
using namespace nge::pga;
using namespace nge::math;

static constexpr f32 EPS = 1e-4f;

// ─── Point Tests ─────────────────────────────────────────────────────────

TEST(PGA_Point, Construction) {
    Point p(1, 2, 3);
    EXPECT_NEAR(p.x, 1.0f, EPS);
    EXPECT_NEAR(p.y, 2.0f, EPS);
    EXPECT_NEAR(p.z, 3.0f, EPS);
    EXPECT_NEAR(p.w, 1.0f, EPS);
}

TEST(PGA_Point, ToVec3) {
    Point p(3, 4, 5);
    Vec3 v = p.ToVec3();
    EXPECT_NEAR(v.x, 3.0f, EPS);
    EXPECT_NEAR(v.y, 4.0f, EPS);
    EXPECT_NEAR(v.z, 5.0f, EPS);
}

TEST(PGA_Point, Origin) {
    Point o = Point::Origin();
    Vec3 v = o.ToVec3();
    EXPECT_NEAR(v.x, 0.0f, EPS);
    EXPECT_NEAR(v.y, 0.0f, EPS);
    EXPECT_NEAR(v.z, 0.0f, EPS);
}

// ─── Plane Tests ─────────────────────────────────────────────────────────

TEST(PGA_Plane, NormalAndDistance) {
    Plane p = Plane::FromNormalDist({0, 1, 0}, 5.0f);
    Vec3 n = p.Normal();
    EXPECT_NEAR(n.x, 0.0f, EPS);
    EXPECT_NEAR(n.y, 1.0f, EPS);
    EXPECT_NEAR(n.z, 0.0f, EPS);
    EXPECT_NEAR(p.Distance(), 5.0f, EPS);
}

TEST(PGA_Plane, ThroughPoints) {
    Plane p = Plane::ThroughPoints({0,0,0}, {1,0,0}, {0,1,0});
    // Should be the XY plane, normal = (0,0,1) or (0,0,-1)
    Vec3 n = p.Normalized().Normal();
    EXPECT_NEAR(Abs(n.z), 1.0f, EPS);
}

// ─── Motor: Identity ─────────────────────────────────────────────────────

TEST(PGA_Motor, Identity) {
    Motor m = Motor::Identity();
    Point p(3, 4, 5);
    Point result = m.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, 3.0f, EPS);
    EXPECT_NEAR(v.y, 4.0f, EPS);
    EXPECT_NEAR(v.z, 5.0f, EPS);
}

// ─── Motor: Pure Translation ─────────────────────────────────────────────

TEST(PGA_Motor, Translation) {
    Motor t = Motor::Translation(10, 20, 30);
    Point p = Point::Origin();
    Point result = t.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, 10.0f, EPS);
    EXPECT_NEAR(v.y, 20.0f, EPS);
    EXPECT_NEAR(v.z, 30.0f, EPS);
}

TEST(PGA_Motor, TranslationNonOrigin) {
    Motor t = Motor::Translation(1, 2, 3);
    Point p(5, 5, 5);
    Point result = t.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, 6.0f, EPS);
    EXPECT_NEAR(v.y, 7.0f, EPS);
    EXPECT_NEAR(v.z, 8.0f, EPS);
}

// ─── Motor: Pure Rotation ────────────────────────────────────────────────

TEST(PGA_Motor, Rotation90Z) {
    // Rotate point (1,0,0) 90 degrees around Z → should give (0,1,0)
    Motor r = Motor::Rotation({0, 0, 1}, PI * 0.5f);
    Point p(1, 0, 0);
    Point result = r.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, 0.0f, EPS);
    EXPECT_NEAR(v.y, 1.0f, EPS);
    EXPECT_NEAR(v.z, 0.0f, EPS);
}

TEST(PGA_Motor, Rotation180Y) {
    // Rotate (1,0,0) 180 degrees around Y → should give (-1,0,0)
    Motor r = Motor::Rotation({0, 1, 0}, PI);
    Point p(1, 0, 0);
    Point result = r.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, -1.0f, EPS);
    EXPECT_NEAR(v.y, 0.0f, EPS);
    EXPECT_NEAR(v.z, 0.0f, EPS);
}

// ─── Motor: Composition ──────────────────────────────────────────────────

TEST(PGA_Motor, ComposeTranslations) {
    Motor t1 = Motor::Translation(1, 0, 0);
    Motor t2 = Motor::Translation(0, 2, 0);
    Motor combined = Motor::Multiply(t2, t1); // t1 first, then t2

    Point p = Point::Origin();
    Point result = combined.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, 1.0f, EPS);
    EXPECT_NEAR(v.y, 2.0f, EPS);
    EXPECT_NEAR(v.z, 0.0f, EPS);
}

TEST(PGA_Motor, ComposeRotationTranslation) {
    // Rotate 90 around Z, then translate by (0,0,5)
    Motor r = Motor::Rotation({0, 0, 1}, PI * 0.5f);
    Motor t = Motor::Translation(0, 0, 5);
    Motor combined = Motor::Multiply(t, r);

    Point p(1, 0, 0);
    Point result = combined.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, 0.0f, EPS);
    EXPECT_NEAR(v.y, 1.0f, EPS);
    EXPECT_NEAR(v.z, 5.0f, EPS);
}

// ─── Motor: Reverse (Inverse) ────────────────────────────────────────────

TEST(PGA_Motor, Reverse) {
    Motor m = Motor::FromRotationTranslation({0, 1, 0}, 0.5f, {3, 4, 5});
    Motor mInv = m.Reverse();
    Motor identity = Motor::Multiply(mInv, m);

    // Apply identity to a point — should leave it unchanged
    Point p(7, 8, 9);
    Point result = identity.Apply(p);
    Vec3 v = result.ToVec3();

    EXPECT_NEAR(v.x, 7.0f, 0.01f);
    EXPECT_NEAR(v.y, 8.0f, 0.01f);
    EXPECT_NEAR(v.z, 9.0f, 0.01f);
}

// ─── Motor: Slerp (Geodesic Interpolation) ──────────────────────────────

TEST(PGA_Motor, SlerpEndpoints) {
    Motor a = Motor::Translation(0, 0, 0);
    Motor b = Motor::Translation(10, 0, 0);

    // t=0 → should be at a
    Motor m0 = Motor::Slerp(a, b, 0.0f);
    Vec3 v0 = m0.Apply(Point::Origin()).ToVec3();
    EXPECT_NEAR(v0.x, 0.0f, 0.1f);

    // t=1 → should be at b
    Motor m1 = Motor::Slerp(a, b, 1.0f);
    Vec3 v1 = m1.Apply(Point::Origin()).ToVec3();
    EXPECT_NEAR(v1.x, 10.0f, 0.1f);
}

TEST(PGA_Motor, SlerpMidpoint) {
    Motor a = Motor::Translation(0, 0, 0);
    Motor b = Motor::Translation(10, 0, 0);

    Motor mid = Motor::Slerp(a, b, 0.5f);
    Vec3 v = mid.Apply(Point::Origin()).ToVec3();
    EXPECT_NEAR(v.x, 5.0f, 0.5f);
}

// ─── Motor: ToMat4 ──────────────────────────────────────────────────────

TEST(PGA_Motor, ToMat4_Identity) {
    Motor m = Motor::Identity();
    Mat4 mat = m.ToMat4();

    EXPECT_NEAR(mat.m[0][0], 1.0f, EPS);
    EXPECT_NEAR(mat.m[1][1], 1.0f, EPS);
    EXPECT_NEAR(mat.m[2][2], 1.0f, EPS);
    EXPECT_NEAR(mat.m[3][3], 1.0f, EPS);
    EXPECT_NEAR(mat.m[3][0], 0.0f, EPS);
    EXPECT_NEAR(mat.m[3][1], 0.0f, EPS);
    EXPECT_NEAR(mat.m[3][2], 0.0f, EPS);
}

TEST(PGA_Motor, ToMat4_Translation) {
    Motor m = Motor::Translation(5, 10, 15);
    Mat4 mat = m.ToMat4();

    EXPECT_NEAR(mat.m[3][0], 5.0f, EPS);
    EXPECT_NEAR(mat.m[3][1], 10.0f, EPS);
    EXPECT_NEAR(mat.m[3][2], 15.0f, EPS);
}

// ─── Line Tests ──────────────────────────────────────────────────────────

TEST(PGA_Line, ThroughPoints) {
    Line l = Line::ThroughPoints(Point(0, 0, 0), Point(1, 0, 0));
    Vec3 dir = l.Direction();
    EXPECT_NEAR(dir.x, 1.0f, EPS);
    EXPECT_NEAR(dir.y, 0.0f, EPS);
    EXPECT_NEAR(dir.z, 0.0f, EPS);
}

// ─── Distance Functions ──────────────────────────────────────────────────

TEST(PGA_Distance, PointToPoint) {
    f32 d = DistancePointPoint(Point(0, 0, 0), Point(3, 4, 0));
    EXPECT_NEAR(d, 5.0f, EPS);
}

TEST(PGA_Distance, PointToPlane) {
    Plane pl = Plane::FromNormalDist({0, 1, 0}, 0);
    f32 d = DistancePointPlane(Point(0, 5, 0), pl);
    EXPECT_NEAR(d, 5.0f, EPS);
}

// ─── Meet/Join ───────────────────────────────────────────────────────────

TEST(PGA_MeetJoin, PlaneMeet) {
    // XZ plane (normal Y) and YZ plane (normal X) → intersection is Z axis
    Plane p1(0, 1, 0, 0); // Y=0 plane
    Plane p2(1, 0, 0, 0); // X=0 plane
    Line l = Meet(p1, p2);
    Vec3 dir = l.Direction().Normalized();

    // Should be along Z axis
    EXPECT_NEAR(Abs(dir.z), 1.0f, EPS);
}
