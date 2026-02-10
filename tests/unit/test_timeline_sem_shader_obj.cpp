#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_timeline_semaphore_pool.h"
#include "engine/rhi/vulkan/vk_shader_object.h"

using namespace nge;
using namespace nge::rhi;
using namespace nge::rhi::vulkan;

// ─── Timeline Semaphore Pool Tests ───────────────────────────────────────

TEST(TimelineSemaphorePool, InitCreatesPool) {
    TimelineSemaphorePool pool;
    TimelineSemaphorePoolConfig config;
    config.initialPoolSize = 8;
    config.maxPoolSize = 32;
    EXPECT_TRUE(pool.Init(nullptr, config));

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.totalSemaphores, 8u);
    EXPECT_EQ(stats.inUseSemaphores, 0u);
    EXPECT_EQ(stats.availableSemaphores, 8u);

    pool.Shutdown();
}

TEST(TimelineSemaphorePool, AcquireAndRelease) {
    TimelineSemaphorePool pool;
    TimelineSemaphorePoolConfig config;
    config.initialPoolSize = 4;
    pool.Init(nullptr, config);

    u32 id0 = pool.Acquire("GraphicsQueue");
    u32 id1 = pool.Acquire("ComputeQueue");
    EXPECT_NE(id0, UINT32_MAX);
    EXPECT_NE(id1, UINT32_MAX);
    EXPECT_NE(id0, id1);

    auto stats = pool.GetStats();
    EXPECT_EQ(stats.inUseSemaphores, 2u);

    pool.Release(id0);
    stats = pool.GetStats();
    EXPECT_EQ(stats.inUseSemaphores, 1u);

    pool.Release(id1);
    stats = pool.GetStats();
    EXPECT_EQ(stats.inUseSemaphores, 0u);

    pool.Shutdown();
}

TEST(TimelineSemaphorePool, GrowthWhenExhausted) {
    TimelineSemaphorePool pool;
    TimelineSemaphorePoolConfig config;
    config.initialPoolSize = 2;
    config.maxPoolSize = 32;
    config.allowGrowth = true;
    pool.Init(nullptr, config);

    pool.Acquire("A");
    pool.Acquire("B");
    EXPECT_EQ(pool.GetStats().availableSemaphores, 0u);

    // Should trigger growth
    u32 id = pool.Acquire("C");
    EXPECT_NE(id, UINT32_MAX);
    EXPECT_EQ(pool.GetStats().growthEvents, 1u);
    EXPECT_GT(pool.GetStats().totalSemaphores, 2u);

    pool.Shutdown();
}

TEST(TimelineSemaphorePool, NoGrowthReturnsInvalid) {
    TimelineSemaphorePool pool;
    TimelineSemaphorePoolConfig config;
    config.initialPoolSize = 1;
    config.maxPoolSize = 1;
    config.allowGrowth = false;
    pool.Init(nullptr, config);

    pool.Acquire("Only");
    u32 overflow = pool.Acquire("Overflow");
    EXPECT_EQ(overflow, UINT32_MAX);

    pool.Shutdown();
}

TEST(TimelineSemaphorePool, SignalValueIncrement) {
    TimelineSemaphorePool pool;
    pool.Init(nullptr);

    u32 id = pool.Acquire("Test");
    EXPECT_EQ(pool.GetCurrentValue(id), 0u);

    u64 v1 = pool.GetNextSignalValue(id);
    EXPECT_EQ(v1, 1u);
    EXPECT_EQ(pool.GetCurrentValue(id), 1u);

    u64 v2 = pool.GetNextSignalValue(id);
    EXPECT_EQ(v2, 2u);

    pool.Shutdown();
}

TEST(TimelineSemaphorePool, HasReachedCheck) {
    TimelineSemaphorePool pool;
    pool.Init(nullptr);

    u32 id = pool.Acquire("Test");
    EXPECT_TRUE(pool.HasReached(id, 0));
    EXPECT_FALSE(pool.HasReached(id, 1));

    pool.CpuSignal(id, 5);
    EXPECT_TRUE(pool.HasReached(id, 3));
    EXPECT_TRUE(pool.HasReached(id, 5));
    EXPECT_FALSE(pool.HasReached(id, 6));

    pool.Shutdown();
}

TEST(TimelineSemaphorePool, ResetAllReclaims) {
    TimelineSemaphorePool pool;
    TimelineSemaphorePoolConfig config;
    config.initialPoolSize = 4;
    pool.Init(nullptr, config);

    pool.Acquire("A");
    pool.Acquire("B");
    pool.Acquire("C");
    EXPECT_EQ(pool.GetStats().inUseSemaphores, 3u);

    pool.ResetAll();
    EXPECT_EQ(pool.GetStats().inUseSemaphores, 0u);
    EXPECT_EQ(pool.GetStats().availableSemaphores, 4u);

    pool.Shutdown();
}

TEST(TimelineSemaphorePool, GetHandle) {
    TimelineSemaphorePool pool;
    pool.Init(nullptr);

    u32 id = pool.Acquire("Test");
    u64 handle = pool.GetHandle(id);
    EXPECT_NE(handle, 0u);

    // Invalid ID
    EXPECT_EQ(pool.GetHandle(9999), 0u);

    pool.Shutdown();
}

// ─── Shader Object Manager Tests ─────────────────────────────────────────

TEST(ShaderObjectManager, InitAndShutdown) {
    ShaderObjectManager mgr;
    ShaderObjectConfig config;
    config.enableBinaryCache = false;
    EXPECT_TRUE(mgr.Init(nullptr, config));

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalObjects, 0u);
    EXPECT_EQ(stats.compilations, 0u);

    mgr.Shutdown();
}

TEST(ShaderObjectManager, CreateShaderObject) {
    ShaderObjectManager mgr;
    ShaderObjectConfig config;
    config.enableBinaryCache = false;
    mgr.Init(nullptr, config);

    ShaderObjectDesc desc;
    desc.stage = ShaderObjectStage::Vertex;
    desc.entryPoint = "main";
    desc.debugName = "BasicVS";
    desc.spirvCode = {0x03, 0x02, 0x23, 0x07}; // SPIR-V magic header

    u64 handle = mgr.CreateShaderObject(desc);
    EXPECT_NE(handle, 0u);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalObjects, 1u);
    EXPECT_EQ(stats.vertexShaders, 1u);
    EXPECT_EQ(stats.compilations, 1u);

    mgr.Shutdown();
}

TEST(ShaderObjectManager, DeduplicationBySPIRVHash) {
    ShaderObjectManager mgr;
    ShaderObjectConfig config;
    config.enableBinaryCache = false;
    mgr.Init(nullptr, config);

    ShaderObjectDesc desc;
    desc.stage = ShaderObjectStage::Fragment;
    desc.debugName = "BasicFS";
    desc.spirvCode = {0x03, 0x02, 0x23, 0x07, 0xAA, 0xBB};

    u64 h1 = mgr.CreateShaderObject(desc);
    u64 h2 = mgr.CreateShaderObject(desc); // Same SPIR-V

    EXPECT_EQ(h1, h2); // Should deduplicate
    EXPECT_EQ(mgr.GetStats().compilations, 1u);
    EXPECT_EQ(mgr.GetStats().cacheHits, 1u);

    mgr.Shutdown();
}

TEST(ShaderObjectManager, FindByName) {
    ShaderObjectManager mgr;
    ShaderObjectConfig config;
    config.enableBinaryCache = false;
    mgr.Init(nullptr, config);

    ShaderObjectDesc desc;
    desc.stage = ShaderObjectStage::Compute;
    desc.debugName = "ParticleSimCS";
    desc.spirvCode = {0x01, 0x02, 0x03};

    u64 handle = mgr.CreateShaderObject(desc);
    u64 found = mgr.FindByName("ParticleSimCS");
    EXPECT_EQ(handle, found);

    u64 notFound = mgr.FindByName("NonExistent");
    EXPECT_EQ(notFound, 0u);

    mgr.Shutdown();
}

TEST(ShaderObjectManager, DestroyReducesCount) {
    ShaderObjectManager mgr;
    ShaderObjectConfig config;
    config.enableBinaryCache = false;
    mgr.Init(nullptr, config);

    ShaderObjectDesc desc;
    desc.stage = ShaderObjectStage::Vertex;
    desc.debugName = "TempVS";
    desc.spirvCode = {0xDE, 0xAD};

    u64 handle = mgr.CreateShaderObject(desc);
    EXPECT_EQ(mgr.GetStats().totalObjects, 1u);

    mgr.Destroy(handle);
    EXPECT_EQ(mgr.GetStats().totalObjects, 0u);

    // Name lookup should fail after destroy
    EXPECT_EQ(mgr.FindByName("TempVS"), 0u);

    mgr.Shutdown();
}

TEST(ShaderObjectManager, MultipleStages) {
    ShaderObjectManager mgr;
    ShaderObjectConfig config;
    config.enableBinaryCache = false;
    mgr.Init(nullptr, config);

    ShaderObjectDesc vs;
    vs.stage = ShaderObjectStage::Vertex;
    vs.debugName = "VS";
    vs.spirvCode = {0x01};

    ShaderObjectDesc fs;
    fs.stage = ShaderObjectStage::Fragment;
    fs.debugName = "FS";
    fs.spirvCode = {0x02};

    ShaderObjectDesc cs;
    cs.stage = ShaderObjectStage::Compute;
    cs.debugName = "CS";
    cs.spirvCode = {0x03};

    ShaderObjectDesc ms;
    ms.stage = ShaderObjectStage::Mesh;
    ms.debugName = "MS";
    ms.spirvCode = {0x04};

    mgr.CreateShaderObject(vs);
    mgr.CreateShaderObject(fs);
    mgr.CreateShaderObject(cs);
    mgr.CreateShaderObject(ms);

    auto stats = mgr.GetStats();
    EXPECT_EQ(stats.totalObjects, 4u);
    EXPECT_EQ(stats.vertexShaders, 1u);
    EXPECT_EQ(stats.fragmentShaders, 1u);
    EXPECT_EQ(stats.computeShaders, 1u);
    EXPECT_EQ(stats.meshShaders, 1u);

    mgr.Shutdown();
}
