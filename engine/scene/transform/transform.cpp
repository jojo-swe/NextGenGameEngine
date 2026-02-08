#include "engine/scene/transform/transform.h"
#include "engine/core/ecs/world.h"

namespace nge::scene {

void TransformSystem::UpdateHierarchy(ecs::World& world) {
    // First pass: update all root entities (no parent)
    world.Each<Transform>([&](ecs::Entity entity, Transform& transform) {
        if (!transform.parent.IsValid()) {
            // Root entity — world = local
            if (transform.dirty) {
                transform.worldMotor = transform.localMotor;
                transform.dirty = false;
                // Propagate to children
                UpdateEntity(world, entity, transform.worldMotor);
            }
        }
    });

    // Second pass: update remaining dirty entities with parents
    // (This handles entities whose parents were already updated above)
    world.Each<Transform>([&](ecs::Entity /*entity*/, Transform& transform) {
        if (transform.dirty && transform.parent.IsValid()) {
            Transform* parentTransform = world.GetComponent<Transform>(transform.parent);
            if (parentTransform) {
                transform.worldMotor = pga::Motor::Multiply(
                    parentTransform->worldMotor, transform.localMotor);
            } else {
                transform.worldMotor = transform.localMotor;
            }
            transform.dirty = false;
        }
    });
}

void TransformSystem::UpdateEntity(ecs::World& world, ecs::Entity /*entity*/, const pga::Motor& parentWorld) {
    // Find children of this entity and update them
    // For now, the second pass in UpdateHierarchy handles this.
    // A proper implementation would maintain a children list per entity.
    (void)world;
    (void)parentWorld;
}

} // namespace nge::scene
