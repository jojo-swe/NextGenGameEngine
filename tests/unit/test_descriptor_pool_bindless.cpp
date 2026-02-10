#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_descriptor_pool_manager.h"
#include "engine/rhi/common/rhi_bindless_texture_manager.h"

using namespace nge;
using namespace nge::rhi;

// ─── Descriptor Pool Manager Tests ───────────────────────────────────────

TEST(DescriptorPool, InitCreatesOnePool) {
    DescriptorPoolManager mgr;
    DescriptorPoolConfig config;
    config.setsPerPool = 256;
    config.maxPools = 4;
    EXPECT_TRUE(mgr.Init(nullptr, config));
    EXPECT_EQ(mgr.GetPoolCount(), 1u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalPools, 1u);
    EXPECT_EQ(stats.totalSetsAllocated, 0u);
    EXPECT_EQ(stats.growthEvents, 0u);

    mgr.Shutdown();
}

TEST(DescriptorPool, AllocateIncrementsCount) {
    DescriptorPoolManager mgr;
    DescriptorPoolConfig config;
    config.setsPerPool = 128;
    mgr.Init(nullptr, config);

    std::vector<DescriptorType> types = {DescriptorType::SampledImage, DescriptorType::UniformBuffer};
    auto set = mgr.Allocate(types);
    EXPECT_NE(set, 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalSetsAllocated, 1u);
    EXPECT_EQ(stats.allocationsPerType.at(static_cast<u8>(DescriptorType::SampledImage)), 1u);
    EXPECT_EQ(stats.allocationsPerType.at(static_cast<u8>(DescriptorType::UniformBuffer)), 1u);

    mgr.Shutdown();
}

TEST(DescriptorPool, GrowthWhenExhausted) {
    DescriptorPoolManager mgr;
    DescriptorPoolConfig config;
    config.setsPerPool = 2; // Very small pool
    config.maxPools = 4;
    config.allowGrowth = true;
    mgr.Init(nullptr, config);

    std::vector<DescriptorType> types = {DescriptorType::StorageBuffer};

    // Fill first pool
    mgr.Allocate(types);
    mgr.Allocate(types);
    EXPECT_EQ(mgr.GetPoolCount(), 1u);

    // This should trigger growth
    mgr.Allocate(types);
    EXPECT_EQ(mgr.GetPoolCount(), 2u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.growthEvents, 1u);
    EXPECT_EQ(stats.totalSetsAllocated, 3u);

    mgr.Shutdown();
}

TEST(DescriptorPool, NoGrowthWhenDisabled) {
    DescriptorPoolManager mgr;
    DescriptorPoolConfig config;
    config.setsPerPool = 1;
    config.maxPools = 1;
    config.allowGrowth = false;
    mgr.Init(nullptr, config);

    std::vector<DescriptorType> types = {DescriptorType::Sampler};
    mgr.Allocate(types); // Fills the only pool

    auto set = mgr.Allocate(types); // Should fail
    EXPECT_EQ(set, 0u);

    mgr.Shutdown();
}

TEST(DescriptorPool, ResetAllClearsAllocations) {
    DescriptorPoolManager mgr;
    DescriptorPoolConfig config;
    config.setsPerPool = 64;
    mgr.Init(nullptr, config);

    std::vector<DescriptorType> types = {DescriptorType::SampledImage};
    for (int i = 0; i < 10; ++i) mgr.Allocate(types);

    EXPECT_EQ(mgr.GetStats().totalSetsAllocated, 10u);

    mgr.ResetAll();
    EXPECT_EQ(mgr.GetStats().totalSetsAllocated, 0u);

    mgr.Shutdown();
}

TEST(DescriptorPool, FragmentationReporting) {
    DescriptorPoolManager mgr;
    DescriptorPoolConfig config;
    config.setsPerPool = 100;
    mgr.Init(nullptr, config);

    std::vector<DescriptorType> types = {DescriptorType::StorageImage};
    for (int i = 0; i < 50; ++i) mgr.Allocate(types);

    auto stats = mgr.GetStats();
    // 50 used out of 100 in a non-full pool = 50% fragmentation
    EXPECT_GT(stats.fragmentationPercent, 0.0f);

    mgr.Shutdown();
}

// ─── Bindless Texture Manager Tests ──────────────────────────────────────

TEST(BindlessTexture, InitReservesDefaultSlots) {
    BindlessTextureManager mgr;
    BindlessTextureConfig config;
    config.maxTextures = 1024;
    config.reservedSlots = 4;
    EXPECT_TRUE(mgr.Init(nullptr, config));

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalSlots, 1024u);
    EXPECT_EQ(stats.usedSlots, 1024u - (1024u - 4u)); // reserved slots count as used
    EXPECT_GE(stats.residentTextures, 4u); // Default textures are resident

    EXPECT_EQ(mgr.GetWhiteTextureIndex(), 0u);
    EXPECT_EQ(mgr.GetBlackTextureIndex(), 1u);
    EXPECT_EQ(mgr.GetDefaultNormalIndex(), 2u);
    EXPECT_EQ(mgr.GetErrorTextureIndex(), 3u);

    mgr.Shutdown();
}

TEST(BindlessTexture, RegisterReturnsUniqueSlot) {
    BindlessTextureManager mgr;
    BindlessTextureConfig config;
    config.maxTextures = 256;
    config.reservedSlots = 4;
    mgr.Init(nullptr, config);

    u32 slot1 = mgr.Register({}, "texture_a");
    u32 slot2 = mgr.Register({}, "texture_b");

    EXPECT_GE(slot1, 4u); // Past reserved
    EXPECT_GE(slot2, 4u);
    EXPECT_NE(slot1, slot2);

    mgr.Shutdown();
}

TEST(BindlessTexture, UnregisterRecyclesSlot) {
    BindlessTextureManager mgr;
    BindlessTextureConfig config;
    config.maxTextures = 8;
    config.reservedSlots = 4;
    mgr.Init(nullptr, config);

    u32 slot1 = mgr.Register({}, "tex1");
    mgr.Unregister(slot1);

    // Next register should reuse the freed slot
    u32 slot2 = mgr.Register({}, "tex2");
    EXPECT_EQ(slot1, slot2);

    mgr.Shutdown();
}

TEST(BindlessTexture, FullReturnsErrorIndex) {
    BindlessTextureManager mgr;
    BindlessTextureConfig config;
    config.maxTextures = 6;
    config.reservedSlots = 4;
    mgr.Init(nullptr, config);

    mgr.Register({}, "tex1");
    mgr.Register({}, "tex2");
    // Now all 6 slots are used (4 reserved + 2 registered)

    u32 overflow = mgr.Register({}, "tex3");
    EXPECT_EQ(overflow, mgr.GetErrorTextureIndex());

    mgr.Shutdown();
}

TEST(BindlessTexture, ResidencyTracking) {
    BindlessTextureManager mgr;
    BindlessTextureConfig config;
    config.maxTextures = 64;
    config.reservedSlots = 4;
    mgr.Init(nullptr, config);

    u32 slot = mgr.Register({}, "tex");
    EXPECT_TRUE(mgr.IsResident(slot));

    mgr.SetResident(slot, false);
    EXPECT_FALSE(mgr.IsResident(slot));

    mgr.SetResident(slot, true);
    EXPECT_TRUE(mgr.IsResident(slot));

    mgr.Shutdown();
}

TEST(BindlessTexture, CannotUnregisterReserved) {
    BindlessTextureManager mgr;
    BindlessTextureConfig config;
    config.maxTextures = 64;
    config.reservedSlots = 4;
    mgr.Init(nullptr, config);

    // Attempting to unregister reserved slot 0 should be a no-op
    mgr.Unregister(0);
    EXPECT_TRUE(mgr.IsResident(0)); // Still resident

    mgr.Shutdown();
}
