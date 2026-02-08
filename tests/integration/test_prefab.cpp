#include <gtest/gtest.h>
#include "engine/scene/prefab/prefab_system.h"

using namespace nge;
using namespace nge::scene;

class PrefabTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_manager.Init();
    }

    void TearDown() override {
        m_manager.Shutdown();
    }

    PrefabManager m_manager;
};

TEST_F(PrefabTest, RegisterAndRetrieve) {
    PrefabData data;
    data.name = "TestPrefab";

    PrefabNode node;
    node.name = "Root";
    node.position = {1, 2, 3};
    node.rotation = {0, 0, 0, 1};
    node.scale = {1, 1, 1};
    data.nodes.push_back(node);

    PrefabId id = m_manager.RegisterPrefab(std::move(data));
    EXPECT_NE(id, INVALID_PREFAB);

    const PrefabData* retrieved = m_manager.GetPrefab(id);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->name, "TestPrefab");
    EXPECT_EQ(retrieved->nodes.size(), 1u);
    EXPECT_EQ(retrieved->nodes[0].name, "Root");
    EXPECT_FLOAT_EQ(retrieved->nodes[0].position.x, 1.0f);
}

TEST_F(PrefabTest, FindByName) {
    PrefabData data;
    data.name = "FindMe";
    PrefabNode node;
    node.name = "Root";
    data.nodes.push_back(node);

    PrefabId id = m_manager.RegisterPrefab(std::move(data));
    EXPECT_EQ(m_manager.FindByName("FindMe"), id);
    EXPECT_EQ(m_manager.FindByName("NotFound"), INVALID_PREFAB);
}

TEST_F(PrefabTest, Unregister) {
    PrefabData data;
    data.name = "Temporary";
    PrefabNode node;
    node.name = "Root";
    data.nodes.push_back(node);

    PrefabId id = m_manager.RegisterPrefab(std::move(data));
    EXPECT_NE(m_manager.GetPrefab(id), nullptr);

    m_manager.UnregisterPrefab(id);
    EXPECT_EQ(m_manager.GetPrefab(id), nullptr);
    EXPECT_EQ(m_manager.FindByName("Temporary"), INVALID_PREFAB);
}

TEST_F(PrefabTest, MultipleRegistrations) {
    for (u32 i = 0; i < 10; ++i) {
        PrefabData data;
        data.name = "Prefab_" + std::to_string(i);
        PrefabNode node;
        node.name = "Root_" + std::to_string(i);
        data.nodes.push_back(node);
        m_manager.RegisterPrefab(std::move(data));
    }

    EXPECT_EQ(m_manager.GetPrefabCount(), 10u);

    for (u32 i = 0; i < 10; ++i) {
        std::string name = "Prefab_" + std::to_string(i);
        PrefabId id = m_manager.FindByName(name);
        EXPECT_NE(id, INVALID_PREFAB);
        EXPECT_NE(m_manager.GetPrefab(id), nullptr);
    }
}

TEST_F(PrefabTest, HierarchicalPrefab) {
    PrefabData data;
    data.name = "Hierarchy";

    PrefabNode root;
    root.name = "Root";
    root.parentIndex = UINT32_MAX;
    data.nodes.push_back(root);

    PrefabNode child1;
    child1.name = "Child1";
    child1.parentIndex = 0; // Parent = Root
    child1.position = {1, 0, 0};
    data.nodes.push_back(child1);

    PrefabNode child2;
    child2.name = "Child2";
    child2.parentIndex = 0; // Parent = Root
    child2.position = {-1, 0, 0};
    data.nodes.push_back(child2);

    PrefabNode grandchild;
    grandchild.name = "Grandchild";
    grandchild.parentIndex = 1; // Parent = Child1
    grandchild.position = {0, 1, 0};
    data.nodes.push_back(grandchild);

    PrefabId id = m_manager.RegisterPrefab(std::move(data));
    const PrefabData* prefab = m_manager.GetPrefab(id);
    ASSERT_NE(prefab, nullptr);
    EXPECT_EQ(prefab->nodes.size(), 4u);
    EXPECT_EQ(prefab->nodes[3].parentIndex, 1u);
}

TEST_F(PrefabTest, ComponentApplierRegistration) {
    bool applierCalled = false;
    m_manager.RegisterComponentApplier(42, [&](ecs::Entity, const u8*, usize) {
        applierCalled = true;
    });

    // The applier won't be called without instantiation into a world,
    // but we verify registration doesn't crash
    EXPECT_FALSE(applierCalled);
}

TEST_F(PrefabTest, InvalidPrefabConstants) {
    EXPECT_EQ(INVALID_PREFAB, UINT32_MAX);
    EXPECT_EQ(m_manager.GetPrefab(INVALID_PREFAB), nullptr);
}
