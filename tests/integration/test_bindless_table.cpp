#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_bindless.h"

using namespace nge;
using namespace nge::rhi;

class BindlessTableTest : public ::testing::Test {
protected:
    void SetUp() override {
        BindlessTableConfig config;
        config.maxTextures2D = 64;
        config.maxTexturesCube = 16;
        config.maxTextures3D = 8;
        config.maxBuffers = 32;
        config.maxSamplers = 8;
        config.maxStorageImages = 16;
        config.releaseLatency = 2;
        // Init with nullptr device (stub mode)
        m_table.Init(nullptr, config);
    }

    void TearDown() override {
        m_table.Shutdown();
    }

    BindlessTable m_table;
};

TEST_F(BindlessTableTest, InitialState) {
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 0u);
    EXPECT_EQ(m_table.GetCapacity(BindlessType::Texture2D), 64u);
    EXPECT_EQ(m_table.GetCapacity(BindlessType::Buffer), 32u);
    EXPECT_FLOAT_EQ(m_table.GetOccupancy(BindlessType::Texture2D), 0.0f);
}

TEST_F(BindlessTableTest, AllocateTexture2D) {
    TextureHandle tex{};
    BindlessIndex idx = m_table.AllocateTexture2D(tex);
    EXPECT_NE(idx, INVALID_BINDLESS);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 1u);
}

TEST_F(BindlessTableTest, AllocateMultiple) {
    TextureHandle tex{};
    BindlessIndex idx0 = m_table.AllocateTexture2D(tex);
    BindlessIndex idx1 = m_table.AllocateTexture2D(tex);
    BindlessIndex idx2 = m_table.AllocateTexture2D(tex);

    EXPECT_NE(idx0, idx1);
    EXPECT_NE(idx1, idx2);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 3u);
}

TEST_F(BindlessTableTest, AllocateBuffer) {
    BufferHandle buf{};
    BindlessIndex idx = m_table.AllocateBuffer(buf);
    EXPECT_NE(idx, INVALID_BINDLESS);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Buffer), 1u);
}

TEST_F(BindlessTableTest, AllocateStorageImage) {
    TextureHandle tex{};
    BindlessIndex idx = m_table.AllocateStorageImage(tex);
    EXPECT_NE(idx, INVALID_BINDLESS);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::StorageImage), 1u);
}

TEST_F(BindlessTableTest, ReleaseDeferred) {
    TextureHandle tex{};
    BindlessIndex idx = m_table.AllocateTexture2D(tex);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 1u);

    // Release at frame 0
    m_table.BeginFrame(0);
    m_table.Release(BindlessType::Texture2D, idx);

    // Still allocated (deferred by 2 frames)
    m_table.BeginFrame(1);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 1u);

    // After latency, slot should be freed
    m_table.BeginFrame(2);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 0u);
}

TEST_F(BindlessTableTest, ReuseReleasedSlot) {
    TextureHandle tex{};

    // Allocate and release
    BindlessIndex idx0 = m_table.AllocateTexture2D(tex);
    m_table.BeginFrame(0);
    m_table.Release(BindlessType::Texture2D, idx0);

    // Advance past release latency
    m_table.BeginFrame(1);
    m_table.BeginFrame(2);

    // Should be able to allocate again (reuses freed slot)
    BindlessIndex idx1 = m_table.AllocateTexture2D(tex);
    EXPECT_NE(idx1, INVALID_BINDLESS);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 1u);
}

TEST_F(BindlessTableTest, ExhaustCapacity) {
    TextureHandle tex{};
    // Exhaust all 8 Texture3D slots
    for (u32 i = 0; i < 8; ++i) {
        BindlessIndex idx = m_table.AllocateTexture3D(tex);
        EXPECT_NE(idx, INVALID_BINDLESS);
    }
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture3D), 8u);

    // Next allocation should fail
    BindlessIndex overflow = m_table.AllocateTexture3D(tex);
    EXPECT_EQ(overflow, INVALID_BINDLESS);
}

TEST_F(BindlessTableTest, Occupancy) {
    TextureHandle tex{};
    // Allocate half of Texture2D slots (32 of 64)
    for (u32 i = 0; i < 32; ++i) {
        m_table.AllocateTexture2D(tex);
    }
    EXPECT_FLOAT_EQ(m_table.GetOccupancy(BindlessType::Texture2D), 0.5f);
}

TEST_F(BindlessTableTest, IndependentTypes) {
    TextureHandle tex{};
    BufferHandle buf{};

    m_table.AllocateTexture2D(tex);
    m_table.AllocateTexture2D(tex);
    m_table.AllocateBuffer(buf);

    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Texture2D), 2u);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::Buffer), 1u);
    EXPECT_EQ(m_table.GetAllocatedCount(BindlessType::TextureCube), 0u);
}

TEST_F(BindlessTableTest, InvalidBindlessConstant) {
    EXPECT_EQ(INVALID_BINDLESS, UINT32_MAX);
}
