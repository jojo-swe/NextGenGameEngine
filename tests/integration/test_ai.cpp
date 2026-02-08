#include <gtest/gtest.h>
#include "engine/ai/behavior_tree.h"

using namespace nge;
using namespace nge::ai;

class BehaviorTreeTest : public ::testing::Test {
protected:
    BTContext MakeContext(f32 dt = 1.0f / 60.0f) {
        BTContext ctx;
        ctx.entity = ecs::Entity(1, 1);
        ctx.blackboard = &m_blackboard;
        ctx.deltaTime = dt;
        return ctx;
    }

    Blackboard m_blackboard;
};

TEST_F(BehaviorTreeTest, ActionSuccess) {
    BehaviorTree tree;
    tree.SetRoot(std::make_unique<BTAction>(
        [](BTContext&) { return BTStatus::Success; }, "AlwaysSucceed"));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, ActionFailure) {
    BehaviorTree tree;
    tree.SetRoot(std::make_unique<BTAction>(
        [](BTContext&) { return BTStatus::Failure; }, "AlwaysFail"));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, ConditionTrue) {
    m_blackboard.Set("health", 100);

    BehaviorTree tree;
    tree.SetRoot(std::make_unique<BTCondition>(
        [](BTContext& ctx) { return ctx.blackboard->Get<int>("health", 0) > 50; },
        "HealthAbove50"));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, ConditionFalse) {
    m_blackboard.Set("health", 10);

    BehaviorTree tree;
    tree.SetRoot(std::make_unique<BTCondition>(
        [](BTContext& ctx) { return ctx.blackboard->Get<int>("health", 0) > 50; },
        "HealthAbove50"));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, SequenceAllSucceed) {
    auto seq = std::make_unique<BTSequence>();
    seq->AddChild(std::make_unique<BTAction>([](BTContext&) { return BTStatus::Success; }));
    seq->AddChild(std::make_unique<BTAction>([](BTContext&) { return BTStatus::Success; }));
    seq->AddChild(std::make_unique<BTAction>([](BTContext&) { return BTStatus::Success; }));

    BehaviorTree tree;
    tree.SetRoot(std::move(seq));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, SequenceOneFailsAborts) {
    int counter = 0;
    auto seq = std::make_unique<BTSequence>();
    seq->AddChild(std::make_unique<BTAction>([&](BTContext&) { counter++; return BTStatus::Success; }));
    seq->AddChild(std::make_unique<BTAction>([&](BTContext&) { counter++; return BTStatus::Failure; }));
    seq->AddChild(std::make_unique<BTAction>([&](BTContext&) { counter++; return BTStatus::Success; }));

    BehaviorTree tree;
    tree.SetRoot(std::move(seq));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Failure);
    EXPECT_EQ(counter, 2); // Third child never executed
}

TEST_F(BehaviorTreeTest, SelectorFirstSucceeds) {
    int counter = 0;
    auto sel = std::make_unique<BTSelector>();
    sel->AddChild(std::make_unique<BTAction>([&](BTContext&) { counter++; return BTStatus::Success; }));
    sel->AddChild(std::make_unique<BTAction>([&](BTContext&) { counter++; return BTStatus::Success; }));

    BehaviorTree tree;
    tree.SetRoot(std::move(sel));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Success);
    EXPECT_EQ(counter, 1); // Second child never evaluated
}

TEST_F(BehaviorTreeTest, SelectorFallsThrough) {
    auto sel = std::make_unique<BTSelector>();
    sel->AddChild(std::make_unique<BTAction>([](BTContext&) { return BTStatus::Failure; }));
    sel->AddChild(std::make_unique<BTAction>([](BTContext&) { return BTStatus::Failure; }));
    sel->AddChild(std::make_unique<BTAction>([](BTContext&) { return BTStatus::Success; }));

    BehaviorTree tree;
    tree.SetRoot(std::move(sel));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Success);
}

TEST_F(BehaviorTreeTest, InverterFlipsResult) {
    auto inv = std::make_unique<BTInverter>();
    inv->SetChild(std::make_unique<BTAction>([](BTContext&) { return BTStatus::Success; }));

    BehaviorTree tree;
    tree.SetRoot(std::move(inv));

    auto ctx = MakeContext();
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Failure);
}

TEST_F(BehaviorTreeTest, WaitReturnsRunningThenSuccess) {
    BehaviorTree tree;
    tree.SetRoot(std::make_unique<BTWait>(0.1f)); // 100ms wait

    auto ctx = MakeContext(0.05f); // 50ms per tick
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Running);
    EXPECT_EQ(tree.Tick(ctx), BTStatus::Success); // 100ms elapsed
}

TEST_F(BehaviorTreeTest, BlackboardOperations) {
    m_blackboard.Set("target", std::string("enemy1"));
    m_blackboard.Set("distance", 5.0f);
    m_blackboard.Set("ammo", 30);

    EXPECT_TRUE(m_blackboard.Has("target"));
    EXPECT_EQ(m_blackboard.Get<std::string>("target"), "enemy1");
    EXPECT_FLOAT_EQ(m_blackboard.Get<f32>("distance"), 5.0f);
    EXPECT_EQ(m_blackboard.Get<int>("ammo"), 30);
    EXPECT_EQ(m_blackboard.Get<int>("missing", -1), -1);

    m_blackboard.Remove("target");
    EXPECT_FALSE(m_blackboard.Has("target"));
}

TEST_F(BehaviorTreeTest, NavMeshPathfinding) {
    NavMesh navMesh;

    // Create a simple grid:  0 -- 1 -- 2
    //                         |         |
    //                         3 -- 4 -- 5
    u32 n0 = navMesh.AddNode({0, 0, 0});
    u32 n1 = navMesh.AddNode({5, 0, 0});
    u32 n2 = navMesh.AddNode({10, 0, 0});
    u32 n3 = navMesh.AddNode({0, 0, 10});
    u32 n4 = navMesh.AddNode({5, 0, 10});
    u32 n5 = navMesh.AddNode({10, 0, 10});

    navMesh.Connect(n0, n1);
    navMesh.Connect(n1, n2);
    navMesh.Connect(n0, n3);
    navMesh.Connect(n3, n4);
    navMesh.Connect(n4, n5);
    navMesh.Connect(n2, n5);

    // Find path from 0 to 5
    auto path = navMesh.FindPath(n0, n5);
    ASSERT_FALSE(path.empty());
    EXPECT_EQ(path.front().x, 0); // Start
    EXPECT_EQ(path.back().x, 10); // End at node 5

    // Find nearest node to a position
    u32 nearest = navMesh.FindNearestNode({4.5f, 0, 0.5f});
    EXPECT_EQ(nearest, n1); // Closest to (5, 0, 0)
}

TEST_F(BehaviorTreeTest, NavMeshNoPath) {
    NavMesh navMesh;
    u32 n0 = navMesh.AddNode({0, 0, 0});
    u32 n1 = navMesh.AddNode({10, 0, 0});
    // No connection

    auto path = navMesh.FindPath(n0, n1);
    EXPECT_TRUE(path.empty());
}
