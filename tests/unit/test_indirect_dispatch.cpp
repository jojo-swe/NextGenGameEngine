#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_indirect_dispatch_builder.h"

using namespace nge::rhi;

TEST(IndirectDispatchBuilder, InitAndShutdown) {
    IndirectDispatchBuilder builder;
    EXPECT_TRUE(builder.Init());

    auto stats = builder.GetStats();
    EXPECT_EQ(stats.activeSlots, 0u);
    EXPECT_EQ(stats.totalDispatches, 0u);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, CreateSlot) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.countBufferHandle = 100;
    req.countBufferOffset = 0;
    req.workgroupSize = 64;
    req.maxGroupCount = 65535;
    req.is2D = false;
    req.debugName = "ParticleSim";

    u32 id = builder.CreateSlot(req);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(builder.GetStats().activeSlots, 1u);

    const auto* slot = builder.GetSlot(id);
    EXPECT_NE(slot, nullptr);
    EXPECT_EQ(slot->request.workgroupSize, 64u);
    EXPECT_EQ(slot->request.debugName, "ParticleSim");

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, BuildCommand1D) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.workgroupSize = 64;
    req.maxGroupCount = 65535;
    req.is2D = false;

    // 256 elements / 64 per group = 4 groups
    auto cmd = builder.BuildCommand(256, req);
    EXPECT_EQ(cmd.groupCountX, 4u);
    EXPECT_EQ(cmd.groupCountY, 1u);
    EXPECT_EQ(cmd.groupCountZ, 1u);

    // 257 elements / 64 = ceil = 5 groups
    cmd = builder.BuildCommand(257, req);
    EXPECT_EQ(cmd.groupCountX, 5u);

    // 0 elements = 0 groups
    cmd = builder.BuildCommand(0, req);
    EXPECT_EQ(cmd.groupCountX, 0u);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, BuildCommand2D) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.workgroupSize = 8;
    req.maxGroupCount = 65535;
    req.is2D = true;
    req.dispatchWidth = 1920;

    // 1920*1080 = 2073600 elements
    auto cmd = builder.BuildCommand(2073600, req);
    EXPECT_EQ(cmd.groupCountX, 240u); // 1920/8
    EXPECT_EQ(cmd.groupCountY, 1080u); // 2073600/1920
    EXPECT_EQ(cmd.groupCountZ, 1u);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, ClampMaxGroupCount) {
    IndirectDispatchBuilder builder;
    IndirectDispatchConfig config;
    config.clampToMaxGroups = true;
    builder.Init(config);

    IndirectDispatchRequest req;
    req.workgroupSize = 1;
    req.maxGroupCount = 100;
    req.is2D = false;

    // 500 elements / 1 per group = 500 groups -> clamped to 100
    auto cmd = builder.BuildCommand(500, req);
    EXPECT_EQ(cmd.groupCountX, 100u);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, DestroySlot) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.workgroupSize = 64;
    req.maxGroupCount = 65535;
    req.is2D = false;

    u32 id = builder.CreateSlot(req);
    EXPECT_EQ(builder.GetStats().activeSlots, 1u);

    builder.DestroySlot(id);
    EXPECT_EQ(builder.GetStats().activeSlots, 0u);
    EXPECT_EQ(builder.GetSlot(id), nullptr);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, MaxSlotsLimit) {
    IndirectDispatchBuilder builder;
    IndirectDispatchConfig config;
    config.maxSlots = 3;
    builder.Init(config);

    IndirectDispatchRequest req;
    req.workgroupSize = 64;
    req.maxGroupCount = 65535;
    req.is2D = false;

    EXPECT_NE(builder.CreateSlot(req), 0u);
    EXPECT_NE(builder.CreateSlot(req), 0u);
    EXPECT_NE(builder.CreateSlot(req), 0u);
    EXPECT_EQ(builder.CreateSlot(req), 0u); // Exceeds limit

    EXPECT_EQ(builder.GetStats().activeSlots, 3u);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, GetCommandOffset) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.workgroupSize = 64;
    req.maxGroupCount = 65535;
    req.is2D = false;

    u32 id1 = builder.CreateSlot(req);
    u32 id2 = builder.CreateSlot(req);

    u64 off1 = builder.GetCommandOffset(id1);
    u64 off2 = builder.GetCommandOffset(id2);

    EXPECT_EQ(off1, 0u);
    EXPECT_EQ(off2, sizeof(DispatchIndirectCommand));
    EXPECT_EQ(builder.GetCommandOffset(999), UINT64_MAX);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, GetActiveSlotIds) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.workgroupSize = 64;
    req.maxGroupCount = 65535;
    req.is2D = false;

    u32 id1 = builder.CreateSlot(req);
    u32 id2 = builder.CreateSlot(req);

    auto ids = builder.GetActiveSlotIds();
    EXPECT_EQ(ids.size(), 2u);

    builder.DestroySlot(id1);
    ids = builder.GetActiveSlotIds();
    EXPECT_EQ(ids.size(), 1u);
    EXPECT_EQ(ids[0], id2);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, RecordDispatchStats) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.workgroupSize = 64;
    req.maxGroupCount = 100;
    req.is2D = false;

    u32 id = builder.CreateSlot(req);

    builder.RecordDispatch(id, 50);
    builder.RecordDispatch(id, 100); // Hits max -> clamped count

    auto stats = builder.GetStats();
    EXPECT_EQ(stats.totalDispatches, 2u);
    EXPECT_EQ(stats.clampedDispatches, 1u);
    EXPECT_EQ(stats.totalWorkgroups, 150u);

    builder.Shutdown();
}

TEST(IndirectDispatchBuilder, ClearResetsAll) {
    IndirectDispatchBuilder builder;
    builder.Init();

    IndirectDispatchRequest req;
    req.workgroupSize = 64;
    req.maxGroupCount = 65535;
    req.is2D = false;

    builder.CreateSlot(req);
    builder.CreateSlot(req);
    EXPECT_EQ(builder.GetStats().activeSlots, 2u);

    builder.Clear();
    EXPECT_EQ(builder.GetStats().activeSlots, 0u);

    // New slot should get offset 0 again
    u32 id = builder.CreateSlot(req);
    EXPECT_EQ(builder.GetCommandOffset(id), 0u);

    builder.Shutdown();
}
