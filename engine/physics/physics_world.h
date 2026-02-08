#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/core/math/pga.h"
#include <vector>
#include <memory>
#include <functional>

namespace nge::physics {

// ─── Physics Types ───────────────────────────────────────────────────────

using BodyId = u32;
inline constexpr BodyId INVALID_BODY = UINT32_MAX;

enum class BodyType : u8 {
    Static,
    Dynamic,
    Kinematic,
};

enum class ShapeType : u8 {
    Box,
    Sphere,
    Capsule,
    ConvexHull,
    TriangleMesh,
    HeightField,
};

// ─── Collision Shape ─────────────────────────────────────────────────────

struct ShapeDesc {
    ShapeType type = ShapeType::Box;

    // Box
    math::Vec3 halfExtents = {0.5f, 0.5f, 0.5f};

    // Sphere
    f32 radius = 0.5f;

    // Capsule
    f32 capsuleRadius = 0.5f;
    f32 capsuleHalfHeight = 0.5f;

    // Convex hull / triangle mesh (index into mesh data)
    const math::Vec3* vertices = nullptr;
    u32 vertexCount = 0;
    const u32* indices = nullptr;
    u32 indexCount = 0;
};

// ─── Body Description ────────────────────────────────────────────────────

struct BodyDesc {
    BodyType   type = BodyType::Dynamic;
    ShapeDesc  shape;
    math::Vec3 position = {0, 0, 0};
    math::Vec4 rotation = {0, 0, 0, 1}; // Quaternion (x,y,z,w)
    math::Vec3 linearVelocity = {0, 0, 0};
    math::Vec3 angularVelocity = {0, 0, 0};
    f32        mass = 1.0f;
    f32        friction = 0.5f;
    f32        restitution = 0.3f;
    f32        linearDamping = 0.05f;
    f32        angularDamping = 0.05f;
    bool       isSensor = false;      // Trigger volume (no physical response)
    u64        userData = 0;          // Arbitrary user data (e.g., ECS entity id)
    u16        collisionLayer = 0;
    u16        collisionMask = 0xFFFF;
};

// ─── Ray Cast / Shape Cast Results ───────────────────────────────────────

struct RayCastResult {
    bool       hit = false;
    math::Vec3 hitPoint;
    math::Vec3 hitNormal;
    f32        distance = 0;
    BodyId     bodyId = INVALID_BODY;
    u64        userData = 0;
};

struct ContactPoint {
    math::Vec3 position;
    math::Vec3 normal;
    f32        penetration;
    BodyId     bodyA;
    BodyId     bodyB;
};

// ─── Collision Callback ──────────────────────────────────────────────────
using ContactCallback = std::function<void(const ContactPoint& contact)>;
using TriggerCallback = std::function<void(BodyId trigger, BodyId other, bool entered)>;

// ─── Physics World Configuration ─────────────────────────────────────────

struct PhysicsWorldConfig {
    math::Vec3 gravity = {0, -9.81f, 0};
    u32  maxBodies = 65536;
    u32  maxBodyPairs = 65536;
    u32  maxContactConstraints = 65536;
    u32  numThreads = 0; // 0 = auto-detect
    f32  fixedTimeStep = 1.0f / 60.0f;
    u32  maxSubSteps = 4;
};

// ─── Physics World ───────────────────────────────────────────────────────
// Wrapper around Jolt Physics engine.
// Provides a clean interface for the game engine to interact with physics.
// When Jolt is integrated via vcpkg, this implementation wraps JPH classes.

class PhysicsWorld {
public:
    PhysicsWorld() = default;
    ~PhysicsWorld();

    bool Init(const PhysicsWorldConfig& config = {});
    void Shutdown();

    // Simulation
    void Step(f32 deltaTime);

    // Body management
    BodyId CreateBody(const BodyDesc& desc);
    void   DestroyBody(BodyId id);
    bool   IsBodyValid(BodyId id) const;

    // Body state
    math::Vec3 GetPosition(BodyId id) const;
    math::Vec4 GetRotation(BodyId id) const; // Quaternion
    math::Vec3 GetLinearVelocity(BodyId id) const;
    math::Vec3 GetAngularVelocity(BodyId id) const;

    void SetPosition(BodyId id, const math::Vec3& pos);
    void SetRotation(BodyId id, const math::Vec4& rot);
    void SetLinearVelocity(BodyId id, const math::Vec3& vel);
    void SetAngularVelocity(BodyId id, const math::Vec3& vel);

    // Convert to/from PGA Motor
    math::pga::Motor GetMotor(BodyId id) const;
    void SetMotor(BodyId id, const math::pga::Motor& motor);

    // Forces
    void AddForce(BodyId id, const math::Vec3& force);
    void AddImpulse(BodyId id, const math::Vec3& impulse);
    void AddTorque(BodyId id, const math::Vec3& torque);

    // Queries
    RayCastResult RayCast(const math::Vec3& origin, const math::Vec3& direction,
                           f32 maxDistance, u16 layerMask = 0xFFFF) const;
    std::vector<RayCastResult> RayCastAll(const math::Vec3& origin, const math::Vec3& direction,
                                            f32 maxDistance, u16 layerMask = 0xFFFF) const;
    std::vector<BodyId> OverlapSphere(const math::Vec3& center, f32 radius,
                                        u16 layerMask = 0xFFFF) const;
    std::vector<BodyId> OverlapBox(const math::Vec3& center, const math::Vec3& halfExtents,
                                     const math::Vec4& rotation, u16 layerMask = 0xFFFF) const;

    // Callbacks
    void SetContactCallback(ContactCallback cb) { m_contactCallback = std::move(cb); }
    void SetTriggerCallback(TriggerCallback cb) { m_triggerCallback = std::move(cb); }

    // Configuration
    void SetGravity(const math::Vec3& gravity);
    math::Vec3 GetGravity() const;

    // Debug
    u32 GetActiveBodyCount() const;
    u32 GetTotalBodyCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    PhysicsWorldConfig m_config;
    ContactCallback    m_contactCallback;
    TriggerCallback    m_triggerCallback;
};

} // namespace nge::physics
