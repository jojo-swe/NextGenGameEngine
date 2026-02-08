#include <gtest/gtest.h>
#include "engine/core/ecs/world.h"

using namespace nge;
using namespace nge::ecs;

struct Position { f32 x = 0, y = 0, z = 0; };
struct Velocity { f32 vx = 0, vy = 0, vz = 0; };
struct Health { i32 hp = 100; };
struct Tag {};

// ─── Entity Creation ─────────────────────────────────────────────────────

TEST(ECS, CreateEntity) {
    World world;
    Entity e = world.CreateEntity();
    EXPECT_TRUE(e.IsValid());
    EXPECT_TRUE(world.IsAlive(e));
}

TEST(ECS, DestroyEntity) {
    World world;
    Entity e = world.CreateEntity();
    world.DestroyEntity(e);
    EXPECT_FALSE(world.IsAlive(e));
}

TEST(ECS, GenerationIncrements) {
    World world;
    Entity e1 = world.CreateEntity();
    u32 idx = e1.Index();
    world.DestroyEntity(e1);

    Entity e2 = world.CreateEntity();
    // Should reuse same index but different generation
    EXPECT_EQ(e2.Index(), idx);
    EXPECT_NE(e1.Generation(), e2.Generation());
    EXPECT_FALSE(world.IsAlive(e1));
    EXPECT_TRUE(world.IsAlive(e2));
}

// ─── Component Operations ────────────────────────────────────────────────

TEST(ECS, AddAndGetComponent) {
    World world;
    world.RegisterComponent<Position>("Position");

    Entity e = world.CreateEntity();
    world.AddComponent<Position>(e, {1.0f, 2.0f, 3.0f});

    Position* pos = world.GetComponent<Position>(e);
    ASSERT_NE(pos, nullptr);
    EXPECT_FLOAT_EQ(pos->x, 1.0f);
    EXPECT_FLOAT_EQ(pos->y, 2.0f);
    EXPECT_FLOAT_EQ(pos->z, 3.0f);
}

TEST(ECS, MultipleComponents) {
    World world;
    world.RegisterComponent<Position>("Position");
    world.RegisterComponent<Velocity>("Velocity");
    world.RegisterComponent<Health>("Health");

    Entity e = world.CreateEntity();
    world.AddComponent<Position>(e, {1, 2, 3});
    world.AddComponent<Velocity>(e, {4, 5, 6});
    world.AddComponent<Health>(e, {200});

    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_TRUE(world.HasComponent<Velocity>(e));
    EXPECT_TRUE(world.HasComponent<Health>(e));

    EXPECT_FLOAT_EQ(world.GetComponent<Position>(e)->x, 1.0f);
    EXPECT_FLOAT_EQ(world.GetComponent<Velocity>(e)->vx, 4.0f);
    EXPECT_EQ(world.GetComponent<Health>(e)->hp, 200);
}

TEST(ECS, RemoveComponent) {
    World world;
    world.RegisterComponent<Position>("Position");
    world.RegisterComponent<Velocity>("Velocity");

    Entity e = world.CreateEntity();
    world.AddComponent<Position>(e, {1, 2, 3});
    world.AddComponent<Velocity>(e, {4, 5, 6});

    world.RemoveComponent<Velocity>(e);
    EXPECT_TRUE(world.HasComponent<Position>(e));
    EXPECT_FALSE(world.HasComponent<Velocity>(e));

    // Position should still be valid
    EXPECT_FLOAT_EQ(world.GetComponent<Position>(e)->x, 1.0f);
}

// ─── Query ───────────────────────────────────────────────────────────────

TEST(ECS, QueryIteration) {
    World world;
    world.RegisterComponent<Position>("Position");
    world.RegisterComponent<Velocity>("Velocity");
    world.RegisterComponent<Health>("Health");

    // Create entities with different component sets
    Entity e1 = world.CreateEntity();
    world.AddComponent<Position>(e1, {1, 0, 0});
    world.AddComponent<Velocity>(e1, {10, 0, 0});

    Entity e2 = world.CreateEntity();
    world.AddComponent<Position>(e2, {2, 0, 0});
    world.AddComponent<Velocity>(e2, {20, 0, 0});

    Entity e3 = world.CreateEntity();
    world.AddComponent<Position>(e3, {3, 0, 0}); // No velocity
    world.AddComponent<Health>(e3, {50});

    // Query for entities with Position AND Velocity
    int count = 0;
    f32 sumX = 0;
    world.Each<Position, Velocity>([&](Entity /*e*/, Position& p, Velocity& v) {
        sumX += p.x + v.vx;
        ++count;
    });

    EXPECT_EQ(count, 2);
    EXPECT_FLOAT_EQ(sumX, 1 + 10 + 2 + 20); // (1+10) + (2+20) = 33
}

// ─── Stress: Many Entities ───────────────────────────────────────────────

TEST(ECS, ManyEntities) {
    World world;
    world.RegisterComponent<Position>("Position");
    world.RegisterComponent<Velocity>("Velocity");

    constexpr int N = 10000;
    std::vector<Entity> entities;
    entities.reserve(N);

    for (int i = 0; i < N; ++i) {
        Entity e = world.CreateEntity();
        world.AddComponent<Position>(e, {static_cast<f32>(i), 0, 0});
        world.AddComponent<Velocity>(e, {1, 0, 0});
        entities.push_back(e);
    }

    EXPECT_EQ(world.GetEntityCount(), static_cast<usize>(N));

    // Simulate one step: pos += vel
    world.Each<Position, Velocity>([](Entity /*e*/, Position& p, Velocity& v) {
        p.x += v.vx;
        p.y += v.vy;
        p.z += v.vz;
    });

    // Check first entity
    Position* pos = world.GetComponent<Position>(entities[0]);
    EXPECT_FLOAT_EQ(pos->x, 1.0f); // 0 + 1

    // Destroy half
    for (int i = 0; i < N / 2; ++i) {
        world.DestroyEntity(entities[i]);
    }

    EXPECT_EQ(world.GetEntityCount(), static_cast<usize>(N / 2));
}
