#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_format_negotiator.h"
#include "engine/rhi/common/rhi_occlusion_compactor.h"

using namespace nge::rhi;

// ─── Format Negotiator Tests ─────────────────────────────────────────────

TEST(FormatNegotiator, InitDesktopMode) {
    FormatNegotiator neg;
    FormatNegotiatorConfig config;
    config.preferBandwidthOverPrecision = false;
    EXPECT_TRUE(neg.Init(nullptr, config));

    auto stats = neg.GetStats();
    EXPECT_EQ(stats.totalNegotiations, 0u);

    neg.Shutdown();
}

TEST(FormatNegotiator, NegotiateColorHDR) {
    FormatNegotiator neg;
    FormatNegotiatorConfig config;
    config.preferBandwidthOverPrecision = false;
    neg.Init(nullptr, config);

    auto result = neg.Negotiate(AttachmentPurpose::ColorHDR);
    EXPECT_GT(result.format, 0u);
    EXPECT_EQ(result.purpose, AttachmentPurpose::ColorHDR);
    EXPECT_FALSE(result.formatName.empty());
    // Desktop prefers R16G16B16A16_SFLOAT (format 97)
    EXPECT_EQ(result.format, 97u);
    EXPECT_FALSE(result.isFallback);

    neg.Shutdown();
}

TEST(FormatNegotiator, NegotiateMobilePrefersBandwidth) {
    FormatNegotiator neg;
    FormatNegotiatorConfig config;
    config.preferBandwidthOverPrecision = true;
    neg.Init(nullptr, config);

    auto result = neg.Negotiate(AttachmentPurpose::ColorHDR);
    // Mobile prefers R11G11B10_UFLOAT (format 26)
    EXPECT_EQ(result.format, 26u);

    auto shadow = neg.Negotiate(AttachmentPurpose::ShadowMap);
    // Mobile prefers D16_UNORM (format 124)
    EXPECT_EQ(shadow.format, 124u);

    neg.Shutdown();
}

TEST(FormatNegotiator, NegotiateDepthFormats) {
    FormatNegotiator neg;
    neg.Init(nullptr);

    auto depthOnly = neg.Negotiate(AttachmentPurpose::DepthOnly);
    EXPECT_GT(depthOnly.format, 0u);

    auto depthStencil = neg.Negotiate(AttachmentPurpose::DepthStencil);
    EXPECT_GT(depthStencil.format, 0u);
    EXPECT_NE(depthOnly.format, depthStencil.format);

    neg.Shutdown();
}

TEST(FormatNegotiator, NegotiateAllPurposes) {
    FormatNegotiator neg;
    neg.Init(nullptr);

    auto all = neg.GetAllNegotiated();
    EXPECT_GE(all.size(), 10u); // At least 10 fallback chains

    for (const auto& result : all) {
        EXPECT_GT(result.format, 0u);
        EXPECT_FALSE(result.formatName.empty());
    }

    neg.Shutdown();
}

TEST(FormatNegotiator, OverrideFormat) {
    FormatNegotiator neg;
    neg.Init(nullptr);

    auto original = neg.Negotiate(AttachmentPurpose::AOBuffer);
    u32 originalFormat = original.format;

    neg.OverrideFormat(AttachmentPurpose::AOBuffer, 76u); // R16_SFLOAT
    auto overridden = neg.Negotiate(AttachmentPurpose::AOBuffer);
    EXPECT_EQ(overridden.format, 76u);
    EXPECT_NE(overridden.format, originalFormat);

    neg.Shutdown();
}

TEST(FormatNegotiator, StorageImageRequirement) {
    FormatNegotiator neg;
    neg.Init(nullptr);

    auto result = neg.NegotiateWithRequirements(AttachmentPurpose::StorageImage,
                                                 false, false, true);
    EXPECT_GT(result.format, 0u);

    neg.Shutdown();
}

TEST(FormatNegotiator, FallbackChainAccess) {
    FormatNegotiator neg;
    neg.Init(nullptr);

    const auto* chain = neg.GetFallbackChain(AttachmentPurpose::GBufferAlbedo);
    EXPECT_NE(chain, nullptr);
    EXPECT_GE(chain->candidates.size(), 1u);

    const auto* invalid = neg.GetFallbackChain(static_cast<AttachmentPurpose>(255));
    EXPECT_EQ(invalid, nullptr);

    neg.Shutdown();
}

TEST(FormatNegotiator, StatsTracking) {
    FormatNegotiator neg;
    neg.Init(nullptr);

    neg.Negotiate(AttachmentPurpose::ColorHDR);
    neg.Negotiate(AttachmentPurpose::DepthOnly);
    neg.Negotiate(AttachmentPurpose::AOBuffer);

    auto stats = neg.GetStats();
    EXPECT_GE(stats.totalNegotiations, 3u);
    EXPECT_GE(stats.preferredFormatHits, 1u);

    neg.Shutdown();
}

// ─── Occlusion Compactor Tests ───────────────────────────────────────────

TEST(OcclusionCompactor, InitAndShutdown) {
    OcclusionCompactor compactor;
    OcclusionCompactorConfig config;
    config.maxInstances = 1024;
    EXPECT_TRUE(compactor.Init(nullptr, config));

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.totalInstances, 0u);

    compactor.Shutdown();
}

TEST(OcclusionCompactor, CompactAllVisible) {
    OcclusionCompactor compactor;
    compactor.Init(nullptr);

    OcclusionInstance instances[3] = {
        {0, 100, true},
        {1, 50,  true},
        {2, 200, true},
    };
    compactor.SubmitResults(instances, 3);

    auto result = compactor.Compact();
    EXPECT_EQ(result.visibleCount, 3u);
    EXPECT_EQ(result.occludedCount, 0u);
    EXPECT_EQ(result.visibleInstanceIds.size(), 3u);

    compactor.Shutdown();
}

TEST(OcclusionCompactor, CompactWithOcclusion) {
    OcclusionCompactor compactor;
    OcclusionCompactorConfig config;
    config.occlusionThreshold = 0;
    compactor.Init(nullptr, config);

    OcclusionInstance instances[4] = {
        {10, 100, true},   // Visible
        {11, 0,   false},  // Occluded
        {12, 50,  true},   // Visible
        {13, 0,   false},  // Occluded
    };
    compactor.SubmitResults(instances, 4);

    auto result = compactor.Compact();
    EXPECT_EQ(result.visibleCount, 2u);
    EXPECT_EQ(result.occludedCount, 2u);
    EXPECT_EQ(result.visibleInstanceIds.size(), 2u);
    EXPECT_EQ(result.visibleInstanceIds[0], 10u);
    EXPECT_EQ(result.visibleInstanceIds[1], 12u);

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.occlusionRate, 50.0f);

    compactor.Shutdown();
}

TEST(OcclusionCompactor, ConservativeMode) {
    OcclusionCompactor compactor;
    OcclusionCompactorConfig config;
    config.conservativeMode = true;
    compactor.Init(nullptr, config);

    // UINT64_MAX signals query not ready
    OcclusionInstance instances[2] = {
        {0, UINT64_MAX, false}, // Not ready
        {1, 100,        true},  // Visible
    };
    compactor.SubmitResults(instances, 2);

    auto result = compactor.Compact();
    EXPECT_EQ(result.visibleCount, 2u); // Both visible (conservative)

    auto stats = compactor.GetStats();
    EXPECT_EQ(stats.queryNotReadyCount, 1u);

    compactor.Shutdown();
}

TEST(OcclusionCompactor, NonConservativeMode) {
    OcclusionCompactor compactor;
    OcclusionCompactorConfig config;
    config.conservativeMode = false;
    compactor.Init(nullptr, config);

    OcclusionInstance instances[2] = {
        {0, UINT64_MAX, false}, // Not ready → treat as occluded
        {1, 100,        true},
    };
    compactor.SubmitResults(instances, 2);

    auto result = compactor.Compact();
    EXPECT_EQ(result.visibleCount, 1u);
    EXPECT_EQ(result.occludedCount, 1u);

    compactor.Shutdown();
}

TEST(OcclusionCompactor, BuildIndirectArgs) {
    OcclusionCompactor compactor;
    compactor.Init(nullptr);

    OcclusionInstance instances[3] = {
        {0, 100, true},
        {1, 50,  true},
        {2, 200, true},
    };
    compactor.SubmitResults(instances, 3);

    auto result = compactor.Compact();
    compactor.BuildIndirectArgs(result, 36, 0, 0); // 36 indices per instance

    EXPECT_EQ(result.indirectIndexCount, 36u);
    EXPECT_EQ(result.indirectInstanceCount, 3u);
    EXPECT_EQ(result.indirectFirstIndex, 0u);
    EXPECT_EQ(result.indirectVertexOffset, 0);
    EXPECT_EQ(result.indirectFirstInstance, 0u);

    compactor.Shutdown();
}

TEST(OcclusionCompactor, ResetClearsState) {
    OcclusionCompactor compactor;
    compactor.Init(nullptr);

    OcclusionInstance inst = {0, 100, true};
    compactor.SubmitResults(&inst, 1);
    compactor.Compact();

    EXPECT_EQ(compactor.GetStats().visibleInstances, 1u);

    compactor.Reset();
    EXPECT_EQ(compactor.GetStats().totalInstances, 0u);
    EXPECT_EQ(compactor.GetStats().visibleInstances, 0u);

    compactor.Shutdown();
}

TEST(OcclusionCompactor, ThresholdFiltering) {
    OcclusionCompactor compactor;
    OcclusionCompactorConfig config;
    config.occlusionThreshold = 10; // Need >10 samples to be visible
    compactor.Init(nullptr, config);

    OcclusionInstance instances[3] = {
        {0, 5,   false}, // Below threshold
        {1, 10,  false}, // At threshold (not above)
        {2, 100, true},  // Above threshold
    };
    compactor.SubmitResults(instances, 3);

    auto result = compactor.Compact();
    EXPECT_EQ(result.visibleCount, 1u);
    EXPECT_EQ(result.visibleInstanceIds[0], 2u);

    compactor.Shutdown();
}
