#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_async_readback.h"

using namespace nge;
using namespace nge::rhi;

TEST(AsyncReadback, InitAndShutdown) {
    AsyncReadbackManager mgr;
    AsyncReadbackConfig config;
    config.stagingBufferSize = 1024 * 1024;
    EXPECT_TRUE(mgr.Init(config));

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.pendingRequests, 0u);
    EXPECT_EQ(stats.completedRequests, 0u);
    EXPECT_EQ(stats.stagingBufferTotal, 1024u * 1024u);

    mgr.Shutdown();
}

TEST(AsyncReadback, SubmitAndComplete) {
    AsyncReadbackManager mgr;
    mgr.Init();

    bool callbackFired = false;
    u64 receivedSize = 0;

    ReadbackRequest req;
    req.sourceResource = 100;
    req.offset = 0;
    req.size = 256;
    req.frameIssued = 0;
    req.framesToWait = 2;
    req.debugName = "TestReadback";
    req.callback = [&](const void* data, u64 size, bool success) {
        callbackFired = true;
        receivedSize = size;
    };

    u32 id = mgr.RequestReadback(req);
    EXPECT_NE(id, 0u);
    EXPECT_EQ(mgr.GetPendingCount(), 1u);

    mgr.Update(0);
    EXPECT_FALSE(callbackFired);

    mgr.Update(1);
    EXPECT_FALSE(callbackFired);

    mgr.Update(2);
    EXPECT_TRUE(callbackFired);
    EXPECT_EQ(receivedSize, 256u);
    EXPECT_EQ(mgr.GetPendingCount(), 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.completedRequests, 1u);
    EXPECT_EQ(stats.totalBytesReadback, 256u);

    mgr.Shutdown();
}

TEST(AsyncReadback, MultipleRequests) {
    AsyncReadbackManager mgr;
    mgr.Init();

    u32 completedCount = 0;

    for (u32 i = 0; i < 5; ++i) {
        ReadbackRequest req;
        req.sourceResource = i;
        req.offset = 0;
        req.size = 128;
        req.frameIssued = 0;
        req.framesToWait = 1;
        req.callback = [&](const void*, u64, bool) { completedCount++; };
        mgr.RequestReadback(req);
    }

    EXPECT_EQ(mgr.GetPendingCount(), 5u);

    mgr.Update(1);
    EXPECT_EQ(completedCount, 5u);
    EXPECT_EQ(mgr.GetPendingCount(), 0u);

    mgr.Shutdown();
}

TEST(AsyncReadback, CancelRequest) {
    AsyncReadbackManager mgr;
    mgr.Init();

    bool callbackFired = false;

    ReadbackRequest req;
    req.sourceResource = 1;
    req.offset = 0;
    req.size = 512;
    req.frameIssued = 0;
    req.framesToWait = 3;
    req.callback = [&](const void*, u64, bool) { callbackFired = true; };

    u32 id = mgr.RequestReadback(req);
    EXPECT_EQ(mgr.GetPendingCount(), 1u);

    mgr.CancelRequest(id);
    EXPECT_EQ(mgr.GetPendingCount(), 0u);

    mgr.Update(5);
    EXPECT_FALSE(callbackFired);

    mgr.Shutdown();
}

TEST(AsyncReadback, StagingBufferFull) {
    AsyncReadbackManager mgr;
    AsyncReadbackConfig config;
    config.stagingBufferSize = 256;
    config.maxPendingRequests = 100;
    mgr.Init(config);

    bool secondFailed = false;

    ReadbackRequest req;
    req.sourceResource = 1;
    req.offset = 0;
    req.size = 256;
    req.frameIssued = 0;
    req.framesToWait = 5;
    req.callback = [](const void*, u64, bool) {};

    u32 id1 = mgr.RequestReadback(req);
    EXPECT_NE(id1, 0u);

    req.callback = [&](const void*, u64, bool success) { secondFailed = !success; };
    u32 id2 = mgr.RequestReadback(req);
    EXPECT_EQ(id2, 0u);
    EXPECT_TRUE(secondFailed);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.failedRequests, 1u);

    mgr.Shutdown();
}

TEST(AsyncReadback, MaxPendingLimit) {
    AsyncReadbackManager mgr;
    AsyncReadbackConfig config;
    config.maxPendingRequests = 2;
    config.stagingBufferSize = 16 * 1024 * 1024;
    mgr.Init(config);

    ReadbackRequest req;
    req.sourceResource = 1;
    req.offset = 0;
    req.size = 64;
    req.frameIssued = 0;
    req.framesToWait = 10;
    req.callback = [](const void*, u64, bool) {};

    EXPECT_NE(mgr.RequestReadback(req), 0u);
    EXPECT_NE(mgr.RequestReadback(req), 0u);

    bool failed = false;
    req.callback = [&](const void*, u64, bool success) { failed = !success; };
    EXPECT_EQ(mgr.RequestReadback(req), 0u);
    EXPECT_TRUE(failed);

    mgr.Shutdown();
}

TEST(AsyncReadback, FlushAll) {
    AsyncReadbackManager mgr;
    mgr.Init();

    u32 completed = 0;

    for (u32 i = 0; i < 3; ++i) {
        ReadbackRequest req;
        req.sourceResource = i;
        req.offset = 0;
        req.size = 128;
        req.frameIssued = 0;
        req.framesToWait = 100;
        req.callback = [&](const void*, u64, bool) { completed++; };
        mgr.RequestReadback(req);
    }

    mgr.FlushAll(5);
    EXPECT_EQ(completed, 3u);
    EXPECT_EQ(mgr.GetPendingCount(), 0u);

    mgr.Shutdown();
}

TEST(AsyncReadback, RequestStatusTracking) {
    AsyncReadbackManager mgr;
    mgr.Init();

    ReadbackRequest req;
    req.sourceResource = 1;
    req.offset = 0;
    req.size = 64;
    req.frameIssued = 0;
    req.framesToWait = 2;
    req.callback = [](const void*, u64, bool) {};

    u32 id = mgr.RequestReadback(req);

    EXPECT_EQ(mgr.GetRequestStatus(id), ReadbackStatus::Pending);

    mgr.Update(2);

    // After delivery it's removed, returns Delivered for unknown IDs
    EXPECT_EQ(mgr.GetRequestStatus(id), ReadbackStatus::Delivered);

    mgr.Shutdown();
}

TEST(AsyncReadback, UtilizationStats) {
    AsyncReadbackManager mgr;
    AsyncReadbackConfig config;
    config.stagingBufferSize = 1024;
    mgr.Init(config);

    ReadbackRequest req;
    req.sourceResource = 1;
    req.offset = 0;
    req.size = 512;
    req.frameIssued = 0;
    req.framesToWait = 5;
    req.callback = [](const void*, u64, bool) {};

    mgr.RequestReadback(req);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.stagingBufferUsed, 512u);
    EXPECT_NEAR(stats.utilizationPercent, 50.0f, 0.1f);

    mgr.Shutdown();
}
