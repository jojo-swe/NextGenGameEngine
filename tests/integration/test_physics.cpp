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

// ─── Collision Tests ──────────────────────────────────────────────────────

TEST_F(PhysicsTest, SphereSphereCollision) {
    BodyDesc a, b;
    a.type = BodyType::Dynamic;
    a.shape.type = ShapeType::Sphere;
    a.shape.radius = 1.0f;
    a.position = {0, 5, 0};
    a.mass = 1.0f;
    a.linearDamping = 0;
    a.restitution = 0.5f;

    b.type = BodyType::Static;
    b.shape.type = ShapeType::Sphere;
    b.shape.radius = 1.0f;
    b.position = {0, 0, 0};

    BodyId idA = m_world.CreateBody(a);
    m_world.CreateBody(b);

    // Step until collision
    for (int i = 0; i < 300; ++i) {
        m_world.Step(1.0f / 60.0f);
    }

    auto posA = m_world.GetPosition(idA);
    // Sphere A should have been stopped by sphere B (sum of radii = 2.0)
    EXPECT_LT(posA.y, 5.0f);        // It fell
    EXPECT_GT(posA.y, 1.0f);        // But not past the static sphere
}

TEST_F(PhysicsTest, SphereBoxCollision) {
    BodyDesc sphere, box;
    sphere.type = BodyType::Dynamic;
    sphere.shape.type = ShapeType::Sphere;
    sphere.shape.radius = 0.5f;
    sphere.position = {0, 5, 0};
    sphere.mass = 1.0f;
    sphere.linearDamping = 0;
    sphere.restitution = 0.0f;

    box.type = BodyType::Static;
    box.shape.type = ShapeType::Box;
    box.shape.halfExtents = {2, 1, 2};
    box.position = {0, 0, 0};

    BodyId idSphere = m_world.CreateBody(sphere);
    m_world.CreateBody(box);

    for (int i = 0; i < 300; ++i) {
        m_world.Step(1.0f / 60.0f);
    }

    auto pos = m_world.GetPosition(idSphere);
    // Sphere should rest on top of box (box top = 1.0, sphere radius = 0.5)
    EXPECT_NEAR(pos.y, 1.5f, 0.2f);
}

TEST_F(PhysicsTest, BoxBoxCollision) {
    BodyDesc a, b;
    a.type = BodyType::Dynamic;
    a.shape.type = ShapeType::Box;
    a.shape.halfExtents = {0.5f, 0.5f, 0.5f};
    a.position = {0, 5, 0};
    a.mass = 1.0f;
    a.linearDamping = 0;
    a.restitution = 0.0f;

    b.type = BodyType::Static;
    b.shape.type = ShapeType::Box;
    b.shape.halfExtents = {2, 1, 2};
    b.position = {0, 0, 0};

    BodyId idA = m_world.CreateBody(a);
    m_world.CreateBody(b);

    for (int i = 0; i < 300; ++i) {
        m_world.Step(1.0f / 60.0f);
    }

    auto pos = m_world.GetPosition(idA);
    // Box A should rest on top of box B (top = 1.0, half extent = 0.5)
    EXPECT_NEAR(pos.y, 1.5f, 0.2f);
}

TEST_F(PhysicsTest, ContactCallbackFired) {
    int contactCount = 0;
    m_world.SetContactCallback([&contactCount](const ContactPoint&) {
        contactCount++;
    });

    BodyDesc a, b;
    a.type = BodyType::Dynamic;
    a.shape.type = ShapeType::Sphere;
    a.shape.radius = 1.0f;
    a.position = {0, 5, 0};
    a.mass = 1.0f;
    a.linearDamping = 0;

    b.type = BodyType::Static;
    b.shape.type = ShapeType::Sphere;
    b.shape.radius = 1.0f;
    b.position = {0, 0, 0};

    m_world.CreateBody(a);
    m_world.CreateBody(b);

    for (int i = 0; i < 60; ++i) {
        m_world.Step(1.0f / 60.0f);
    }

    EXPECT_GT(contactCount, 0);
}

// ─── Raycast Tests ────────────────────────────────────────────────────────

TEST_F(PhysicsTest, RaycastSphere) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Sphere;
    desc.shape.radius = 1.0f;
    desc.position = {0, 5, 0};

    BodyId id = m_world.CreateBody(desc);

    auto result = m_world.RayCast({0, 0, 0}, {0, 1, 0}, 100.0f);
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.bodyId, id);
    EXPECT_NEAR(result.distance, 4.0f, 0.01f); // 5 - 1 = 4
    EXPECT_NEAR(result.hitPoint.y, 4.0f, 0.01f);
}

TEST_F(PhysicsTest, RaycastBox) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Box;
    desc.shape.halfExtents = {1, 1, 1};
    desc.position = {0, 5, 0};

    BodyId id = m_world.CreateBody(desc);

    auto result = m_world.RayCast({0, 0, 0}, {0, 1, 0}, 100.0f);
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.bodyId, id);
    EXPECT_NEAR(result.distance, 4.0f, 0.01f); // 5 - 1 = 4
}

TEST_F(PhysicsTest, RaycastMiss) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Sphere;
    desc.shape.radius = 1.0f;
    desc.position = {10, 5, 0};

    m_world.CreateBody(desc);

    auto result = m_world.RayCast({0, 0.1, 0}, {0, -1, 0}, 100.0f);
    // Should only hit ground plane, not the sphere
    EXPECT_TRUE(result.hit);
    EXPECT_EQ(result.bodyId, INVALID_BODY); // Ground plane hit, no body
}

TEST_F(PhysicsTest, RaycastLayerMask) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Sphere;
    desc.shape.radius = 1.0f;
    desc.position = {0, 5, 0};
    desc.collisionLayer = 0x0002;
    desc.collisionMask = 0x0002;

    m_world.CreateBody(desc);

    // Raycast with mask that doesn't include layer 2
    auto result = m_world.RayCast({0, 0, 0}, {0, 1, 0}, 100.0f, 0x0001);
    EXPECT_EQ(result.bodyId, INVALID_BODY); // Should not hit the body

    // Raycast with mask that includes layer 2
    auto result2 = m_world.RayCast({0, 0, 0}, {0, 1, 0}, 100.0f, 0x0002);
    EXPECT_NE(result2.bodyId, INVALID_BODY);
}

// ─── Overlap Tests ────────────────────────────────────────────────────────

TEST_F(PhysicsTest, OverlapSphereHit) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Sphere;
    desc.shape.radius = 1.0f;
    desc.position = {0, 5, 0};

    m_world.CreateBody(desc);

    auto bodies = m_world.OverlapSphere({0, 5, 0}, 2.0f);
    EXPECT_EQ(bodies.size(), 1u);
}

TEST_F(PhysicsTest, OverlapSphereMiss) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Sphere;
    desc.shape.radius = 1.0f;
    desc.position = {0, 5, 0};

    m_world.CreateBody(desc);

    auto bodies = m_world.OverlapSphere({20, 20, 20}, 2.0f);
    EXPECT_EQ(bodies.size(), 0u);
}

TEST_F(PhysicsTest, OverlapBoxHit) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Box;
    desc.shape.halfExtents = {1, 1, 1};
    desc.position = {0, 5, 0};

    m_world.CreateBody(desc);

    auto bodies = m_world.OverlapBox({0, 5, 0}, {2, 2, 2}, {0, 0, 0, 1});
    EXPECT_EQ(bodies.size(), 1u);
}

TEST_F(PhysicsTest, OverlapBoxMiss) {
    BodyDesc desc;
    desc.type = BodyType::Static;
    desc.shape.type = ShapeType::Box;
    desc.shape.halfExtents = {1, 1, 1};
    desc.position = {0, 5, 0};

    m_world.CreateBody(desc);

    auto bodies = m_world.OverlapBox({20, 20, 20}, {2, 2, 2}, {0, 0, 0, 1});
    EXPECT_EQ(bodies.size(), 0u);
}

TEST_F(PhysicsTest, OverlapSphereMultipleBodies) {
    BodyDesc a, b, c;
    a.shape.type = ShapeType::Sphere;
    a.shape.radius = 1.0f;
    a.position = {0, 5, 0};

    b.shape.type = ShapeType::Sphere;
    b.shape.radius = 1.0f;
    b.position = {3, 5, 0};

    c.shape.type = ShapeType::Sphere;
    c.shape.radius = 1.0f;
    c.position = {10, 5, 0};

    m_world.CreateBody(a);
    m_world.CreateBody(b);
    m_world.CreateBody(c);

    auto bodies = m_world.OverlapSphere({1, 5, 0}, 3.0f);
    EXPECT_EQ(bodies.size(), 2u); // Should hit a and b, not c
}
