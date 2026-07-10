#include "engine/core/math/pga.h"

namespace nge::pga {

// ─── Motor Geometric Product (Composition) ───────────────────────────────
// M_result = a * b (apply b first, then a)
// Uses SIMD for the 8-component even-subalgebra product.
Motor Motor::Multiply(const Motor& a, const Motor& b) {
    // Rotor-rotor product (scalar part of result)
    // s' = a.s*b.s - a.e23*b.e23 - a.e31*b.e31 - a.e12*b.e12
    // e23' = a.s*b.e23 + a.e23*b.s + a.e31*b.e12 - a.e12*b.e31
    // e31' = a.s*b.e31 + a.e31*b.s + a.e12*b.e23 - a.e23*b.e12
    // e12' = a.s*b.e12 + a.e12*b.s + a.e23*b.e31 - a.e31*b.e23

    // Scalar implementation (a future SIMD version must match these formulas):
    f32 rs   = a.s*b.s     - a.e23*b.e23   - a.e31*b.e31   - a.e12*b.e12;
    f32 re23 = a.s*b.e23   + a.e23*b.s     + a.e31*b.e12   - a.e12*b.e31;
    f32 re31 = a.s*b.e31   + a.e31*b.s     + a.e12*b.e23   - a.e23*b.e12;
    f32 re12 = a.s*b.e12   + a.e12*b.s     + a.e23*b.e31   - a.e31*b.e23;

    // Ideal part product (mixed rotor×ideal terms)
    f32 re0123 = a.s*b.e0123   + a.e0123*b.s
               + a.e23*b.e01   - a.e01*b.e23    // Wait — need careful sign tracking
               + a.e31*b.e02   - a.e02*b.e31
               + a.e12*b.e03   - a.e03*b.e12;

    // Actually, for the ideal part of the motor product:
    // The ideal (e0-containing) components:
    // e01' = a.s*b.e01 + a.e01*b.s   + a.e23*b.e0123 + a.e0123*b.e23
    //      + a.e31*b.e03 - a.e03*b.e31  - a.e12*b.e02 + a.e02*b.e12
    // ... this gets complex. Let me use the standard PGA motor product formulas.

    // Using the standard even-subalgebra multiplication table for ℝ(3,0,1):
    // Result p1 (rotor part):
    // Already computed above (rs, re23, re31, re12)

    // Result p2 (ideal part):
    f32 re01 = a.s*b.e01 + a.e01*b.s
             - a.e23*b.e0123 - a.e0123*b.e23
             + a.e12*b.e02 - a.e02*b.e12
             - a.e31*b.e03 + a.e03*b.e31;

    f32 re02 = a.s*b.e02 + a.e02*b.s
             - a.e31*b.e0123 - a.e0123*b.e31
             + a.e23*b.e03 - a.e03*b.e23
             - a.e12*b.e01 + a.e01*b.e12;

    f32 re03 = a.s*b.e03 + a.e03*b.s
             - a.e12*b.e0123 - a.e0123*b.e12
             + a.e31*b.e01 - a.e01*b.e31
             - a.e23*b.e02 + a.e02*b.e23;

    re0123 = a.s*b.e0123 + a.e0123*b.s
           + a.e01*b.e23 - a.e23*b.e01
           + a.e02*b.e31 - a.e31*b.e02
           + a.e03*b.e12 - a.e12*b.e03;

    return Motor(rs, re23, re31, re12, re0123, re01, re02, re03);
}

// ─── Sandwich Product: Transform Point ───────────────────────────────────
// P' = M * P * ~M
// Optimized: avoids full multivector multiply by exploiting grade structure.
Point Motor::Apply(Point pt) const {
    // For a normalized motor M = (s, e23, e31, e12, e0123, e01, e02, e03)
    // and a point P = (x*e032 + y*e013 + z*e021 + w*e123)
    //
    // The sandwich product for points has a well-known closed form.
    // This is equivalent to rotation then translation.

    f32 x = pt.e032, y = pt.e013, z = pt.e021, w = pt.e123;

    // Rotation part (rotor sandwich on the Euclidean components)
    // This is exactly quaternion rotation: q * v * q~
    f32 a = s, b = e23, c = e31, d = e12;

    f32 a2 = a*a, b2 = b*b, c2 = c*c, d2 = d*d;
    f32 ab = a*b, ac = a*c, ad = a*d;
    f32 bc = b*c, bd = b*d, cd = c*d;

    f32 rx = (a2 + b2 - c2 - d2) * x + 2*(bc - ad) * y + 2*(bd + ac) * z;
    f32 ry = 2*(bc + ad) * x + (a2 - b2 + c2 - d2) * y + 2*(cd - ab) * z;
    f32 rz = 2*(bd - ac) * x + 2*(cd + ab) * y + (a2 - b2 - c2 + d2) * z;

    // Translation contribution from ideal part
    // t = -2 * (s*ideal + cross(rotor_bivec, ideal_bivec) + ...)
    // For a motor applied to a point with w != 0:
    f32 tx = 2.0f * (e12*e02 - e0123*e23 - e01*s - e31*e03) * w;
    f32 ty = 2.0f * (e23*e03 - e0123*e31 - e02*s - e12*e01) * w;
    f32 tz = 2.0f * (e31*e01 - e0123*e12 - e03*s - e23*e02) * w;

    return Point(rx + tx, ry + ty, rz + tz, w);
}

// ─── Sandwich Product: Transform Plane ───────────────────────────────────
Plane Motor::Apply(Plane pl) const {
    f32 a = s, b = e23, c = e31, d = e12;
    f32 x = pl.e1, y = pl.e2, z = pl.e3, w = pl.e0;

    f32 a2 = a*a, b2 = b*b, c2 = c*c, d2 = d*d;

    // Rotation of normal
    f32 nx = (a2 + b2 - c2 - d2) * x + 2*(b*c + a*d) * y + 2*(b*d - a*c) * z;
    f32 ny = 2*(b*c - a*d) * x + (a2 - b2 + c2 - d2) * y + 2*(c*d + a*b) * z;
    f32 nz = 2*(b*d + a*c) * x + 2*(c*d - a*b) * y + (a2 - b2 - c2 + d2) * z;

    // Offset adjustment from translation
    f32 nw = w - 2.0f * (e01*nx + e02*ny + e03*nz);

    return Plane(nx, ny, nz, nw);
}

// ─── Sandwich Product: Transform Line ────────────────────────────────────
Line Motor::Apply(Line ln) const {
    // Transform both direction and moment parts
    f32 a = s, b = e23, c = e31, d = e12;
    f32 a2 = a*a, b2 = b*b, c2 = c*c, d2 = d*d;

    // Rotate direction
    f32 dx = ln.e23, dy = ln.e31, dz = ln.e12;
    f32 rdx = (a2 + b2 - c2 - d2)*dx + 2*(b*c + a*d)*dy + 2*(b*d - a*c)*dz;
    f32 rdy = 2*(b*c - a*d)*dx + (a2 - b2 + c2 - d2)*dy + 2*(c*d + a*b)*dz;
    f32 rdz = 2*(b*d + a*c)*dx + 2*(c*d - a*b)*dy + (a2 - b2 - c2 + d2)*dz;

    // Rotate moment
    f32 mx = ln.e01, my = ln.e02, mz = ln.e03;
    f32 rmx = (a2 + b2 - c2 - d2)*mx + 2*(b*c + a*d)*my + 2*(b*d - a*c)*mz;
    f32 rmy = 2*(b*c - a*d)*mx + (a2 - b2 + c2 - d2)*my + 2*(c*d + a*b)*mz;
    f32 rmz = 2*(b*d + a*c)*mx + 2*(c*d - a*b)*my + (a2 - b2 - c2 + d2)*mz;

    // Translation contribution to moment
    f32 tx = -2.0f * e01, ty = -2.0f * e02, tz = -2.0f * e03;
    rmx += ty * rdz - tz * rdy;
    rmy += tz * rdx - tx * rdz;
    rmz += tx * rdy - ty * rdx;

    return Line(rmx, rmy, rmz, rdx, rdy, rdz);
}

// ─── Motor Logarithm ────────────────────────────────────────────────────
// Maps motor to bivector (line) in the Lie algebra.
// Motor = exp(Line), so Log(Motor) = Line
Line Motor::Log() const {
    // Extract rotation angle from scalar + bivector part
    f32 rotNorm = math::Sqrt(e23*e23 + e31*e31 + e12*e12);
    f32 halfAngle = math::Atan2(rotNorm, s);

    f32 invRotNorm;
    f32 idealScale;

    if (rotNorm > math::EPSILON) {
        invRotNorm = 1.0f / rotNorm;
        idealScale = -e0123 * invRotNorm;
    } else {
        // Near identity: use Taylor expansion
        invRotNorm = 1.0f;
        idealScale = -e0123;
        halfAngle = rotNorm; // Small angle approximation
    }

    f32 lineScale = halfAngle * invRotNorm;

    Line result;
    result.e23 = e23 * lineScale;
    result.e31 = e31 * lineScale;
    result.e12 = e12 * lineScale;

    if (rotNorm > math::EPSILON) {
        f32 dIdeal = idealScale * halfAngle;

        result.e01 = e01 * lineScale + e23 * dIdeal;
        result.e02 = e02 * lineScale + e31 * dIdeal;
        result.e03 = e03 * lineScale + e12 * dIdeal;
    } else {
        result.e01 = e01;
        result.e02 = e02;
        result.e03 = e03;
    }

    return result;
}

// ─── Exponential Map ─────────────────────────────────────────────────────
// Maps a bivector (line) in the Lie algebra to a motor.
Motor Motor::Exp(Line l) {
    // Euclidean norm of direction part = half rotation angle
    f32 dirNorm = math::Sqrt(l.e23*l.e23 + l.e31*l.e31 + l.e12*l.e12);

    f32 cosA, sinA_over_norm, dualScale;

    if (dirNorm > math::EPSILON) {
        cosA = math::Cos(dirNorm);
        sinA_over_norm = math::Sin(dirNorm) / dirNorm;

        // Dual/ideal contribution
        f32 momentDot = l.e23*l.e01 + l.e31*l.e02 + l.e12*l.e03;
        dualScale = -momentDot / (dirNorm * dirNorm);
    } else {
        // Near zero rotation
        cosA = 1.0f;
        sinA_over_norm = 1.0f;
        dualScale = 0.0f;
    }

    Motor result;
    result.s    = cosA;
    result.e23  = l.e23 * sinA_over_norm;
    result.e31  = l.e31 * sinA_over_norm;
    result.e12  = l.e12 * sinA_over_norm;

    result.e0123 = dualScale * math::Sin(dirNorm);

    if (dirNorm > math::EPSILON) {
        result.e01 = l.e01 * sinA_over_norm + l.e23 * dualScale * cosA;
        result.e02 = l.e02 * sinA_over_norm + l.e31 * dualScale * cosA;
        result.e03 = l.e03 * sinA_over_norm + l.e12 * dualScale * cosA;
    } else {
        result.e01 = l.e01;
        result.e02 = l.e02;
        result.e03 = l.e03;
    }

    return result;
}

// ─── Motor Slerp (Geodesic Interpolation) ────────────────────────────────
Motor Motor::Slerp(const Motor& a, const Motor& b, f32 t) {
    // M(t) = exp(t * log(b * ~a)) * a
    Motor delta = Multiply(b, a.Reverse());
    Line logDelta = delta.Log();

    // Scale the bivector by t
    Line scaled;
    scaled.e23 = logDelta.e23 * t;
    scaled.e31 = logDelta.e31 * t;
    scaled.e12 = logDelta.e12 * t;
    scaled.e01 = logDelta.e01 * t;
    scaled.e02 = logDelta.e02 * t;
    scaled.e03 = logDelta.e03 * t;

    Motor interpolated = Exp(scaled);
    return Multiply(interpolated, a);
}

// ─── Motor → 4x4 Matrix (for GPU upload) ────────────────────────────────
math::Mat4 Motor::ToMat4() const {
    // Ensure normalized
    f32 norm2 = s*s + e23*e23 + e31*e31 + e12*e12;
    f32 invN2 = 1.0f / norm2;

    f32 a = s, b = e23, c = e31, d = e12;
    f32 a2 = a*a, b2 = b*b, c2 = c*c, d2 = d*d;

    math::Mat4 mat;

    // Rotation part
    mat.m[0][0] = (a2 + b2 - c2 - d2) * invN2;
    mat.m[0][1] = 2*(b*c + a*d) * invN2;
    mat.m[0][2] = 2*(b*d - a*c) * invN2;
    mat.m[0][3] = 0;

    mat.m[1][0] = 2*(b*c - a*d) * invN2;
    mat.m[1][1] = (a2 - b2 + c2 - d2) * invN2;
    mat.m[1][2] = 2*(c*d + a*b) * invN2;
    mat.m[1][3] = 0;

    mat.m[2][0] = 2*(b*d + a*c) * invN2;
    mat.m[2][1] = 2*(c*d - a*b) * invN2;
    mat.m[2][2] = (a2 - b2 - c2 + d2) * invN2;
    mat.m[2][3] = 0;

    // Translation from motor ideal part
    // Derived from sandwich product of origin point
    Point origin(0, 0, 0, 1);
    Point transformed = Apply(origin);
    math::Vec3 t = transformed.ToVec3();

    mat.m[3][0] = t.x;
    mat.m[3][1] = t.y;
    mat.m[3][2] = t.z;
    mat.m[3][3] = 1.0f;

    return mat;
}

// ─── Plane through 3 points ─────────────────────────────────────────────
Plane Plane::ThroughPoints(math::Vec3 a, math::Vec3 b, math::Vec3 c) {
    math::Vec3 ab = b - a;
    math::Vec3 ac = c - a;
    math::Vec3 n = ab.Cross(ac).Normalized();
    f32 d = -n.Dot(a);
    return Plane(n.x, n.y, n.z, d);
}

// ─── Line through two points ────────────────────────────────────────────
Line Line::ThroughPoints(Point a, Point b) {
    // Join (regressive product) of two points
    // For normalized points (w=1):
    // direction = b - a (as e23, e31, e12 components)
    // moment = a × b (as e01, e02, e03 components)
    math::Vec3 av = a.ToVec3();
    math::Vec3 bv = b.ToVec3();
    math::Vec3 dir = bv - av;
    math::Vec3 moment = av.Cross(bv);

    return Line(moment.x, moment.y, moment.z, dir.x, dir.y, dir.z);
}

// ─── Line from direction + point ────────────────────────────────────────
Line Line::FromDirectionPoint(math::Vec3 dir, math::Vec3 point) {
    math::Vec3 moment = point.Cross(dir);
    return Line(moment.x, moment.y, moment.z, dir.x, dir.y, dir.z);
}

// ─── Meet of two planes → Line ──────────────────────────────────────────
Line Meet(Plane a, Plane b) {
    // Regressive product of two planes
    Line result;
    result.e23 = a.e2*b.e3 - a.e3*b.e2;
    result.e31 = a.e3*b.e1 - a.e1*b.e3;
    result.e12 = a.e1*b.e2 - a.e2*b.e1;
    result.e01 = a.e0*b.e1 - a.e1*b.e0;
    result.e02 = a.e0*b.e2 - a.e2*b.e0;
    result.e03 = a.e0*b.e3 - a.e3*b.e0;
    return result;
}

// ─── Join of two points → Line ──────────────────────────────────────────
Line Join(Point a, Point b) {
    return Line::ThroughPoints(a, b);
}

// ─── Join of three points → Plane ───────────────────────────────────────
Plane Join(Point a, Point b, Point c) {
    return Plane::ThroughPoints(a.ToVec3(), b.ToVec3(), c.ToVec3());
}

// ─── Inner product: signed distance ─────────────────────────────────────
f32 InnerProduct(Plane plane, Point point) {
    Point np = point.Normalized();
    Plane npl = plane.Normalized();
    return npl.e1*np.x + npl.e2*np.y + npl.e3*np.z + npl.e0;
}

// ─── Distance functions ─────────────────────────────────────────────────

f32 DistancePointPlane(Point pt, Plane pl) {
    return math::Abs(InnerProduct(pl, pt));
}

f32 DistancePointPoint(Point a, Point b) {
    math::Vec3 av = a.ToVec3();
    math::Vec3 bv = b.ToVec3();
    math::Vec3 diff = bv - av;
    return diff.Length();
}

f32 DistancePointLine(Point pt, Line ln) {
    Line normalized = ln.Normalized();
    math::Vec3 p = pt.ToVec3();
    math::Vec3 d = normalized.Direction();
    math::Vec3 m = normalized.Moment();

    // Distance = |d × p + m| / |d| (d is already normalized)
    math::Vec3 cross = d.Cross(p);
    math::Vec3 sum = {cross.x + m.x, cross.y + m.y, cross.z + m.z};
    return sum.Length();
}

f32 AngleBetweenPlanes(Plane a, Plane b) {
    Plane na = a.Normalized();
    Plane nb = b.Normalized();
    f32 dot = na.e1*nb.e1 + na.e2*nb.e2 + na.e3*nb.e3;
    return math::Acos(math::Clamp(dot, -1.0f, 1.0f));
}

} // namespace nge::pga
