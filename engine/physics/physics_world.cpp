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

struct AABB {
    math::Vec3 min;
    math::Vec3 max;
};

struct PhysicsWorld::Impl {
    struct Body {
        bool       active = false;
        BodyType   type   = BodyType::Static;
        ShapeType  shapeType = ShapeType::Box;
        math::Vec3 halfExtents = {0.5f, 0.5f, 0.5f};
        f32        radius = 0.5f;
        math::Vec3 position;
        math::Vec4 rotation = {0, 0, 0, 1};
        math::Vec3 linearVelocity;
        math::Vec3 angularVelocity;
        f32        mass = 1.0f;
        f32        invMass = 1.0f;
        f32        linearDamping = 0.05f;
        f32        angularDamping = 0.05f;
        f32        friction = 0.5f;
        f32        restitution = 0.3f;
        bool       isSensor = false;
        u64        userData = 0;
        u16        collisionLayer = 0;
        u16        collisionMask = 0xFFFF;
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

    AABB ComputeAABB(u32 bodyIndex) const {
        const auto& b = bodies[bodyIndex];
        AABB box;
        if (b.shapeType == ShapeType::Sphere) {
            box.min = {b.position.x - b.radius, b.position.y - b.radius, b.position.z - b.radius};
            box.max = {b.position.x + b.radius, b.position.y + b.radius, b.position.z + b.radius};
        } else {
            box.min = {b.position.x - b.halfExtents.x, b.position.y - b.halfExtents.y, b.position.z - b.halfExtents.z};
            box.max = {b.position.x + b.halfExtents.x, b.position.y + b.halfExtents.y, b.position.z + b.halfExtents.z};
        }
        return box;
    }

    bool AABBOverlap(const AABB& a, const AABB& b) const {
        return !(a.max.x < b.min.x || b.max.x < a.min.x ||
                 a.max.y < b.min.y || b.max.y < a.min.y ||
                 a.max.z < b.min.z || b.max.z < a.min.z);
    }

    bool LayersMatch(u16 layerA, u16 maskA, u16 layerB, u16 maskB) const {
        return (layerA & maskB) || (layerB & maskA);
    }
};

PhysicsWorld::PhysicsWorld() = default;
PhysicsWorld::~PhysicsWorld() {
    Shutdown();
}
PhysicsWorld::PhysicsWorld(PhysicsWorld&&) noexcept = default;
PhysicsWorld& PhysicsWorld::operator=(PhysicsWorld&&) noexcept = default;

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

        // ─── Collision detection and response ──────────────────────────
        for (u32 i = 0; i < static_cast<u32>(m_impl->bodies.size()); ++i) {
            auto& a = m_impl->bodies[i];
            if (!a.active || a.type == BodyType::Static) continue;

            for (u32 j = 0; j < static_cast<u32>(m_impl->bodies.size()); ++j) {
                if (i == j) continue;
                auto& b = m_impl->bodies[j];
                if (!b.active) continue;
                if (!m_impl->LayersMatch(a.collisionLayer, a.collisionMask, b.collisionLayer, b.collisionMask)) continue;

                AABB aabbA = m_impl->ComputeAABB(i);
                AABB aabbB = m_impl->ComputeAABB(j);

                if (!m_impl->AABBOverlap(aabbA, aabbB)) continue;

                // Compute collision normal and penetration
                math::Vec3 normal = {0, 1, 0};
                f32 penetration = 0;

                if (a.shapeType == ShapeType::Sphere && b.shapeType == ShapeType::Sphere) {
                    math::Vec3 delta = b.position - a.position;
                    f32 dist = delta.Length();
                    f32 sumR = a.radius + b.radius;
                    if (dist < sumR && dist > 0.0001f) {
                        normal = delta * (1.0f / dist);
                        penetration = sumR - dist;
                    } else if (dist <= 0.0001f) {
                        normal = {0, 1, 0};
                        penetration = sumR;
                    } else continue;
                } else if (a.shapeType == ShapeType::Sphere && b.shapeType == ShapeType::Box) {
                    // Closest point on box to sphere center
                    math::Vec3 closest = {
                        math::Clamp(a.position.x, b.position.x - b.halfExtents.x, b.position.x + b.halfExtents.x),
                        math::Clamp(a.position.y, b.position.y - b.halfExtents.y, b.position.y + b.halfExtents.y),
                        math::Clamp(a.position.z, b.position.z - b.halfExtents.z, b.position.z + b.halfExtents.z)
                    };
                    math::Vec3 delta = a.position - closest;
                    f32 dist = delta.Length();
                    if (dist < a.radius && dist > 0.0001f) {
                        normal = delta * (1.0f / dist);
                        penetration = a.radius - dist;
                    } else if (dist <= 0.0001f) {
                        // Sphere center inside box — push out along smallest axis
                        math::Vec3 toCenter = a.position - b.position;
                        f32 ax = std::abs(toCenter.x) / b.halfExtents.x;
                        f32 ay = std::abs(toCenter.y) / b.halfExtents.y;
                        f32 az = std::abs(toCenter.z) / b.halfExtents.z;
                        if (ax >= ay && ax >= az) {
                            normal = {toCenter.x > 0 ? 1.0f : -1.0f, 0, 0};
                            penetration = b.halfExtents.x + a.radius - std::abs(toCenter.x);
                        } else if (ay >= az) {
                            normal = {0, toCenter.y > 0 ? 1.0f : -1.0f, 0};
                            penetration = b.halfExtents.y + a.radius - std::abs(toCenter.y);
                        } else {
                            normal = {0, 0, toCenter.z > 0 ? 1.0f : -1.0f};
                            penetration = b.halfExtents.z + a.radius - std::abs(toCenter.z);
                        }
                    } else continue;
                } else if (a.shapeType == ShapeType::Box && b.shapeType == ShapeType::Box) {
                    // Box-box: AABB overlap with minimum penetration axis
                    f32 overlapX = std::min(aabbA.max.x, aabbB.max.x) - std::max(aabbA.min.x, aabbB.min.x);
                    f32 overlapY = std::min(aabbA.max.y, aabbB.max.y) - std::max(aabbA.min.y, aabbB.min.y);
                    f32 overlapZ = std::min(aabbA.max.z, aabbB.max.z) - std::max(aabbA.min.z, aabbB.min.z);
                    if (overlapX <= 0 || overlapY <= 0 || overlapZ <= 0) continue;
                    if (overlapX <= overlapY && overlapX <= overlapZ) {
                        normal = a.position.x < b.position.x ? math::Vec3{-1, 0, 0} : math::Vec3{1, 0, 0};
                        penetration = overlapX;
                    } else if (overlapY <= overlapZ) {
                        normal = a.position.y < b.position.y ? math::Vec3{0, -1, 0} : math::Vec3{0, 1, 0};
                        penetration = overlapY;
                    } else {
                        normal = a.position.z < b.position.z ? math::Vec3{0, 0, -1} : math::Vec3{0, 0, 1};
                        penetration = overlapZ;
                    }
                } else {
                    // Box-sphere: delegate to sphere-box with swapped roles
                    // (a=box, b=sphere → treat as b.sphere vs a.box)
                    math::Vec3 closest = {
                        math::Clamp(b.position.x, a.position.x - a.halfExtents.x, a.position.x + a.halfExtents.x),
                        math::Clamp(b.position.y, a.position.y - a.halfExtents.y, a.position.y + a.halfExtents.y),
                        math::Clamp(b.position.z, a.position.z - a.halfExtents.z, a.position.z + a.halfExtents.z)
                    };
                    math::Vec3 delta = b.position - closest;
                    f32 dist = delta.Length();
                    if (dist < b.radius && dist > 0.0001f) {
                        normal = delta * (-1.0f / dist); // normal points from b to a
                        penetration = b.radius - dist;
                    } else if (dist <= 0.0001f) {
                        math::Vec3 toCenter = b.position - a.position;
                        f32 ay = std::abs(toCenter.y) / a.halfExtents.y;
                        normal = {0, toCenter.y > 0 ? -1.0f : 1.0f, 0};
                        penetration = a.halfExtents.y + b.radius - std::abs(toCenter.y);
                        (void)ay;
                    } else continue;
                }

                // Fire contact callback
                if (m_contactCallback) {
                    ContactPoint cp;
                    cp.position = a.position + normal * (penetration * 0.5f);
                    cp.normal = normal;
                    cp.penetration = penetration;
                    cp.bodyA = i;
                    cp.bodyB = j;
                    m_contactCallback(cp);
                }

                // Skip resolution for sensors
                if (a.isSensor || b.isSensor) continue;
                if (b.type == BodyType::Static && a.type == BodyType::Static) continue;

                // Positional correction (Baumgarte)
                f32 totalInvMass = a.invMass + b.invMass;
                if (totalInvMass <= 0) continue;

                f32 correction = penetration / totalInvMass * 0.8f;
                a.position = a.position - normal * (correction * a.invMass);
                b.position = b.position + normal * (correction * b.invMass);

                // Impulse-based velocity response
                math::Vec3 relVel = b.linearVelocity - a.linearVelocity;
                f32 velAlongNormal = relVel.Dot(normal);
                if (velAlongNormal > 0) continue; // Separating

                f32 e = math::Min(a.restitution, b.restitution);
                f32 j_imp = -(1.0f + e) * velAlongNormal / totalInvMass;

                math::Vec3 impulse = normal * j_imp;
                a.linearVelocity = a.linearVelocity - impulse * a.invMass;
                b.linearVelocity = b.linearVelocity + impulse * b.invMass;

                // Simple friction
                math::Vec3 relVelT = relVel - normal * relVel.Dot(normal);
                f32 tangentSpeed = relVelT.Length();
                if (tangentSpeed > 0.0001f) {
                    math::Vec3 tangent = relVelT * (1.0f / tangentSpeed);
                    f32 frictionCoef = math::Sqrt(a.friction * b.friction);
                    f32 jt = -tangentSpeed / totalInvMass * frictionCoef;
                    math::Vec3 frictionImpulse = tangent * jt;
                    a.linearVelocity = a.linearVelocity - frictionImpulse * a.invMass;
                    b.linearVelocity = b.linearVelocity + frictionImpulse * b.invMass;
                }
            } // for j
        } // for i

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
    body.shapeType = desc.shape.type;
    body.halfExtents = desc.shape.halfExtents;
    body.radius = desc.shape.radius;
    body.position = desc.position;
    body.rotation = desc.rotation;
    body.linearVelocity = desc.linearVelocity;
    body.angularVelocity = desc.angularVelocity;
    body.mass = math::Max(desc.mass, 0.001f);
    body.invMass = (desc.type == BodyType::Static) ? 0.0f : 1.0f / body.mass;
    body.friction = desc.friction;
    body.restitution = desc.restitution;
    body.linearDamping = desc.linearDamping;
    body.angularDamping = desc.angularDamping;
    body.isSensor = desc.isSensor;
    body.userData = desc.userData;
    body.collisionLayer = desc.collisionLayer;
    body.collisionMask = desc.collisionMask;
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

pga::Motor PhysicsWorld::GetMotor(BodyId id) const {
    if (!IsBodyValid(id)) return pga::Motor::Identity();
    const auto& b = m_impl->bodies[id];
    // Convert quaternion + position to PGA Motor
    // Motor = Translator * Rotor
    pga::Motor rotor = pga::Motor::FromAxisAngle(
        pga::Line{}, 0); // TODO: proper quaternion → rotor conversion
    pga::Motor translator = pga::Motor::Translation(b.position.x, b.position.y, b.position.z);
    return pga::Motor::Multiply(translator, rotor);
}

void PhysicsWorld::SetMotor(BodyId id, const pga::Motor& motor) {
    if (!IsBodyValid(id)) return;
    // Extract position from motor
    pga::Point origin{0, 0, 0};
    pga::Point transformed = motor.Transform(origin);
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
                                      f32 maxDistance, u16 layerMask) const {
    RayCastResult result;
    if (!m_impl) return result;

    // Ground plane ray cast (y = 0)
    if (direction.y != 0) {
        f32 t = -origin.y / direction.y;
        if (t > 0 && t < maxDistance) {
            result.hit = true;
            result.distance = t;
            result.hitPoint = origin + direction * t;
            result.hitNormal = {0, 1, 0};
        }
    }

    // Raycast against all active bodies
    for (u32 i = 0; i < static_cast<u32>(m_impl->bodies.size()); ++i) {
        const auto& body = m_impl->bodies[i];
        if (!body.active) continue;
        if (!(body.collisionLayer & layerMask)) continue;

        f32 hitT = -1;

        if (body.shapeType == ShapeType::Sphere) {
            math::Vec3 oc = origin - body.position;
            f32 a = direction.Dot(direction);
            f32 b = oc.Dot(direction);
            f32 c = oc.Dot(oc) - body.radius * body.radius;
            f32 disc = b * b - a * c;
            if (disc < 0) continue;
            f32 sqD = std::sqrt(disc);
            f32 t1 = (-b - sqD) / a;
            f32 t2 = (-b + sqD) / a;
            if (t1 > 0 && t1 < maxDistance) hitT = t1;
            else if (t2 > 0 && t2 < maxDistance) hitT = t2;
        } else {
            AABB box = m_impl->ComputeAABB(i);
            f32 tmin = 0;
            f32 tmax = maxDistance;
            bool valid = true;

            for (int axis = 0; axis < 3; ++axis) {
                f32 o = (axis == 0) ? origin.x : (axis == 1) ? origin.y : origin.z;
                f32 d = (axis == 0) ? direction.x : (axis == 1) ? direction.y : direction.z;
                f32 bmin = (axis == 0) ? box.min.x : (axis == 1) ? box.min.y : box.min.z;
                f32 bmax = (axis == 0) ? box.max.x : (axis == 1) ? box.max.y : box.max.z;

                if (std::abs(d) < 1e-8f) {
                    if (o < bmin || o > bmax) { valid = false; break; }
                } else {
                    f32 invD = 1.0f / d;
                    f32 t1 = (bmin - o) * invD;
                    f32 t2 = (bmax - o) * invD;
                    if (t1 > t2) std::swap(t1, t2);
                    tmin = math::Max(tmin, t1);
                    tmax = math::Min(tmax, t2);
                    if (tmin > tmax) { valid = false; break; }
                }
            }

            if (valid && tmin > 0 && tmin < maxDistance) hitT = tmin;
            else if (valid && tmin <= 0 && tmax > 0 && tmax < maxDistance) hitT = tmax;
        }

        if (hitT > 0 && (!result.hit || hitT < result.distance)) {
            result.hit = true;
            result.distance = hitT;
            result.hitPoint = origin + direction * hitT;
            result.bodyId = i;
            result.userData = body.userData;

            if (body.shapeType == ShapeType::Sphere) {
                result.hitNormal = (result.hitPoint - body.position).Normalized();
            } else {
                AABB box = m_impl->ComputeAABB(i);
                f32 cx = (box.min.x + box.max.x) * 0.5f;
                f32 cy = (box.min.y + box.max.y) * 0.5f;
                f32 cz = (box.min.z + box.max.z) * 0.5f;
                f32 dx = std::abs(result.hitPoint.x - cx) - (box.max.x - box.min.x) * 0.5f;
                f32 dy = std::abs(result.hitPoint.y - cy) - (box.max.y - box.min.y) * 0.5f;
                f32 dz = std::abs(result.hitPoint.z - cz) - (box.max.z - box.min.z) * 0.5f;
                if (dx >= dy && dx >= dz) {
                    result.hitNormal = {result.hitPoint.x > cx ? 1.0f : -1.0f, 0, 0};
                } else if (dy >= dz) {
                    result.hitNormal = {0, result.hitPoint.y > cy ? 1.0f : -1.0f, 0};
                } else {
                    result.hitNormal = {0, 0, result.hitPoint.z > cz ? 1.0f : -1.0f};
                }
            }
        }
    }

    return result;
}

std::vector<RayCastResult> PhysicsWorld::RayCastAll(const math::Vec3& origin, const math::Vec3& direction,
                                                       f32 maxDistance, u16 layerMask) const {
    std::vector<RayCastResult> results;
    auto single = RayCast(origin, direction, maxDistance, layerMask);
    if (single.hit) results.push_back(single);
    return results;
}

std::vector<BodyId> PhysicsWorld::OverlapSphere(const math::Vec3& center, f32 radius,
                                                   u16 layerMask) const {
    std::vector<BodyId> result;
    if (!m_impl) return result;

    for (u32 i = 0; i < static_cast<u32>(m_impl->bodies.size()); ++i) {
        const auto& body = m_impl->bodies[i];
        if (!body.active) continue;
        if (!(body.collisionLayer & layerMask)) continue;

        if (body.shapeType == ShapeType::Sphere) {
            f32 dist = (body.position - center).Length();
            if (dist <= radius + body.radius) {
                result.push_back(i);
            }
        } else {
            // Sphere-AABB overlap: closest point on AABB to sphere center
            math::Vec3 closest = {
                math::Clamp(center.x, body.position.x - body.halfExtents.x, body.position.x + body.halfExtents.x),
                math::Clamp(center.y, body.position.y - body.halfExtents.y, body.position.y + body.halfExtents.y),
                math::Clamp(center.z, body.position.z - body.halfExtents.z, body.position.z + body.halfExtents.z)
            };
            f32 distSq = (closest - center).LengthSq();
            if (distSq <= radius * radius) {
                result.push_back(i);
            }
        }
    }

    return result;
}

std::vector<BodyId> PhysicsWorld::OverlapBox(const math::Vec3& center, const math::Vec3& halfExtents,
                                                const math::Vec4& /*rotation*/, u16 layerMask) const {
    std::vector<BodyId> result;
    if (!m_impl) return result;

    AABB queryBox;
    queryBox.min = {center.x - halfExtents.x, center.y - halfExtents.y, center.z - halfExtents.z};
    queryBox.max = {center.x + halfExtents.x, center.y + halfExtents.y, center.z + halfExtents.z};

    for (u32 i = 0; i < static_cast<u32>(m_impl->bodies.size()); ++i) {
        const auto& body = m_impl->bodies[i];
        if (!body.active) continue;
        if (!(body.collisionLayer & layerMask)) continue;

        AABB bodyBox = m_impl->ComputeAABB(i);
        if (m_impl->AABBOverlap(queryBox, bodyBox)) {
            result.push_back(i);
        }
    }

    return result;
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
