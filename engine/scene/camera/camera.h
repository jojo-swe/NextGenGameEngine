#pragma once

#include "engine/core/types.h"
#include "engine/core/math/pga.h"
#include "engine/core/math/math_types.h"

namespace nge::scene {

// ─── Camera Projection ───────────────────────────────────────────────────

enum class ProjectionType : u8 {
    Perspective,
    Orthographic,
    InfinitePerspective, // Reversed-Z infinite far plane (preferred for depth precision)
};

struct CameraProjection {
    ProjectionType type = ProjectionType::InfinitePerspective;
    f32 fovY       = math::PI / 3.0f; // 60 degrees
    f32 aspectRatio = 16.0f / 9.0f;
    f32 nearPlane  = 0.1f;
    f32 farPlane   = 10000.0f;         // Ignored for infinite perspective

    // Orthographic extents
    f32 orthoWidth  = 20.0f;
    f32 orthoHeight = 20.0f;

    math::Mat4 GetProjectionMatrix() const {
        switch (type) {
            case ProjectionType::Perspective:
                return math::Mat4::Perspective(fovY, aspectRatio, nearPlane, farPlane);
            case ProjectionType::InfinitePerspective:
                return math::Mat4::InfinitePerspective(fovY, aspectRatio, nearPlane);
            case ProjectionType::Orthographic:
                return math::Mat4::Ortho(
                    -orthoWidth * 0.5f, orthoWidth * 0.5f,
                    -orthoHeight * 0.5f, orthoHeight * 0.5f,
                    nearPlane, farPlane);
        }
        return math::Mat4::Identity();
    }
};

// ─── Camera Component ────────────────────────────────────────────────────
// Combines a PGA Motor (for view transform) with projection parameters.
// The view matrix is derived from the inverse of the camera's world motor.

struct Camera {
    CameraProjection projection;
    bool isActive = false;      // Only one camera is active at a time

    // Temporal jitter for TAA/TSR
    f32 jitterX = 0;
    f32 jitterY = 0;
    u32 jitterIndex = 0;

    // ─── View matrix from motor ───────────────────────────────────────
    // The camera's world motor is stored in its Transform component.
    // View matrix = inverse(world_motor).ToMat4()

    math::Mat4 GetViewMatrix(const pga::Motor& worldMotor) const {
        return worldMotor.Reverse().ToMat4();
    }

    math::Mat4 GetViewProjectionMatrix(const pga::Motor& worldMotor) const {
        math::Mat4 view = GetViewMatrix(worldMotor);
        math::Mat4 proj = projection.GetProjectionMatrix();

        // Apply jitter for temporal effects
        math::Mat4 jitteredProj = proj;
        if (jitterX != 0 || jitterY != 0) {
            jitteredProj.m[2][0] += jitterX;
            jitteredProj.m[2][1] += jitterY;
        }

        return jitteredProj * view;
    }

    // ─── Frustum planes extraction ────────────────────────────────────
    // Extracts 6 frustum planes from the view-projection matrix.
    // Used for CPU/GPU frustum culling.
    struct FrustumPlanes {
        math::Vec4 planes[6]; // Left, Right, Bottom, Top, Near, Far
    };

    static FrustumPlanes ExtractFrustumPlanes(const math::Mat4& vp) {
        FrustumPlanes fp;

        // Left:   row3 + row0
        fp.planes[0] = {vp.m[0][3] + vp.m[0][0], vp.m[1][3] + vp.m[1][0],
                         vp.m[2][3] + vp.m[2][0], vp.m[3][3] + vp.m[3][0]};
        // Right:  row3 - row0
        fp.planes[1] = {vp.m[0][3] - vp.m[0][0], vp.m[1][3] - vp.m[1][0],
                         vp.m[2][3] - vp.m[2][0], vp.m[3][3] - vp.m[3][0]};
        // Bottom: row3 + row1
        fp.planes[2] = {vp.m[0][3] + vp.m[0][1], vp.m[1][3] + vp.m[1][1],
                         vp.m[2][3] + vp.m[2][1], vp.m[3][3] + vp.m[3][1]};
        // Top:    row3 - row1
        fp.planes[3] = {vp.m[0][3] - vp.m[0][1], vp.m[1][3] - vp.m[1][1],
                         vp.m[2][3] - vp.m[2][1], vp.m[3][3] - vp.m[3][1]};
        // Near:   row3 + row2
        fp.planes[4] = {vp.m[0][3] + vp.m[0][2], vp.m[1][3] + vp.m[1][2],
                         vp.m[2][3] + vp.m[2][2], vp.m[3][3] + vp.m[3][2]};
        // Far:    row3 - row2
        fp.planes[5] = {vp.m[0][3] - vp.m[0][2], vp.m[1][3] - vp.m[1][2],
                         vp.m[2][3] - vp.m[2][2], vp.m[3][3] - vp.m[3][2]};

        // Normalize each plane
        for (int i = 0; i < 6; ++i) {
            f32 len = math::Sqrt(fp.planes[i].x * fp.planes[i].x +
                                  fp.planes[i].y * fp.planes[i].y +
                                  fp.planes[i].z * fp.planes[i].z);
            if (len > math::EPSILON) {
                f32 invLen = 1.0f / len;
                fp.planes[i] = fp.planes[i] * invLen;
            }
        }

        return fp;
    }

    // ─── Temporal jitter ──────────────────────────────────────────────
    // Halton(2,3) sequence for sub-pixel jitter in TAA/TSR
    void AdvanceJitter(u32 screenWidth, u32 screenHeight) {
        jitterIndex++;
        // Halton base-2
        f32 hx = 0; { u32 i = jitterIndex; f32 f = 0.5f; while(i > 0) { hx += f * (i & 1); i >>= 1; f *= 0.5f; } }
        // Halton base-3
        f32 hy = 0; { u32 i = jitterIndex; f32 f = 1.0f/3.0f; while(i > 0) { hy += f * (i % 3); i /= 3; f /= 3.0f; } }

        jitterX = (hx - 0.5f) / static_cast<f32>(screenWidth);
        jitterY = (hy - 0.5f) / static_cast<f32>(screenHeight);
    }
};

// ─── Free-fly Camera Controller ──────────────────────────────────────────
// Utility for editor/debug camera movement.

class FreeFlyController {
public:
    f32 moveSpeed   = 10.0f;
    f32 lookSpeed   = 0.003f;
    f32 sprintMult  = 3.0f;

    f32 yaw   = 0;
    f32 pitch = 0;

    void Update(pga::Motor& motor, f32 deltaTime,
                bool forward, bool backward, bool left, bool right,
                bool up, bool down, bool sprint,
                f32 mouseDeltaX, f32 mouseDeltaY)
    {
        // Mouse look
        yaw   -= mouseDeltaX * lookSpeed;
        pitch -= mouseDeltaY * lookSpeed;
        pitch = math::Clamp(pitch, -math::HALF_PI + 0.01f, math::HALF_PI - 0.01f);

        // Build rotation motor from Euler angles
        pga::Motor rotY = pga::Motor::Rotation({0, 1, 0}, yaw);
        pga::Motor rotX = pga::Motor::Rotation({1, 0, 0}, pitch);
        pga::Motor rotation = pga::Motor::Multiply(rotY, rotX);

        // Movement
        f32 speed = moveSpeed * deltaTime * (sprint ? sprintMult : 1.0f);
        math::Vec3 move{0, 0, 0};

        if (forward)  move.z -= speed;
        if (backward) move.z += speed;
        if (right)    move.x += speed;
        if (left)     move.x -= speed;
        if (up)       move.y += speed;
        if (down)     move.y -= speed;

        // Transform movement by camera rotation
        pga::Point movePoint(move.x, move.y, move.z);
        pga::Point rotatedMove = rotation.Apply(movePoint);
        math::Vec3 worldMove = rotatedMove.ToVec3();

        // Get current position and add movement
        math::Vec3 currentPos = motor.Apply(pga::Point::Origin()).ToVec3();
        math::Vec3 newPos = currentPos + worldMove;

        // Rebuild motor: translate then rotate
        pga::Motor trans = pga::Motor::Translation(newPos);
        motor = pga::Motor::Multiply(trans, rotation);
    }
};

} // namespace nge::scene
