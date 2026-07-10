#pragma once

#include "engine/core/types.h"
#include "engine/core/math/pga.h"
#include "engine/core/math/math_types.h"
#include "engine/core/ecs/entity.h"
#include "engine/core/containers/array.h"

namespace nge::ecs { class World; }

namespace nge::scene {

// ─── Transform Component ─────────────────────────────────────────────────
// Uses PGA Motors as the primary representation.
// Motors unify rotation + translation in 8 floats (vs 16 for mat4).
// Hierarchy is maintained via parent entity references.
struct Transform {
    pga::Motor localMotor;              // Local transform relative to parent
    pga::Motor worldMotor;              // Cached world transform (parent × local)
    ecs::Entity parent = ecs::Entity::Invalid();
    bool dirty = true;                  // Needs world recalculation

    // ─── Convenience setters ──────────────────────────────────────────

    void SetPosition(math::Vec3 pos) {
        // TODO(WP-2): extract the rotation from the current motor and
        // recombine: Motor::Multiply(Translation(pos), rotorPart).
        // For now this rebuilds the motor from scratch and DROPS rotation.
        localMotor = pga::Motor::Translation(pos);
        dirty = true;
    }

    void SetRotation(math::Vec3 axis, f32 angle) {
        pga::Motor trans = pga::Motor::Translation(GetLocalPosition());
        pga::Motor rot = pga::Motor::Rotation(axis, angle);
        localMotor = pga::Motor::Multiply(trans, rot);
        dirty = true;
    }

    void SetPositionRotation(math::Vec3 pos, math::Vec3 axis, f32 angle) {
        pga::Motor trans = pga::Motor::Translation(pos);
        pga::Motor rot = pga::Motor::Rotation(axis, angle);
        localMotor = pga::Motor::Multiply(trans, rot);
        dirty = true;
    }

    void Translate(math::Vec3 delta) {
        pga::Motor t = pga::Motor::Translation(delta);
        localMotor = pga::Motor::Multiply(t, localMotor);
        dirty = true;
    }

    void Rotate(math::Vec3 axis, f32 angle) {
        pga::Motor r = pga::Motor::Rotation(axis, angle);
        localMotor = pga::Motor::Multiply(localMotor, r);
        dirty = true;
    }

    // ─── Getters ──────────────────────────────────────────────────────

    math::Vec3 GetLocalPosition() const {
        pga::Point origin = pga::Point::Origin();
        pga::Point transformed = localMotor.Apply(origin);
        return transformed.ToVec3();
    }

    math::Vec3 GetWorldPosition() const {
        pga::Point origin = pga::Point::Origin();
        pga::Point transformed = worldMotor.Apply(origin);
        return transformed.ToVec3();
    }

    math::Vec3 GetForward() const {
        pga::Point fwd(0, 0, -1); // -Z is forward (Vulkan convention)
        pga::Point transformed = worldMotor.Apply(fwd);
        math::Vec3 pos = GetWorldPosition();
        math::Vec3 fwdWorld = transformed.ToVec3();
        return (fwdWorld - pos).Normalized();
    }

    math::Vec3 GetRight() const {
        pga::Point right(1, 0, 0);
        pga::Point transformed = worldMotor.Apply(right);
        math::Vec3 pos = GetWorldPosition();
        return (transformed.ToVec3() - pos).Normalized();
    }

    math::Vec3 GetUp() const {
        pga::Point up(0, 1, 0);
        pga::Point transformed = worldMotor.Apply(up);
        math::Vec3 pos = GetWorldPosition();
        return (transformed.ToVec3() - pos).Normalized();
    }

    math::Mat4 GetWorldMatrix() const {
        return worldMotor.ToMat4();
    }
};

// ─── Transform System ────────────────────────────────────────────────────
// Updates world transforms by traversing the hierarchy.
// Should be called once per frame before rendering.

class TransformSystem {
public:
    // Update all dirty transforms in the world.
    // Processes parent-first ordering to propagate changes down.
    static void UpdateHierarchy(ecs::World& world);

private:
    // Recursive update for a single entity and its children
    static void UpdateEntity(ecs::World& world, ecs::Entity entity, const pga::Motor& parentWorld);
};

} // namespace nge::scene
