#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_vt_feedback_analyzer.h"

using namespace nge;
using namespace nge::rhi;

static VTPageId MakePage(u32 tex, u32 mip, u32 x, u32 y) {
    return VTPageId{tex, mip, x, y};
}

TEST(VTFeedbackAnalyzer, InitAndShutdown) {
    VTFeedbackAnalyzer analyzer;
    EXPECT_TRUE(analyzer.Init());

    EXPECT_EQ(analyzer.GetTrackedPageCount(), 0u);
    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.totalPagesTracked, 0u);
    EXPECT_EQ(stats.totalRequestsProcessed, 0u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, RequestPage) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto page = MakePage(0, 2, 4, 5);
    analyzer.RequestPage(page, 0.1f, 0);

    EXPECT_EQ(analyzer.GetTrackedPageCount(), 1u);
    EXPECT_TRUE(analyzer.IsRequested(page));
    EXPECT_EQ(analyzer.GetResidency(page), PageResidency::NotResident);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, DuplicateRequestsMerge) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto page = MakePage(0, 1, 3, 3);
    analyzer.RequestPage(page, 0.05f, 0);
    analyzer.RequestPage(page, 0.1f, 1);
    analyzer.RequestPage(page, 0.02f, 2);

    EXPECT_EQ(analyzer.GetTrackedPageCount(), 1u); // Still 1 unique page

    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.duplicatesRemoved, 2u);
    EXPECT_EQ(stats.totalRequestsProcessed, 3u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, SubmitFeedbackBatch) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    VTPageId pages[3] = {
        MakePage(0, 0, 0, 0),
        MakePage(0, 0, 1, 0),
        MakePage(0, 1, 0, 0),
    };

    analyzer.SubmitFeedback(pages, 3, 0);

    EXPECT_EQ(analyzer.GetTrackedPageCount(), 3u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, SubmitFeedbackDeduplicates) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    VTPageId pages[4] = {
        MakePage(0, 0, 0, 0),
        MakePage(0, 0, 0, 0), // Duplicate
        MakePage(0, 0, 1, 0),
        MakePage(0, 0, 0, 0), // Duplicate
    };

    analyzer.SubmitFeedback(pages, 4, 0);

    EXPECT_EQ(analyzer.GetTrackedPageCount(), 2u); // Only 2 unique
    EXPECT_EQ(analyzer.GetStats().duplicatesRemoved, 2u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, PriorityQueueSorted) {
    VTFeedbackAnalyzer analyzer;
    VTFeedbackConfig config;
    config.mipUrgencyWeight = 2.0f;
    config.frequencyWeight = 1.0f;
    config.coverageWeight = 1.5f;
    analyzer.Init(config);

    // Lower mip = higher priority
    analyzer.RequestPage(MakePage(0, 5, 0, 0), 0.01f, 0); // Low priority (high mip)
    analyzer.RequestPage(MakePage(0, 0, 0, 0), 0.1f, 0);  // High priority (mip 0, large coverage)
    analyzer.RequestPage(MakePage(0, 2, 0, 0), 0.05f, 0);

    auto queue = analyzer.GetPriorityQueue();
    EXPECT_EQ(queue.size(), 3u);

    // First should be mip 0 (highest priority)
    EXPECT_EQ(queue[0].page.mipLevel, 0u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, PriorityQueueExcludesResident) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto page1 = MakePage(0, 0, 0, 0);
    auto page2 = MakePage(0, 0, 1, 0);

    analyzer.RequestPage(page1, 0.1f, 0);
    analyzer.RequestPage(page2, 0.1f, 0);

    analyzer.MarkResident(page1);

    auto queue = analyzer.GetPriorityQueue();
    EXPECT_EQ(queue.size(), 1u); // Only page2 (page1 is resident)
    EXPECT_EQ(queue[0].page.pageX, 1u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, PriorityQueueExcludesLoading) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto page = MakePage(0, 0, 0, 0);
    analyzer.RequestPage(page, 0.1f, 0);

    analyzer.MarkLoading(page);

    auto queue = analyzer.GetPriorityQueue();
    EXPECT_TRUE(queue.empty()); // Loading pages excluded

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, PriorityQueueMaxCount) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    for (u32 i = 0; i < 10; ++i) {
        analyzer.RequestPage(MakePage(0, 0, i, 0), 0.01f, 0);
    }

    auto queue = analyzer.GetPriorityQueue(3);
    EXPECT_EQ(queue.size(), 3u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, MarkResident) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto page = MakePage(0, 0, 0, 0);
    analyzer.RequestPage(page, 0.1f, 0);

    EXPECT_EQ(analyzer.GetResidency(page), PageResidency::NotResident);

    analyzer.MarkResident(page);
    EXPECT_EQ(analyzer.GetResidency(page), PageResidency::Resident);
    EXPECT_FALSE(analyzer.IsRequested(page)); // Resident -> not "requested"

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, MarkLoading) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto page = MakePage(0, 0, 0, 0);
    analyzer.MarkLoading(page);

    EXPECT_EQ(analyzer.GetResidency(page), PageResidency::Loading);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, ProcessFrameEvictionCandidates) {
    VTFeedbackAnalyzer analyzer;
    VTFeedbackConfig config;
    config.evictionFrameThreshold = 10;
    analyzer.Init(config);

    auto page = MakePage(0, 0, 0, 0);
    analyzer.RequestPage(page, 0.1f, 0);
    analyzer.MarkResident(page);

    // After 11 frames without request, should become eviction candidate
    analyzer.ProcessFrame(11);

    EXPECT_EQ(analyzer.GetResidency(page), PageResidency::EvictionCandidate);

    auto candidates = analyzer.GetEvictionCandidates();
    EXPECT_EQ(candidates.size(), 1u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, ProcessFrameRecentNotEvicted) {
    VTFeedbackAnalyzer analyzer;
    VTFeedbackConfig config;
    config.evictionFrameThreshold = 10;
    analyzer.Init(config);

    auto page = MakePage(0, 0, 0, 0);
    analyzer.RequestPage(page, 0.1f, 5); // Requested at frame 5
    analyzer.MarkResident(page);

    analyzer.ProcessFrame(10); // Only 5 frames since request (< threshold)

    EXPECT_EQ(analyzer.GetResidency(page), PageResidency::Resident); // Still resident

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, Evict) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto page = MakePage(0, 0, 0, 0);
    analyzer.MarkResident(page);

    analyzer.Evict(page);
    EXPECT_EQ(analyzer.GetResidency(page), PageResidency::NotResident);
    EXPECT_EQ(analyzer.GetStats().pagesEvicted, 1u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, GetEvictionCandidatesMaxCount) {
    VTFeedbackAnalyzer analyzer;
    VTFeedbackConfig config;
    config.evictionFrameThreshold = 5;
    analyzer.Init(config);

    for (u32 i = 0; i < 10; ++i) {
        auto page = MakePage(0, 0, i, 0);
        analyzer.RequestPage(page, 0.01f, 0);
        analyzer.MarkResident(page);
    }

    analyzer.ProcessFrame(100);

    auto candidates = analyzer.GetEvictionCandidates(3);
    EXPECT_EQ(candidates.size(), 3u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, MaxPagesLimit) {
    VTFeedbackAnalyzer analyzer;
    VTFeedbackConfig config;
    config.maxPagesTracked = 5;
    analyzer.Init(config);

    for (u32 i = 0; i < 10; ++i) {
        analyzer.RequestPage(MakePage(0, 0, i, 0), 0.01f, 0);
    }

    EXPECT_EQ(analyzer.GetTrackedPageCount(), 5u); // Capped

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, StatsTracking) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    auto p1 = MakePage(0, 0, 0, 0);
    auto p2 = MakePage(0, 0, 1, 0);
    auto p3 = MakePage(0, 1, 0, 0);

    analyzer.RequestPage(p1, 0.1f, 0);
    analyzer.RequestPage(p2, 0.05f, 0);
    analyzer.RequestPage(p3, 0.02f, 0);

    analyzer.MarkResident(p1);
    analyzer.MarkLoading(p2);

    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.totalPagesTracked, 3u);
    EXPECT_EQ(stats.residentPages, 1u);
    EXPECT_EQ(stats.loadingPages, 1u);
    EXPECT_EQ(stats.pendingRequests, 1u); // p3 is pending

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, ResetClearsAll) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    analyzer.RequestPage(MakePage(0, 0, 0, 0), 0.1f, 0);
    analyzer.MarkResident(MakePage(0, 0, 0, 0));

    analyzer.Reset();

    EXPECT_EQ(analyzer.GetTrackedPageCount(), 0u);
    auto stats = analyzer.GetStats();
    EXPECT_EQ(stats.totalRequestsProcessed, 0u);
    EXPECT_EQ(stats.residentPages, 0u);
    EXPECT_EQ(stats.pagesEvicted, 0u);

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, IsRequestedFalseForUnknown) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    EXPECT_FALSE(analyzer.IsRequested(MakePage(99, 99, 99, 99)));

    analyzer.Shutdown();
}

TEST(VTFeedbackAnalyzer, GetResidencyNotResident) {
    VTFeedbackAnalyzer analyzer;
    analyzer.Init();

    EXPECT_EQ(analyzer.GetResidency(MakePage(0, 0, 0, 0)), PageResidency::NotResident);

    analyzer.Shutdown();
}
