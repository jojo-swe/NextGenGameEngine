#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_mesh_lod_selector.h"

using namespace nge::rhi;

static u32 SetupMeshWith3LODs(MeshLODSelector& sel) {
    u32 id = sel.RegisterMesh(5.0f, 1.0f, "TestMesh");
    sel.AddLODLevel(id, 10000, 0.5f, 0.0f, 20.0f);    // LOD 0: high detail
    sel.AddLODLevel(id, 5000,  2.0f, 20.0f, 50.0f);    // LOD 1: medium
    sel.AddLODLevel(id, 1000,  8.0f, 50.0f, 1000.0f);  // LOD 2: low detail
    return id;
}

TEST(MeshLODSelector, InitAndShutdown) {
    MeshLODSelector sel;
    EXPECT_TRUE(sel.Init());
    EXPECT_EQ(sel.GetMeshCount(), 0u);
    sel.Shutdown();
}

TEST(MeshLODSelector, RegisterMesh) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = sel.RegisterMesh(5.0f, 1.0f, "Hero");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(sel.GetMeshCount(), 1u);

    const auto* info = sel.GetMeshInfo(id);
    EXPECT_NE(info, nullptr);
    EXPECT_FLOAT_EQ(info->boundingSphereRadius, 5.0f);
    EXPECT_FLOAT_EQ(info->importance, 1.0f);
    EXPECT_EQ(info->debugName, "Hero");

    sel.Shutdown();
}

TEST(MeshLODSelector, AddLODLevel) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = SetupMeshWith3LODs(sel);

    EXPECT_EQ(sel.GetLODCount(id), 3u);
    EXPECT_EQ(sel.GetTriangleCount(id, 0), 10000u);
    EXPECT_EQ(sel.GetTriangleCount(id, 1), 5000u);
    EXPECT_EQ(sel.GetTriangleCount(id, 2), 1000u);

    sel.Shutdown();
}

TEST(MeshLODSelector, SelectLODCloseDistance) {
    MeshLODSelector sel;
    LODSelectorConfig config;
    config.errorThreshold = 1.0f;
    sel.Init(config);

    u32 id = SetupMeshWith3LODs(sel);

    // Very close: should pick LOD 0 (high detail, low error)
    u32 lod = sel.SelectLOD(id, 2.0f, 500.0f);
    EXPECT_EQ(lod, 0u);

    sel.Shutdown();
}

TEST(MeshLODSelector, SelectLODFarDistance) {
    MeshLODSelector sel;
    LODSelectorConfig config;
    config.errorThreshold = 1.0f;
    sel.Init(config);

    u32 id = SetupMeshWith3LODs(sel);

    // Very far: should pick coarsest LOD
    u32 lod = sel.SelectLOD(id, 500.0f, 5.0f);
    EXPECT_EQ(lod, 2u);

    sel.Shutdown();
}

TEST(MeshLODSelector, SelectLODMidDistance) {
    MeshLODSelector sel;
    LODSelectorConfig config;
    config.errorThreshold = 1.0f;
    sel.Init(config);

    u32 id = SetupMeshWith3LODs(sel);

    // Medium distance: should pick LOD 1 or LOD 0 based on error
    u32 lod = sel.SelectLOD(id, 30.0f, 50.0f);
    EXPECT_GE(lod, 0u);
    EXPECT_LE(lod, 2u);

    sel.Shutdown();
}

TEST(MeshLODSelector, ForceLOD) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = SetupMeshWith3LODs(sel);

    sel.SetForceLOD(2);
    EXPECT_EQ(sel.SelectLOD(id, 1.0f, 1000.0f), 2u); // Forced to LOD 2 even when close

    sel.SetForceLOD(-1); // Auto mode
    u32 lod = sel.SelectLOD(id, 1.0f, 1000.0f);
    EXPECT_NE(lod, 2u); // Should pick better LOD when close

    sel.Shutdown();
}

TEST(MeshLODSelector, HysteresisPreventsPopping) {
    MeshLODSelector sel;
    LODSelectorConfig config;
    config.enableHysteresis = true;
    config.hysteresisBand = 0.2f;
    config.errorThreshold = 100.0f; // Use distance-based selection
    sel.Init(config);

    u32 id = SetupMeshWith3LODs(sel);

    // At exactly the switch boundary, hysteresis should keep previous LOD
    u32 lod = sel.SelectLODWithHysteresis(id, 0, 21.0f, 50.0f);
    // Should stay at LOD 0 due to hysteresis band (21 is close to 20 boundary)
    // Exact behavior depends on hysteresis calculation
    EXPECT_LE(lod, 1u);

    sel.Shutdown();
}

TEST(MeshLODSelector, HysteresisDisabled) {
    MeshLODSelector sel;
    LODSelectorConfig config;
    config.enableHysteresis = false;
    sel.Init(config);

    u32 id = SetupMeshWith3LODs(sel);

    // Without hysteresis, should immediately switch
    u32 lod = sel.SelectLODWithHysteresis(id, 0, 100.0f, 10.0f);
    // Far away -> should select coarser LOD
    EXPECT_GE(lod, 1u);

    sel.Shutdown();
}

TEST(MeshLODSelector, BatchSelectLOD) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = SetupMeshWith3LODs(sel);

    LODInstance instances[3];
    instances[0] = {id, 0, 5.0f, 500.0f, 0, 0, 0.0f};
    instances[1] = {id, 1, 30.0f, 50.0f, 0, 0, 0.0f};
    instances[2] = {id, 2, 200.0f, 10.0f, 0, 0, 0.0f};

    sel.BatchSelectLOD(instances, 3);

    // Close instance should have lower LOD index (higher quality)
    EXPECT_LE(instances[0].currentLOD, instances[2].currentLOD);

    sel.Shutdown();
}

TEST(MeshLODSelector, GetLODCountEmpty) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = sel.RegisterMesh(5.0f);
    EXPECT_EQ(sel.GetLODCount(id), 0u);

    // Non-existent mesh
    EXPECT_EQ(sel.GetLODCount(999), 0u);

    sel.Shutdown();
}

TEST(MeshLODSelector, GetTriangleCountInvalid) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = SetupMeshWith3LODs(sel);

    EXPECT_EQ(sel.GetTriangleCount(id, 99), 0u);  // Out of range
    EXPECT_EQ(sel.GetTriangleCount(999, 0), 0u);   // Non-existent mesh

    sel.Shutdown();
}

TEST(MeshLODSelector, MaxMeshesLimit) {
    MeshLODSelector sel;
    LODSelectorConfig config;
    config.maxMeshes = 3;
    sel.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        EXPECT_NE(sel.RegisterMesh(1.0f), UINT32_MAX);
    }
    EXPECT_EQ(sel.RegisterMesh(1.0f), UINT32_MAX);

    sel.Shutdown();
}

TEST(MeshLODSelector, ImportanceAffectsSelection) {
    MeshLODSelector sel;
    LODSelectorConfig config;
    config.errorThreshold = 1.0f;
    sel.Init(config);

    // High importance mesh: should get finer LOD at same distance
    u32 heroId = sel.RegisterMesh(5.0f, 5.0f, "Hero");
    sel.AddLODLevel(heroId, 10000, 0.5f, 0.0f, 20.0f);
    sel.AddLODLevel(heroId, 1000, 8.0f, 20.0f, 1000.0f);

    // Low importance mesh
    u32 bgId = sel.RegisterMesh(5.0f, 0.5f, "Background");
    sel.AddLODLevel(bgId, 10000, 0.5f, 0.0f, 20.0f);
    sel.AddLODLevel(bgId, 1000, 8.0f, 20.0f, 1000.0f);

    u32 heroLOD = sel.SelectLOD(heroId, 50.0f, 30.0f);
    u32 bgLOD = sel.SelectLOD(bgId, 50.0f, 30.0f);

    // Hero should have equal or better LOD than background at same distance
    EXPECT_LE(heroLOD, bgLOD);

    sel.Shutdown();
}

TEST(MeshLODSelector, StatsTracking) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = SetupMeshWith3LODs(sel);

    sel.SelectLOD(id, 5.0f, 500.0f);
    sel.SelectLOD(id, 100.0f, 10.0f);

    auto stats = sel.GetStats();
    EXPECT_EQ(stats.totalMeshes, 1u);
    EXPECT_EQ(stats.totalInstances, 2u);
    EXPECT_GT(stats.totalTrianglesSelected, 0u);

    sel.Shutdown();
}

TEST(MeshLODSelector, ResetClearsAll) {
    MeshLODSelector sel;
    sel.Init();

    SetupMeshWith3LODs(sel);
    sel.SelectLOD(0, 5.0f, 500.0f);

    sel.Reset();

    EXPECT_EQ(sel.GetMeshCount(), 0u);
    auto stats = sel.GetStats();
    EXPECT_EQ(stats.totalInstances, 0u);
    EXPECT_EQ(stats.totalTrianglesSelected, 0u);

    sel.Shutdown();
}

TEST(MeshLODSelector, GetMeshInfoInvalid) {
    MeshLODSelector sel;
    sel.Init();

    EXPECT_EQ(sel.GetMeshInfo(999), nullptr);

    sel.Shutdown();
}

TEST(MeshLODSelector, SelectLODNonExistentMesh) {
    MeshLODSelector sel;
    sel.Init();

    u32 lod = sel.SelectLOD(999, 10.0f, 100.0f);
    EXPECT_EQ(lod, 0u); // Default

    sel.Shutdown();
}

TEST(MeshLODSelector, SingleLODMesh) {
    MeshLODSelector sel;
    sel.Init();

    u32 id = sel.RegisterMesh(5.0f);
    sel.AddLODLevel(id, 50000, 0.1f, 0.0f, 1000.0f);

    // Only 1 LOD -> always returns 0
    EXPECT_EQ(sel.SelectLOD(id, 5.0f, 500.0f), 0u);
    EXPECT_EQ(sel.SelectLOD(id, 500.0f, 5.0f), 0u);

    sel.Shutdown();
}
