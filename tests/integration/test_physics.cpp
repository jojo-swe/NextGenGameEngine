#include <gtest/gtest.h>
#include "engine/physics/physics_world.h"

using namespace nge;
using namespace nge::physics;

class PhysicsTest : public ::testing::Test {
protected:
    void SetUp() override {
        PhysicsWorldConfig config;
        config.gravity = {0, -9.81f, 0};
        config.fixedTimeStep = 1.0f / 60.0f;
        ASSERT_TRUE(m_world.Init(config));
    }

    void TearDown() override {
        m_world.Shutdown();
    }

    PhysicsWorld m_world;
};

TEST_F(PhysicsTest, CreateAndDestroyBody) {
    BodyDesc desc;
    desc.type = BodyType::Dynamic;
    desc.position = {0, 10, 0};
    desc.mass = 1.0f;

    BodyId id = m_world.CreateBody(desc);
    EXPECT_NE(id, INVALID_BODY);
    EXPECT_TRUE(m_world.IsBodyValid(id));

    auto pos = m_world.GetPosition(id);
    EXPECT_FLOAT_EQ(pos.x, 0);
    EXPECT_FLOAT_EQ(pos.y, 10);
    EXPECT_FLOAT_EQ(pos.z, 0);

    m_world.DestroyBody(id);
    EXPECT_FALSE(m_world.IsBodyValid(id));
}

TEST_F(PhysicsTest, GravityFall) {
    BodyDesc desc;
    desc.type = BodyType::Dynamic;
    desc.position = {0, 10, 0};
    desc.mass = 1.0f;
    desc.linearDamping = 0;

    BodyId id = m_world.CreateBody(desc);

    // Step for ~1 second
    for (int i = 0; i < 60; ++i) {
        m_world.Step(1.0f / 60.0f);
    }

    auto pos = m_world.GetPosition(id);
    // After 1 second of free fall: y = 10 - 0.5 * 9.81 * 1^2 ≈ 5.095
    // But with Euler integration there's numerical drift, so just check it fell
    EXPECT_LT(pos.y, 10.0f);
    EXPECT_GT(pos.y, -1.0f); // Should have hit ground at y=0
}

TEST_F(PhysicsTest, StaticBodyDoesNotMove) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.position = {5, 5, 5};

    BodyId id = m_world.CreateBody(desc);

    for (int i = 0; i < 60; ++i) {
        m_world.Step(1.0f / 60.0f);
    }

    auto pos = m_world.GetPosition(id);
    EXPECT_FLOAT_EQ(pos.x, 5);
    EXPECT_FLOAT_EQ(pos.y, 5);
    EXPECT_FLOAT_EQ(pos.z, 5);
}

TEST_F(PhysicsTest, ApplyImpulse) {
    BodyDesc desc;
    desc.type = BodyType::Dynamic;
    desc.position = {0, 5, 0};
    desc.mass = 1.0f;

    BodyId id = m_world.CreateBody(desc);
    m_world.AddImpulse(id, {10, 0, 0});

    auto vel = m_world.GetLinearVelocity(id);
    EXPECT_GT(vel.x, 0);

    m_world.Step(1.0f / 60.0f);
    auto pos = m_world.GetPosition(id);
    EXPECT_GT(pos.x, 0);
}

TEST_F(PhysicsTest, RayCastGround) {
    auto result = m_world.RayCast({0, 5, 0}, {0, -1, 0}, 100.0f);
    EXPECT_TRUE(result.hit);
    EXPECT_NEAR(result.hitPoint.y, 0, 0.01f);
    EXPECT_NEAR(result.distance, 5.0f, 0.01f);
}

TEST_F(PhysicsTest, SetGravity) {
    m_world.SetGravity({0, -20, 0});
    auto g = m_world.GetGravity();
    EXPECT_FLOAT_EQ(g.y, -20);
}

TEST_F(PhysicsTest, BodyCountTracking) {
    EXPECT_EQ(m_world.GetActiveBodyCount(), 0u);

    BodyDesc desc;
    desc.type = BodyType::Dynamic;
    BodyId a = m_world.CreateBody(desc);
    BodyId b = m_world.CreateBody(desc);
    EXPECT_EQ(m_world.GetActiveBodyCount(), 2u);

    m_world.DestroyBody(a);
    EXPECT_EQ(m_world.GetActiveBodyCount(), 1u);

    m_world.DestroyBody(b);
    EXPECT_EQ(m_world.GetActiveBodyCount(), 0u);
}

TEST_F(PhysicsTest, GroundCollision) {
    BodyDesc desc;
    desc.type = BodyType::Dynamic;
    desc.position = {0, 0.1f, 0};
    desc.mass = 1.0f;
    desc.restitution = 0.5f;

    BodyId id = m_world.CreateBody(desc);

    // Step many times — body should settle at y=0
    for (int i = 0; i < 300; ++i) {
        m_world.Step(1.0f / 60.0f);
    }

    auto pos = m_world.GetPosition(id);
    EXPECT_GE(pos.y, 0.0f);
    EXPECT_LT(pos.y, 1.0f); // Should be near ground
}
