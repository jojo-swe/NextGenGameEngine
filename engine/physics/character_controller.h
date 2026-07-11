#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/physics/physics_world.h"

namespace nge::physics {

// ─── Character Controller Configuration ──────────────────────────────────

struct CharacterControllerConfig {
    f32 radius = 0.4f;              // Capsule radius
    f32 halfHeight = 0.9f;          // Capsule half-height (cylinder part)
    f32 slopeLimit = 0.7f;          // Max walkable slope (cos of max angle, 0.7 ≈ 45°)
    f32 stepHeight = 0.3f;          // Max step height the controller can climb
    f32 skinWidth = 0.08f;          // Buffer around capsule for collision queries
    f32 maxSpeed = 8.0f;            // Max horizontal speed
    f32 acceleration = 40.0f;       // Horizontal acceleration
    f32 deceleration = 60.0f;       // Horizontal deceleration when no input
    f32 jumpSpeed = 6.0f;           // Initial jump velocity
    f32 gravityScale = 1.0f;        // Multiplier on world gravity
    f32 maxFallSpeed = -40.0f;      // Terminal velocity
    u16 collisionLayer = 1;         // Layer this controller collides with
};

// ─── Character Controller State ───────────────────────────────────────────

struct CharacterControllerState {
    bool  isGrounded = false;
    bool  wasGrounded = false;
    bool  isJumping = false;
    f32   groundDistance = 0;
    math::Vec3 groundNormal = {0, 1, 0};
    math::Vec3 velocity = {0, 0, 0};
    math::Vec3 position = {0, 0, 0};
};

// ─── Character Controller ─────────────────────────────────────────────────
// Kinematic character controller that uses sphere sweeps against the physics
// world. Does not use a physics body — it moves manually and resolves
// collisions by depenetration + sliding.

class CharacterController {
public:
    CharacterController() = default;
    ~CharacterController() = default;

    void Init(PhysicsWorld* world, const CharacterControllerConfig& config = {});
    void Shutdown();

    // Set position directly (e.g., spawn or teleport)
    void SetPosition(const math::Vec3& pos);
    math::Vec3 GetPosition() const { return m_state.position; }

    // Set desired horizontal movement direction (normalized, world-space)
    void SetMoveDirection(const math::Vec3& dir);
    void Jump();

    // Per-frame update — applies gravity, moves, resolves collisions
    void Update(f32 deltaTime);

    const CharacterControllerState& GetState() const { return m_state; }
    const CharacterControllerConfig& GetConfig() const { return m_config; }

    bool IsGrounded() const { return m_state.isGrounded; }

private:
    void GroundCheck();
    void MoveHorizontal(f32 deltaTime);
    void MoveVertical(f32 deltaTime);
    void ApplyGravity(f32 deltaTime);

    PhysicsWorld* m_world = nullptr;
    CharacterControllerConfig m_config;
    CharacterControllerState m_state;
    math::Vec3 m_moveDir = {0, 0, 0};
};

} // namespace nge::physics
