#include <gtest/gtest.h>
#include "engine/renderer/pipeline/instance_manager.h"
#include "engine/renderer/pipeline/mesh_registry.h"

using namespace nge;
using namespace nge::renderer;

// ─── Instance Manager Tests ──────────────────────────────────────────────

TEST(InstanceManager, InitialState) {
    InstanceManager mgr;
    mgr.Init(nullptr, 1024);

    EXPECT_EQ(mgr.GetInstanceCount(), 0u);
    EXPECT_EQ(mgr.GetMaxInstances(), 1024u);
    EXPECT_FLOAT_EQ(mgr.GetUtilization(), 0.0f);

    mgr.Shutdown();
}

TEST(InstanceManager, SubmitSingle) {
    InstanceManager mgr;
    mgr.Init(nullptr, 256);
    mgr.BeginFrame();

    GPUInstanceData inst{};
    inst.meshId = 0;
    inst.materialId = 1;
    inst.flags = static_cast<u32>(InstanceFlags::Visible | InstanceFlags::CastShadow);

    u32 idx = mgr.Submit(inst);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(mgr.GetInstanceCount(), 1u);

    mgr.Shutdown();
}

TEST(InstanceManager, SubmitBatch) {
    InstanceManager mgr;
    mgr.Init(nullptr, 256);
    mgr.BeginFrame();

    std::vector<GPUInstanceData> batch(10);
    for (u32 i = 0; i < 10; ++i) {
        batch[i].meshId = i;
        batch[i].materialId = i % 3;
    }

    mgr.Submit(batch.data(), static_cast<u32>(batch.size()));
    EXPECT_EQ(mgr.GetInstanceCount(), 10u);

    mgr.Shutdown();
}

TEST(InstanceManager, BeginFrameResetsCount) {
    InstanceManager mgr;
    mgr.Init(nullptr, 256);

    mgr.BeginFrame();
    GPUInstanceData inst{};
    mgr.Submit(inst);
    mgr.Submit(inst);
    EXPECT_EQ(mgr.GetInstanceCount(), 2u);

    mgr.BeginFrame(); // Should reset
    EXPECT_EQ(mgr.GetInstanceCount(), 0u);

    mgr.Shutdown();
}

TEST(InstanceManager, Utilization) {
    InstanceManager mgr;
    mgr.Init(nullptr, 100);
    mgr.BeginFrame();

    GPUInstanceData inst{};
    for (u32 i = 0; i < 50; ++i) mgr.Submit(inst);

    EXPECT_FLOAT_EQ(mgr.GetUtilization(), 0.5f);

    mgr.Shutdown();
}

TEST(InstanceManager, OverflowClamp) {
    InstanceManager mgr;
    mgr.Init(nullptr, 4);
    mgr.BeginFrame();

    GPUInstanceData inst{};
    for (u32 i = 0; i < 4; ++i) {
        EXPECT_NE(mgr.Submit(inst), UINT32_MAX);
    }

    // 5th should fail
    EXPECT_EQ(mgr.Submit(inst), UINT32_MAX);
    EXPECT_EQ(mgr.GetInstanceCount(), 4u);

    mgr.Shutdown();
}

TEST(InstanceManager, SortByMaterial) {
    InstanceManager mgr;
    mgr.Init(nullptr, 256);
    mgr.BeginFrame();

    GPUInstanceData inst{};
    inst.materialId = 3; mgr.Submit(inst);
    inst.materialId = 1; mgr.Submit(inst);
    inst.materialId = 2; mgr.Submit(inst);

    mgr.SortByMaterial();
    // After sort, instances should be ordered by materialId
    EXPECT_EQ(mgr.GetInstanceCount(), 3u);

    mgr.Shutdown();
}

TEST(InstanceManager, FlagsOperators) {
    auto flags = InstanceFlags::Visible | InstanceFlags::CastShadow | InstanceFlags::Static;
    EXPECT_EQ(static_cast<u32>(flags), 0b1011u); // bits 0, 1, 3
}

// ─── Mesh Registry Tests ─────────────────────────────────────────────────

TEST(MeshRegistry, InitialState) {
    MeshRegistry reg;
    reg.Init(nullptr, 128);

    EXPECT_EQ(reg.GetMeshCount(), 0u);
    EXPECT_EQ(reg.GetTotalVertices(), 0u);
    EXPECT_EQ(reg.GetTotalIndices(), 0u);

    reg.Shutdown();
}

TEST(MeshRegistry, RegisterAndLookup) {
    MeshRegistry reg;
    reg.Init(nullptr, 128);

    GPUMeshEntry entry;
    entry.totalVertices = 100;
    entry.totalIndices = 300;
    entry.vertexStride = 32;
    entry.lods.push_back({0, 300, 0, 100, 0, 0, 1.0f});

    MeshId id = reg.Register("TestMesh", entry);
    EXPECT_NE(id, INVALID_MESH_ID);
    EXPECT_EQ(reg.GetMeshCount(), 1u);

    const GPUMeshEntry* found = reg.Get(id);
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->name, "TestMesh");
    EXPECT_EQ(found->totalVertices, 100u);

    reg.Shutdown();
}

TEST(MeshRegistry, FindByName) {
    MeshRegistry reg;
    reg.Init(nullptr, 128);

    GPUMeshEntry entry;
    entry.totalVertices = 50;
    entry.vertexStride = 32;
    reg.Register("Cube", entry);

    MeshId id = reg.FindByName("Cube");
    EXPECT_NE(id, INVALID_MESH_ID);

    MeshId missing = reg.FindByName("Sphere");
    EXPECT_EQ(missing, INVALID_MESH_ID);

    reg.Shutdown();
}

TEST(MeshRegistry, DuplicateNameReturnsExisting) {
    MeshRegistry reg;
    reg.Init(nullptr, 128);

    GPUMeshEntry entry;
    entry.totalVertices = 50;
    entry.vertexStride = 32;

    MeshId id1 = reg.Register("Cube", entry);
    MeshId id2 = reg.Register("Cube", entry);
    EXPECT_EQ(id1, id2);
    EXPECT_EQ(reg.GetMeshCount(), 1u);

    reg.Shutdown();
}

TEST(MeshRegistry, Unregister) {
    MeshRegistry reg;
    reg.Init(nullptr, 128);

    GPUMeshEntry entry;
    entry.totalVertices = 50;
    entry.vertexStride = 32;

    MeshId id = reg.Register("Temp", entry);
    EXPECT_EQ(reg.GetMeshCount(), 1u);

    reg.Unregister(id);
    EXPECT_EQ(reg.GetMeshCount(), 0u);
    EXPECT_EQ(reg.Get(id), nullptr);
    EXPECT_EQ(reg.FindByName("Temp"), INVALID_MESH_ID);

    reg.Shutdown();
}

TEST(MeshRegistry, MultipleLODs) {
    MeshRegistry reg;
    reg.Init(nullptr, 128);

    GPUMeshEntry entry;
    entry.totalVertices = 1000;
    entry.totalIndices = 3000;
    entry.vertexStride = 32;
    entry.lods.push_back({0, 3000, 0, 1000, 0, 0, 1.0f});     // LOD0
    entry.lods.push_back({3000, 1500, 1000, 500, 0, 0, 0.5f}); // LOD1
    entry.lods.push_back({4500, 300, 1500, 100, 0, 0, 0.1f});  // LOD2

    MeshId id = reg.Register("DetailedMesh", entry);
    const GPUMeshEntry* mesh = reg.Get(id);
    ASSERT_NE(mesh, nullptr);
    EXPECT_EQ(mesh->lods.size(), 3u);
    EXPECT_FLOAT_EQ(mesh->lods[2].screenSizeThreshold, 0.1f);

    reg.Shutdown();
}

TEST(MeshRegistry, InvalidMeshIdConstant) {
    EXPECT_EQ(INVALID_MESH_ID, UINT32_MAX);
}
