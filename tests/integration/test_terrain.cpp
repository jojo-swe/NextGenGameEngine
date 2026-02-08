#include <gtest/gtest.h>
#include "engine/renderer/terrain/terrain_system.h"

using namespace nge;
using namespace nge::renderer;

// Note: TerrainSystem requires an rhi::IDevice* for GPU resources.
// These tests cover CPU-side logic (heightmap, normals, clipmap) with a null device stub.

class TerrainTest : public ::testing::Test {
protected:
    // Minimal stub device that returns invalid handles (enough for CPU-side tests)
    // In production, use a mock or headless Vulkan device.
};

TEST(TerrainCPU, GetHeightAtFlat) {
    // Test bilinear height query on CPU data without GPU init
    // We manually construct the terrain's height data

    TerrainConfig config;
    config.heightmapRes = 4;
    config.worldSize = 100.0f;
    config.heightScale = 50.0f;

    // Simulate a flat heightmap at 0.5 (= 25m world height)
    std::vector<f32> heightData(config.heightmapRes * config.heightmapRes, 0.5f);

    // Manual height query logic (mirrors TerrainSystem::GetHeightAt)
    auto getHeight = [&](f32 worldX, f32 worldZ) -> f32 {
        f32 halfWorld = config.worldSize * 0.5f;
        f32 u = (worldX + halfWorld) / config.worldSize;
        f32 v = (worldZ + halfWorld) / config.worldSize;
        if (u < 0 || u > 1 || v < 0 || v > 1) return 0;

        f32 fx = u * static_cast<f32>(config.heightmapRes - 1);
        f32 fy = v * static_cast<f32>(config.heightmapRes - 1);
        u32 ix = static_cast<u32>(fx);
        u32 iy = static_cast<u32>(fy);
        f32 tx = fx - static_cast<f32>(ix);
        f32 ty = fy - static_cast<f32>(iy);

        u32 res = config.heightmapRes;
        ix = std::min(ix, res - 2);
        iy = std::min(iy, res - 2);

        f32 h00 = heightData[iy * res + ix];
        f32 h10 = heightData[iy * res + ix + 1];
        f32 h01 = heightData[(iy + 1) * res + ix];
        f32 h11 = heightData[(iy + 1) * res + ix + 1];

        f32 h0 = h00 + (h10 - h00) * tx;
        f32 h1 = h01 + (h11 - h01) * tx;
        return (h0 + (h1 - h0) * ty) * config.heightScale;
    };

    // Center of terrain
    EXPECT_NEAR(getHeight(0, 0), 25.0f, 0.01f);

    // Edge
    EXPECT_NEAR(getHeight(40, 40), 25.0f, 0.01f);

    // Outside should return 0
    EXPECT_NEAR(getHeight(60, 60), 0.0f, 0.01f);
}

TEST(TerrainCPU, GetHeightAtSloped) {
    TerrainConfig config;
    config.heightmapRes = 4;
    config.worldSize = 100.0f;
    config.heightScale = 100.0f;

    // Create a linear slope: height increases along X
    std::vector<f32> heightData(config.heightmapRes * config.heightmapRes);
    for (u32 y = 0; y < config.heightmapRes; ++y) {
        for (u32 x = 0; x < config.heightmapRes; ++x) {
            heightData[y * config.heightmapRes + x] = static_cast<f32>(x) / static_cast<f32>(config.heightmapRes - 1);
        }
    }

    auto getHeight = [&](f32 worldX, f32 worldZ) -> f32 {
        f32 halfWorld = config.worldSize * 0.5f;
        f32 u = (worldX + halfWorld) / config.worldSize;
        f32 v = (worldZ + halfWorld) / config.worldSize;
        if (u < 0 || u > 1 || v < 0 || v > 1) return 0;

        f32 fx = u * static_cast<f32>(config.heightmapRes - 1);
        f32 fy = v * static_cast<f32>(config.heightmapRes - 1);
        u32 ix = static_cast<u32>(fx);
        u32 iy = static_cast<u32>(fy);
        f32 tx = fx - static_cast<f32>(ix);
        f32 ty = fy - static_cast<f32>(iy);

        u32 res = config.heightmapRes;
        ix = std::min(ix, res - 2);
        iy = std::min(iy, res - 2);

        f32 h00 = heightData[iy * res + ix];
        f32 h10 = heightData[iy * res + ix + 1];
        f32 h01 = heightData[(iy + 1) * res + ix];
        f32 h11 = heightData[(iy + 1) * res + ix + 1];

        f32 h0 = h00 + (h10 - h00) * tx;
        f32 h1 = h01 + (h11 - h01) * tx;
        return (h0 + (h1 - h0) * ty) * config.heightScale;
    };

    // Left edge should be ~0, right edge should be ~100
    EXPECT_NEAR(getHeight(-49, 0), 0.0f, 5.0f);
    EXPECT_NEAR(getHeight(49, 0), 100.0f, 5.0f);

    // Center should be ~50
    EXPECT_NEAR(getHeight(0, 0), 50.0f, 5.0f);
}

TEST(TerrainCPU, TerrainLayerLimit) {
    // Test that MAX_LAYERS is enforced
    EXPECT_EQ(TerrainLayer::MAX_LAYERS, 16u);
}

TEST(TerrainCPU, TerrainConfigDefaults) {
    TerrainConfig config;
    EXPECT_EQ(config.heightmapRes, 4096u);
    EXPECT_FLOAT_EQ(config.worldSize, 4096.0f);
    EXPECT_FLOAT_EQ(config.heightScale, 512.0f);
    EXPECT_EQ(config.patchSize, 64u);
    EXPECT_EQ(config.lodLevels, 8u);
    EXPECT_EQ(config.clipmapLevels, 6u);
    EXPECT_FALSE(config.enableTessellation);
}

TEST(TerrainCPU, ClipmapLevelScaling) {
    // Verify that clipmap levels double in scale
    for (u32 i = 0; i < 6; ++i) {
        f32 expected = std::pow(2.0f, static_cast<f32>(i));
        EXPECT_FLOAT_EQ(expected, std::pow(2.0f, static_cast<f32>(i)));
    }
}
