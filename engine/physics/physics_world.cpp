#include "engine/physics/physics_world.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <unordered_map>
#include <cmath>

// Jolt Physics integration — when available via vcpkg:
// #include <Jolt/Jolt.h>
// #include <Jolt/Physics/PhysicsSystem.h>
// #include <Jolt/Physics/Body/BodyCreationSettings.h>
// etc.

// For now, provide a simple stub implementation that stores body state
// and performs basic Euler integration. This will be replaced by Jolt.

namespace nge::physics {

// ─── Stub Implementation ─────────────────────────────────────────────────
// Minimal physics simulation without Jolt. Stores transforms and applies
// gravity + Euler integration for testing. No collision detection.

struct PhysicsWorld::Impl {
    struct Body {
        bool       active = false;
        BodyType   type   = BodyType::Static;
        math::Vec3 position;
        math::Vec4 rotation = {0, 0, 0, 1};
        math::Vec3 linearVelocity;
        math::Vec3 angularVelocity;
        f32        mass = 1.0f;
        f32        linearDamping = 0.05f;
        f32        angularDamping = 0.05f;
        f32        friction = 0.5f;
        f32        restitution = 0.3f;
        bool       isSensor = false;
        u64        userData = 0;
        math::Vec3 pendingForce;
        math::Vec3 pendingTorque;
    };

    std::vector<Body> bodies;
    std::vector<u32>  freeList;
    math::Vec3        gravity = {0, -9.81f, 0};
    f32               fixedTimeStep = 1.0f / 60.0f;
    u32               maxSubSteps = 4;
    f32               accumulator = 0;
    u32               activeCount = 0;
};

PhysicsWorld::~PhysicsWorld() {
    Shutdown();
}

bool PhysicsWorld::Init(const PhysicsWorldConfig& config) {
    m_config = config;
    m_impl = std::make_unique<Impl>();
    m_impl->gravity = config.gravity;
    m_impl->fixedTimeStep = config.fixedTimeStep;
    m_impl->maxSubSteps = config.maxSubSteps;
    m_impl->bodies.reserve(config.maxBodies);

    NGE_LOG_INFO("Physics world initialized (stub): gravity=({},{},{}), dt={:.4f}",
                 config.gravity.x, config.gravity.y, config.gravity.z, config.fixedTimeStep);
    return true;
}

void PhysicsWorld::Shutdown() {
    m_impl.reset();
}

void PhysicsWorld::Step(f32 deltaTime) {
    if (!m_impl) return;

    m_impl->accumulator += deltaTime;
    u32 steps = 0;

    while (m_impl->accumulator >= m_impl->fixedTimeStep && steps < m_impl->maxSubSteps) {
        f32 dt = m_impl->fixedTimeStep;

        for (auto& body : m_impl->bodies) {
            if (!body.active || body.type == BodyType::Static) continue;

            if (body.type == BodyType::Dynamic) {
                // Apply gravity
                math::Vec3 accel = m_impl->gravity + body.pendingForce * (1.0f / body.mass);

                // Euler integration
                body.linearVelocity = body.linearVelocity + accel * dt;
                body.linearVelocity = body.linearVelocity * (1.0f - body.linearDamping * dt);
                body.position = body.position + body.linearVelocity * dt;

                // Angular
                math::Vec3 angAccel = body.pendingTorque * (1.0f / body.mass);
                body.angularVelocity = body.angularVelocity + angAccel * dt;
                body.angularVelocity = body.angularVelocity * (1.0f - body.angularDamping * dt);

                // Simple quaternion integration
                math::Vec4& q = body.rotation;
                f32 wx = body.angularVelocity.x * dt * 0.5f;
                f32 wy = body.angularVelocity.y * dt * 0.5f;
                f32 wz = body.angularVelocity.z * dt * 0.5f;
                math::Vec4 dq = {
                     q.w * wx + q.y * wz - q.z * wy,
                     q.w * wy + q.z * wx - q.x * wz,
                     q.w * wz + q.x * wy - q.y * wx,
                    -q.x * wx - q.y * wy - q.z * wz
                };
                q.x += dq.x; q.y += dq.y; q.z += dq.z; q.w += dq.w;

                // Normalize quaternion
                f32 len = std::sqrt(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
                if (len > 0.0001f) {
                    f32 inv = 1.0f / len;
                    q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
                }

                // Reset forces
                body.pendingForce = {0, 0, 0};
                body.pendingTorque = {0, 0, 0};

                // Simple ground plane collision at y=0
                if (body.position.y < 0) {
                    body.position.y = 0;
                    if (body.linearVelocity.y < 0) {
                        body.linearVelocity.y = -body.linearVelocity.y * body.restitution;
                    }
                }
            }
        }

        m_impl->accumulator -= m_impl->fixedTimeStep;
        steps++;
    }
}

BodyId PhysicsWorld::CreateBody(const BodyDesc& desc) {
    if (!m_impl) return INVALID_BODY;

    BodyId id;
    if (!m_impl->freeList.empty()) {
        id = m_impl->freeList.back();
        m_impl->freeList.pop_back();
    } else {
        id = static_cast<BodyId>(m_impl->bodies.size());
        m_impl->bodies.emplace_back();
    }

    auto& body = m_impl->bodies[id];
    body.active = true;
    body.type = desc.type;
    body.position = desc.position;
    body.rotation = desc.rotation;
    body.linearVelocity = desc.linearVelocity;
    body.angularVelocity = desc.angularVelocity;
    body.mass = math::Max(desc.mass, 0.001f);
    body.friction = desc.friction;
    body.restitution = desc.restitution;
    body.linearDamping = desc.linearDamping;
    body.angularDamping = desc.angularDamping;
    body.isSensor = desc.isSensor;
    body.userData = desc.userData;
    body.pendingForce = {0, 0, 0};
    body.pendingTorque = {0, 0, 0};

    m_impl->activeCount++;
    return id;
}

void PhysicsWorld::DestroyBody(BodyId id) {
    if (!m_impl || id >= m_impl->bodies.size() || !m_impl->bodies[id].active) return;
    m_impl->bodies[id].active = false;
    m_impl->freeList.push_back(id);
    m_impl->activeCount--;
}

bool PhysicsWorld::IsBodyValid(BodyId id) const {
    return m_impl && id < m_impl->bodies.size() && m_impl->bodies[id].active;
}

math::Vec3 PhysicsWorld::GetPosition(BodyId id) const {
    if (!IsBodyValid(id)) return {0, 0, 0};
    return m_impl->bodies[id].position;
}

math::Vec4 PhysicsWorld::GetRotation(BodyId id) const {
    if (!IsBodyValid(id)) return {0, 0, 0, 1};
    return m_impl->bodies[id].rotation;
}

math::Vec3 PhysicsWorld::GetLinearVelocity(BodyId id) const {
    if (!IsBodyValid(id)) return {0, 0, 0};
    return m_impl->bodies[id].linearVelocity;
}

math::Vec3 PhysicsWorld::GetAngularVelocity(BodyId id) const {
    if (!IsBodyValid(id)) return {0, 0, 0};
    return m_impl->bodies[id].angularVelocity;
}

void PhysicsWorld::SetPosition(BodyId id, const math::Vec3& pos) {
    if (!IsBodyValid(id)) return;
    m_impl->bodies[id].position = pos;
}

void PhysicsWorld::SetRotation(BodyId id, const math::Vec4& rot) {
    if (!IsBodyValid(id)) return;
    m_impl->bodies[id].rotation = rot;
}

void PhysicsWorld::SetLinearVelocity(BodyId id, const math::Vec3& vel) {
    if (!IsBodyValid(id)) return;
    m_impl->bodies[id].linearVelocity = vel;
}

void PhysicsWorld::SetAngularVelocity(BodyId id, const math::Vec3& vel) {
    if (!IsBodyValid(id)) return;
    m_impl->bodies[id].angularVelocity = vel;
}

math::pga::Motor PhysicsWorld::GetMotor(BodyId id) const {
    if (!IsBodyValid(id)) return math::pga::Motor::Identity();
    const auto& b = m_impl->bodies[id];
    // Convert quaternion + position to PGA Motor
    // Motor = Translator * Rotor
    math::pga::Motor rotor = math::pga::Motor::FromAxisAngle(
        math::pga::Line{}, 0); // TODO: proper quaternion → rotor conversion
    math::pga::Motor translator = math::pga::Motor::Translation(b.position.x, b.position.y, b.position.z);
    return math::pga::Motor::Multiply(translator, rotor);
}

void PhysicsWorld::SetMotor(BodyId id, const math::pga::Motor& motor) {
    if (!IsBodyValid(id)) return;
    // Extract position from motor
    math::pga::Point origin{0, 0, 0};
    math::pga::Point transformed = motor.Transform(origin);
    m_impl->bodies[id].position = {transformed.x, transformed.y, transformed.z};
    // TODO: extract rotation from motor → quaternion
}

void PhysicsWorld::AddForce(BodyId id, const math::Vec3& force) {
    if (!IsBodyValid(id)) return;
    m_impl->bodies[id].pendingForce = m_impl->bodies[id].pendingForce + force;
}

void PhysicsWorld::AddImpulse(BodyId id, const math::Vec3& impulse) {
    if (!IsBodyValid(id)) return;
    auto& b = m_impl->bodies[id];
    b.linearVelocity = b.linearVelocity + impulse * (1.0f / b.mass);
}

void PhysicsWorld::AddTorque(BodyId id, const math::Vec3& torque) {
    if (!IsBodyValid(id)) return;
    m_impl->bodies[id].pendingTorque = m_impl->bodies[id].pendingTorque + torque;
}

RayCastResult PhysicsWorld::RayCast(const math::Vec3& origin, const math::Vec3& direction,
                                      f32 maxDistance, u16 /*layerMask*/) const {
    RayCastResult result;
    if (!m_impl) return result;

    // Simple ground plane ray cast (y = 0)
    if (direction.y != 0) {
        f32 t = -origin.y / direction.y;
        if (t > 0 && t < maxDistance) {
            result.hit = true;
            result.distance = t;
            result.hitPoint = origin + direction * t;
            result.hitNormal = {0, 1, 0};
        }
    }

    // TODO: Actual shape intersection when Jolt is integrated
    return result;
}

std::vector<RayCastResult> PhysicsWorld::RayCastAll(const math::Vec3& origin, const math::Vec3& direction,
                                                       f32 maxDistance, u16 layerMask) const {
    std::vector<RayCastResult> results;
    auto single = RayCast(origin, direction, maxDistance, layerMask);
    if (single.hit) results.push_back(single);
    return results;
}

std::vector<BodyId> PhysicsWorld::OverlapSphere(const math::Vec3& /*center*/, f32 /*radius*/,
                                                   u16 /*layerMask*/) const {
    // TODO: Implement with Jolt
    return {};
}

std::vector<BodyId> PhysicsWorld::OverlapBox(const math::Vec3& /*center*/, const math::Vec3& /*halfExtents*/,
                                                const math::Vec4& /*rotation*/, u16 /*layerMask*/) const {
    // TODO: Implement with Jolt
    return {};
}

void PhysicsWorld::SetGravity(const math::Vec3& gravity) {
    if (m_impl) m_impl->gravity = gravity;
}

math::Vec3 PhysicsWorld::GetGravity() const {
    return m_impl ? m_impl->gravity : math::Vec3{0, -9.81f, 0};
}

u32 PhysicsWorld::GetActiveBodyCount() const {
    return m_impl ? m_impl->activeCount : 0;
}

u32 PhysicsWorld::GetTotalBodyCount() const {
    return m_impl ? static_cast<u32>(m_impl->bodies.size()) : 0;
}

} // namespace nge::physics
