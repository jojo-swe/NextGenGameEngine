#include "engine/physics/character_controller.h"
#include "engine/core/math/math_types.h"
#include <cmath>
#include <algorithm>

namespace nge::physics {

void CharacterController::Init(PhysicsWorld* world, const CharacterControllerConfig& config) {
    m_world = world;
    m_config = config;
    m_state = {};
}

void CharacterController::Shutdown() {
    m_world = nullptr;
}

void CharacterController::SetPosition(const math::Vec3& pos) {
    m_state.position = pos;
}

void CharacterController::SetMoveDirection(const math::Vec3& dir) {
    m_moveDir = dir;
}

void CharacterController::Jump() {
    if (m_state.isGrounded) {
        m_state.velocity.y = m_config.jumpSpeed;
        m_state.isJumping = true;
        m_state.isGrounded = false;
    }
}

void CharacterController::Update(f32 deltaTime) {
    if (!m_world) return;

    m_state.wasGrounded = m_state.isGrounded;

    ApplyGravity(deltaTime);
    MoveHorizontal(deltaTime);
    MoveVertical(deltaTime);
    GroundCheck();
}

void CharacterController::ApplyGravity(f32 deltaTime) {
    if (!m_state.isGrounded || m_state.isJumping) {
        math::Vec3 g = m_world->GetGravity();
        m_state.velocity.y += g.y * m_config.gravityScale * deltaTime;
        m_state.velocity.y = math::Max(m_state.velocity.y, m_config.maxFallSpeed);
    } else {
        m_state.velocity.y = 0;
    }
}

void CharacterController::MoveHorizontal(f32 deltaTime) {
    // Compute desired horizontal velocity from input
    math::Vec3 desiredVel = {0, 0, 0};
    f32 inputLen = std::sqrt(m_moveDir.x * m_moveDir.x + m_moveDir.z * m_moveDir.z);
    if (inputLen > 0.001f) {
        f32 inv = 1.0f / inputLen;
        desiredVel.x = m_moveDir.x * inv * m_config.maxSpeed;
        desiredVel.z = m_moveDir.z * inv * m_config.maxSpeed;
    }

    // Smoothly accelerate/decelerate toward desired velocity
    f32 accel = (inputLen > 0.001f) ? m_config.acceleration : m_config.deceleration;
    math::Vec3 curHoriz = {m_state.velocity.x, 0, m_state.velocity.z};
    math::Vec3 diff = desiredVel - curHoriz;
    f32 diffLen = std::sqrt(diff.x * diff.x + diff.z * diff.z);
    if (diffLen > 0.001f) {
        f32 step = accel * deltaTime;
        if (step >= diffLen) {
            m_state.velocity.x = desiredVel.x;
            m_state.velocity.z = desiredVel.z;
        } else {
            f32 ratio = step / diffLen;
            m_state.velocity.x += diff.x * ratio;
            m_state.velocity.z += diff.z * ratio;
        }
    }

    // Move horizontally with collision (sphere sweep + slide)
    math::Vec3 horizVel = {m_state.velocity.x, 0, m_state.velocity.z};
    f32 moveDist = std::sqrt(horizVel.x * horizVel.x + horizVel.z * horizVel.z) * deltaTime;
    if (moveDist > 0.0001f) {
        math::Vec3 moveDir = horizVel * (1.0f / (moveDist / deltaTime));

        // Sweep from the capsule center (lower sphere)
        math::Vec3 sweepOrigin = m_state.position;
        sweepOrigin.y -= m_config.halfHeight;

        auto hit = m_world->ShapeCastSphere(
            sweepOrigin, m_config.radius + m_config.skinWidth,
            moveDir, moveDist, m_config.collisionLayer);

        if (hit.hit) {
            // Project velocity onto the collision plane (slide)
            math::Vec3 normal = hit.hitNormal;
            // Remove the normal component from velocity
            f32 dot = m_state.velocity.x * normal.x + m_state.velocity.z * normal.z;
            m_state.velocity.x -= normal.x * dot;
            m_state.velocity.z -= normal.z * dot;

            // Move up to the hit point
            f32 actualDist = math::Max(hit.distance - m_config.skinWidth, 0.0f);
            m_state.position.x += moveDir.x * actualDist;
            m_state.position.z += moveDir.z * actualDist;
        } else {
            m_state.position.x += moveDir.x * moveDist;
            m_state.position.z += moveDir.z * moveDist;
        }
    }
}

void CharacterController::MoveVertical(f32 deltaTime) {
    f32 vertVel = m_state.velocity.y;
    f32 moveDist = vertVel * deltaTime;

    if (std::abs(moveDist) < 0.0001f) return;

    math::Vec3 moveDir = {0, moveDist > 0 ? 1.0f : -1.0f, 0};
    f32 absDist = std::abs(moveDist);

    // Sweep from the appropriate end of the capsule
    math::Vec3 sweepOrigin = m_state.position;
    if (moveDist < 0) {
        // Moving down — sweep from bottom sphere
        sweepOrigin.y -= m_config.halfHeight;
    } else {
        // Moving up — sweep from top sphere
        sweepOrigin.y += m_config.halfHeight;
    }

    auto hit = m_world->ShapeCastSphere(
        sweepOrigin, m_config.radius + m_config.skinWidth,
        moveDir, absDist, m_config.collisionLayer);

    if (hit.hit) {
        f32 actualDist = math::Max(hit.distance - m_config.skinWidth, 0.0f);
        m_state.position.y += moveDir.y * actualDist;

        if (moveDist < 0) {
            // Landed on ground
            m_state.velocity.y = 0;
            m_state.isJumping = false;
            m_state.isGrounded = true;
            m_state.groundNormal = hit.hitNormal;
            m_state.groundDistance = hit.distance;
        } else {
            // Hit ceiling
            m_state.velocity.y = 0;
        }
    } else {
        m_state.position.y += moveDist;
    }
}

void CharacterController::GroundCheck() {
    if (m_state.velocity.y > 0.001f) {
        m_state.isGrounded = false;
        return;
    }

    // Probe downward from bottom of capsule
    f32 probeDist = m_config.skinWidth + 0.05f;
    math::Vec3 probeOrigin = m_state.position;
    probeOrigin.y -= m_config.halfHeight;

    auto hit = m_world->ShapeCastSphere(
        probeOrigin, m_config.radius + m_config.skinWidth,
        {0, -1, 0}, probeDist, m_config.collisionLayer);

    if (hit.hit) {
        // Check slope — ground normal must be within slope limit
        f32 slopeDot = hit.hitNormal.y; // cos of angle from up
        if (slopeDot >= m_config.slopeLimit) {
            m_state.isGrounded = true;
            m_state.isJumping = false;
            m_state.velocity.y = 0;
            m_state.groundNormal = hit.hitNormal;
            m_state.groundDistance = hit.distance;
            return;
        }
    }

    // Also check ground plane at y=0
    if (m_state.position.y - m_config.halfHeight - m_config.radius <= 0.001f) {
        m_state.isGrounded = true;
        m_state.isJumping = false;
        m_state.velocity.y = 0;
        m_state.groundNormal = {0, 1, 0};
        m_state.groundDistance = m_state.position.y - m_config.halfHeight - m_config.radius;
        return;
    }

    m_state.isGrounded = false;
}

} // namespace nge::physics
