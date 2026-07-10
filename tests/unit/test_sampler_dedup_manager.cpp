#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_sampler_dedup_manager.h"

using namespace nge;
using namespace nge::rhi;

static SamplerDesc MakeDesc(SamplerFilter mag = SamplerFilter::Linear,
                             SamplerFilter min = SamplerFilter::Linear,
                             SamplerAddressMode addr = SamplerAddressMode::Repeat) {
    SamplerDesc desc;
    desc.magFilter = mag;
    desc.minFilter = min;
    desc.addressU = addr;
    desc.addressV = addr;
    desc.addressW = addr;
    return desc;
}

TEST(SamplerDedupManager, InitAndShutdown) {
    SamplerDedupManager mgr;
    EXPECT_TRUE(mgr.Init());

    EXPECT_EQ(mgr.GetCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalSamplers, 0u);
    EXPECT_EQ(stats.totalAcquires, 0u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, AcquireCreatesNewSampler) {
    SamplerDedupManager mgr;
    mgr.Init();

    auto desc = MakeDesc();
    u64 handle = mgr.Acquire(desc);
    EXPECT_NE(handle, 0u);
    EXPECT_EQ(mgr.GetCount(), 1u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalAcquires, 1u);
    EXPECT_EQ(stats.deduplicated, 0u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, AcquireDuplicateDeduplicates) {
    SamplerDedupManager mgr;
    mgr.Init();

    auto desc = MakeDesc();
    u64 h1 = mgr.Acquire(desc);
    u64 h2 = mgr.Acquire(desc);

    EXPECT_EQ(h1, h2); // Same handle returned
    EXPECT_EQ(mgr.GetCount(), 1u); // Only 1 sampler created

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalAcquires, 2u);
    EXPECT_EQ(stats.deduplicated, 1u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, DifferentDescsDifferentSamplers) {
    SamplerDedupManager mgr;
    mgr.Init();

    u64 h1 = mgr.Acquire(MakeDesc(SamplerFilter::Linear, SamplerFilter::Linear));
    u64 h2 = mgr.Acquire(MakeDesc(SamplerFilter::Nearest, SamplerFilter::Nearest));

    EXPECT_NE(h1, h2);
    EXPECT_EQ(mgr.GetCount(), 2u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, RefCountIncrementsOnAcquire) {
    SamplerDedupManager mgr;
    SamplerDedupConfig config;
    config.enableRefCounting = true;
    mgr.Init(config);

    auto desc = MakeDesc();
    u64 handle = mgr.Acquire(desc);
    EXPECT_EQ(mgr.GetRefCount(handle), 1u);

    mgr.Acquire(desc); // Duplicate acquire
    EXPECT_EQ(mgr.GetRefCount(handle), 2u);

    mgr.Acquire(desc); // Another
    EXPECT_EQ(mgr.GetRefCount(handle), 3u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, ReleaseDecrementsRefCount) {
    SamplerDedupManager mgr;
    SamplerDedupConfig config;
    config.enableRefCounting = true;
    mgr.Init(config);

    auto desc = MakeDesc();
    u64 handle = mgr.Acquire(desc);
    mgr.Acquire(desc); // refCount = 2

    mgr.Release(handle);
    EXPECT_EQ(mgr.GetRefCount(handle), 1u);
    EXPECT_EQ(mgr.GetCount(), 1u); // Still alive

    mgr.Release(handle);
    EXPECT_EQ(mgr.GetCount(), 0u); // Destroyed at refCount 0

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalDestroyed, 1u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, ReleaseNonExistent) {
    SamplerDedupManager mgr;
    mgr.Init();

    mgr.Release(9999); // Should not crash
    EXPECT_EQ(mgr.GetStats().totalReleases, 1u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, Exists) {
    SamplerDedupManager mgr;
    mgr.Init();

    auto desc = MakeDesc();
    EXPECT_FALSE(mgr.Exists(desc));

    mgr.Acquire(desc);
    EXPECT_TRUE(mgr.Exists(desc));

    auto other = MakeDesc(SamplerFilter::Nearest, SamplerFilter::Nearest);
    EXPECT_FALSE(mgr.Exists(other));

    mgr.Shutdown();
}

TEST(SamplerDedupManager, GetEntry) {
    SamplerDedupManager mgr;
    mgr.Init();

    auto desc = MakeDesc(SamplerFilter::Linear, SamplerFilter::Linear, SamplerAddressMode::ClampToEdge);
    u64 handle = mgr.Acquire(desc);

    const auto* entry = mgr.GetEntry(handle);
    EXPECT_NE(entry, nullptr);
    EXPECT_EQ(entry->samplerHandle, handle);
    EXPECT_EQ(entry->refCount, 1u);
    EXPECT_EQ(entry->desc.magFilter, SamplerFilter::Linear);
    EXPECT_EQ(entry->desc.addressU, SamplerAddressMode::ClampToEdge);

    EXPECT_EQ(mgr.GetEntry(9999), nullptr);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, CanonicalizationDebugNameIgnored) {
    SamplerDedupManager mgr;
    SamplerDedupConfig config;
    config.canonicalize = true;
    mgr.Init(config);

    SamplerDesc d1 = MakeDesc();
    d1.debugName = "MaterialA_Albedo";

    SamplerDesc d2 = MakeDesc();
    d2.debugName = "MaterialB_Normal"; // Different name, same state

    u64 h1 = mgr.Acquire(d1);
    u64 h2 = mgr.Acquire(d2);

    EXPECT_EQ(h1, h2); // Should deduplicate (debug name ignored)
    EXPECT_EQ(mgr.GetCount(), 1u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, CanonicalizationAnisotropy) {
    SamplerDedupManager mgr;
    SamplerDedupConfig config;
    config.canonicalize = true;
    mgr.Init(config);

    SamplerDesc d1 = MakeDesc();
    d1.anisotropyEnable = false;
    d1.maxAnisotropy = 16.0f; // Should be normalized to 1.0

    SamplerDesc d2 = MakeDesc();
    d2.anisotropyEnable = false;
    d2.maxAnisotropy = 1.0f;

    u64 h1 = mgr.Acquire(d1);
    u64 h2 = mgr.Acquire(d2);

    EXPECT_EQ(h1, h2); // Canonicalized to same state
    EXPECT_EQ(mgr.GetStats().deduplicated, 1u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, MaxSamplersLimit) {
    SamplerDedupManager mgr;
    SamplerDedupConfig config;
    config.maxSamplers = 3;
    mgr.Init(config);

    for (u32 i = 0; i < 3; ++i) {
        auto desc = MakeDesc(static_cast<SamplerFilter>(i % 3));
        desc.mipLodBias = static_cast<float>(i); // Make unique
        EXPECT_NE(mgr.Acquire(desc), 0u);
    }
    EXPECT_EQ(mgr.GetCount(), 3u);

    // 4th should fail
    SamplerDesc extra = MakeDesc();
    extra.mipLodBias = 99.0f;
    EXPECT_EQ(mgr.Acquire(extra), 0u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, PurgeUnreferenced) {
    SamplerDedupManager mgr;
    SamplerDedupConfig config;
    config.enableRefCounting = true;
    mgr.Init(config);

    auto d1 = MakeDesc(SamplerFilter::Linear, SamplerFilter::Linear);
    auto d2 = MakeDesc(SamplerFilter::Nearest, SamplerFilter::Nearest);
    auto d3 = MakeDesc(SamplerFilter::Linear, SamplerFilter::Nearest);

    u64 h1 = mgr.Acquire(d1);
    u64 h2 = mgr.Acquire(d2);
    u64 h3 = mgr.Acquire(d3);

    // Release h1 and h3, keep h2
    mgr.Release(h1);
    mgr.Release(h3);

    // h1 and h3 were destroyed on Release since refCount hit 0
    // So count should be 1
    EXPECT_EQ(mgr.GetCount(), 1u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, ResetClearsAll) {
    SamplerDedupManager mgr;
    mgr.Init();

    mgr.Acquire(MakeDesc());
    mgr.Acquire(MakeDesc(SamplerFilter::Nearest, SamplerFilter::Nearest));

    mgr.Reset();

    EXPECT_EQ(mgr.GetCount(), 0u);
    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalAcquires, 0u);
    EXPECT_EQ(stats.deduplicated, 0u);
    EXPECT_EQ(stats.peakSamplers, 0u);

    mgr.Shutdown();
}

TEST(SamplerDedupManager, PeakSamplers) {
    SamplerDedupManager mgr;
    SamplerDedupConfig config;
    config.enableRefCounting = true;
    mgr.Init(config);

    auto d1 = MakeDesc(SamplerFilter::Linear, SamplerFilter::Linear);
    auto d2 = MakeDesc(SamplerFilter::Nearest, SamplerFilter::Nearest);
    auto d3 = MakeDesc(SamplerFilter::Linear, SamplerFilter::Nearest);

    u64 h1 = mgr.Acquire(d1);
    mgr.Acquire(d2);
    mgr.Acquire(d3);

    EXPECT_EQ(mgr.GetStats().peakSamplers, 3u);

    mgr.Release(h1); // Destroys, count drops to 2

    EXPECT_EQ(mgr.GetCount(), 2u);
    EXPECT_EQ(mgr.GetStats().peakSamplers, 3u); // Peak unchanged

    mgr.Shutdown();
}

TEST(SamplerDedupManager, AddressModeVariation) {
    SamplerDedupManager mgr;
    mgr.Init();

    u64 h1 = mgr.Acquire(MakeDesc(SamplerFilter::Linear, SamplerFilter::Linear, SamplerAddressMode::Repeat));
    u64 h2 = mgr.Acquire(MakeDesc(SamplerFilter::Linear, SamplerFilter::Linear, SamplerAddressMode::ClampToEdge));
    u64 h3 = mgr.Acquire(MakeDesc(SamplerFilter::Linear, SamplerFilter::Linear, SamplerAddressMode::MirroredRepeat));

    EXPECT_NE(h1, h2);
    EXPECT_NE(h2, h3);
    EXPECT_EQ(mgr.GetCount(), 3u);

    mgr.Shutdown();
}
