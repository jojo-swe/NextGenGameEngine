#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_shader_binding_table.h"
#include <cstring>

using namespace nge::rhi;

TEST(ShaderBindingTable, InitAndShutdown) {
    ShaderBindingTableManager sbt;
    EXPECT_TRUE(sbt.Init());

    auto stats = sbt.GetStats();
    EXPECT_EQ(stats.totalRecords, 0u);
    EXPECT_EQ(stats.totalSizeBytes, 0u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, AddRayGenRecord) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    u32 idx = sbt.AddRayGenRecord(0xAA, nullptr, 0, "PrimaryRayGen");
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::RayGen), 1u);

    const auto* rec = sbt.GetRecord(SBTRecordType::RayGen, 0);
    EXPECT_NE(rec, nullptr);
    EXPECT_EQ(rec->shaderGroupHandle, 0xAAu);
    EXPECT_EQ(rec->type, SBTRecordType::RayGen);
    EXPECT_EQ(rec->debugName, "PrimaryRayGen");

    sbt.Shutdown();
}

TEST(ShaderBindingTable, AddMissRecord) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddMissRecord(0xBB, nullptr, 0, "SkyMiss");
    sbt.AddMissRecord(0xCC, nullptr, 0, "ShadowMiss");

    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::Miss), 2u);

    const auto* r0 = sbt.GetRecord(SBTRecordType::Miss, 0);
    const auto* r1 = sbt.GetRecord(SBTRecordType::Miss, 1);
    EXPECT_EQ(r0->shaderGroupHandle, 0xBBu);
    EXPECT_EQ(r1->shaderGroupHandle, 0xCCu);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, AddHitGroupRecord) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddHitGroupRecord(0x10, nullptr, 0, "OpaqueCHS");
    sbt.AddHitGroupRecord(0x20, nullptr, 0, "TransparentCHS");
    sbt.AddHitGroupRecord(0x30, nullptr, 0, "ShadowAHS");

    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::HitGroup), 3u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, AddCallableRecord) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddCallableRecord(0x50, nullptr, 0, "ProceduralNoise");
    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::Callable), 1u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, InlineData) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    struct MaterialData {
        float albedo[4];
        u32 textureIndex;
    };
    MaterialData mat = {{1.0f, 0.5f, 0.2f, 1.0f}, 42};

    u32 idx = sbt.AddHitGroupRecord(0x10, &mat, sizeof(mat), "MatHitGroup");
    EXPECT_EQ(idx, 0u);

    const auto* rec = sbt.GetRecord(SBTRecordType::HitGroup, 0);
    EXPECT_EQ(rec->inlineData.size(), sizeof(MaterialData));

    MaterialData readBack;
    std::memcpy(&readBack, rec->inlineData.data(), sizeof(MaterialData));
    EXPECT_EQ(readBack.textureIndex, 42u);
    EXPECT_FLOAT_EQ(readBack.albedo[0], 1.0f);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, BuildLayoutEmpty) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    auto layout = sbt.BuildLayout();
    EXPECT_EQ(layout.totalSize, 0u);
    EXPECT_EQ(layout.rayGen.recordCount, 0u);
    EXPECT_EQ(layout.miss.recordCount, 0u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, BuildLayoutBasic) {
    ShaderBindingTableManager sbt;
    SBTConfig config;
    config.handleSize = 32;
    config.handleAlignment = 64;
    config.baseAlignment = 64;
    sbt.Init(config);

    sbt.AddRayGenRecord(1);
    sbt.AddMissRecord(2);
    sbt.AddMissRecord(3);
    sbt.AddHitGroupRecord(4);
    sbt.AddHitGroupRecord(5);
    sbt.AddHitGroupRecord(6);

    auto layout = sbt.BuildLayout();

    // Ray gen: 1 record
    EXPECT_EQ(layout.rayGen.recordCount, 1u);
    EXPECT_GT(layout.rayGen.stride, 0u);
    EXPECT_EQ(layout.rayGen.stride % config.handleAlignment, 0u);

    // Miss: 2 records
    EXPECT_EQ(layout.miss.recordCount, 2u);
    EXPECT_EQ(layout.miss.bufferOffset % config.baseAlignment, 0u);

    // Hit group: 3 records
    EXPECT_EQ(layout.hitGroup.recordCount, 3u);
    EXPECT_EQ(layout.hitGroup.bufferOffset % config.baseAlignment, 0u);

    // Total size should be non-zero
    EXPECT_GT(layout.totalSize, 0u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, WriteToBuffer) {
    ShaderBindingTableManager sbt;
    SBTConfig config;
    config.handleSize = 8; // Simplified for test
    config.handleAlignment = 8;
    config.baseAlignment = 16;
    sbt.Init(config);

    u64 rayGenHandle = 0xDEADBEEFCAFEBABE;
    sbt.AddRayGenRecord(rayGenHandle);
    sbt.AddMissRecord(0x1111111122222222);

    auto layout = sbt.BuildLayout();

    std::vector<u8> buffer(static_cast<size_t>(layout.totalSize), 0);
    u64 written = sbt.WriteToBuffer(buffer.data(), buffer.size());

    EXPECT_EQ(written, layout.totalSize);

    // Verify ray gen handle is written at the start of its region
    u64 readHandle = 0;
    std::memcpy(&readHandle, buffer.data() + layout.rayGen.bufferOffset, sizeof(u64));
    EXPECT_EQ(readHandle, rayGenHandle);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, WriteToBufferTooSmall) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddRayGenRecord(1);

    auto layout = sbt.BuildLayout();

    std::vector<u8> buffer(1, 0); // Too small
    u64 written = sbt.WriteToBuffer(buffer.data(), buffer.size());
    EXPECT_EQ(written, 0u); // Should fail

    sbt.Shutdown();
}

TEST(ShaderBindingTable, GetRecordOutOfBounds) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddRayGenRecord(1);

    EXPECT_EQ(sbt.GetRecord(SBTRecordType::RayGen, 1), nullptr);  // Out of bounds
    EXPECT_EQ(sbt.GetRecord(SBTRecordType::Miss, 0), nullptr);     // Empty category

    sbt.Shutdown();
}

TEST(ShaderBindingTable, ClearRecordsByType) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddRayGenRecord(1);
    sbt.AddMissRecord(2);
    sbt.AddHitGroupRecord(3);

    sbt.ClearRecords(SBTRecordType::Miss);

    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::RayGen), 1u);
    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::Miss), 0u);
    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::HitGroup), 1u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, ClearAll) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddRayGenRecord(1);
    sbt.AddMissRecord(2);
    sbt.AddHitGroupRecord(3);
    sbt.AddCallableRecord(4);

    sbt.ClearAll();

    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::RayGen), 0u);
    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::Miss), 0u);
    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::HitGroup), 0u);
    EXPECT_EQ(sbt.GetRecordCount(SBTRecordType::Callable), 0u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, StatsTracking) {
    ShaderBindingTableManager sbt;
    sbt.Init();

    sbt.AddRayGenRecord(1);
    sbt.AddMissRecord(2);
    sbt.AddMissRecord(3);
    sbt.AddHitGroupRecord(4);
    sbt.AddHitGroupRecord(5);
    sbt.AddHitGroupRecord(6);
    sbt.AddCallableRecord(7);

    auto stats = sbt.GetStats();
    EXPECT_EQ(stats.rayGenRecords, 1u);
    EXPECT_EQ(stats.missRecords, 2u);
    EXPECT_EQ(stats.hitGroupRecords, 3u);
    EXPECT_EQ(stats.callableRecords, 1u);
    EXPECT_EQ(stats.totalRecords, 7u);
    EXPECT_GT(stats.totalSizeBytes, 0u);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, AlignmentCorrectness) {
    ShaderBindingTableManager sbt;
    SBTConfig config;
    config.handleSize = 32;
    config.handleAlignment = 64;
    config.baseAlignment = 256;
    sbt.Init(config);

    sbt.AddRayGenRecord(1);
    sbt.AddMissRecord(2);
    sbt.AddHitGroupRecord(3);

    auto layout = sbt.BuildLayout();

    // All region offsets should be aligned to baseAlignment
    EXPECT_EQ(layout.rayGen.bufferOffset % config.baseAlignment, 0u);
    if (layout.miss.recordCount > 0) {
        EXPECT_EQ(layout.miss.bufferOffset % config.baseAlignment, 0u);
    }
    if (layout.hitGroup.recordCount > 0) {
        EXPECT_EQ(layout.hitGroup.bufferOffset % config.baseAlignment, 0u);
    }

    // Strides should be aligned to handleAlignment
    if (layout.rayGen.stride > 0) {
        EXPECT_EQ(layout.rayGen.stride % config.handleAlignment, 0u);
    }

    sbt.Shutdown();
}

TEST(ShaderBindingTable, InlineDataAffectsStride) {
    ShaderBindingTableManager sbt;
    SBTConfig config;
    config.handleSize = 32;
    config.handleAlignment = 64;
    config.baseAlignment = 64;
    sbt.Init(config);

    // Add hit group without inline data
    sbt.AddHitGroupRecord(1);

    auto layout1 = sbt.BuildLayout();
    u64 stride1 = layout1.hitGroup.stride;

    sbt.ClearAll();

    // Add hit group with 128 bytes of inline data
    u8 bigData[128] = {};
    sbt.AddHitGroupRecord(1, bigData, 128);

    auto layout2 = sbt.BuildLayout();
    u64 stride2 = layout2.hitGroup.stride;

    // Stride with inline data should be larger
    EXPECT_GT(stride2, stride1);

    sbt.Shutdown();
}

TEST(ShaderBindingTable, RecordTooLarge) {
    ShaderBindingTableManager sbt;
    SBTConfig config;
    config.handleSize = 32;
    config.maxRecordSize = 64;
    sbt.Init(config);

    // Try to add record with too much inline data
    u8 bigData[128] = {};
    u32 idx = sbt.AddHitGroupRecord(1, bigData, 128); // 32 + 128 = 160 > 64

    EXPECT_EQ(idx, UINT32_MAX); // Should fail

    sbt.Shutdown();
}
