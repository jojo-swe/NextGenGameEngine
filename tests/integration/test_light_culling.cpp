#include <gtest/gtest.h>
#include "engine/renderer/lighting/light_culling.h"

using namespace nge;
using namespace nge::renderer;

class LightCullingTest : public ::testing::Test {
protected:
    void SetUp() override {
        ClusterGridConfig config;
        config.tilesX = 4;
        config.tilesY = 4;
        config.slices = 8;
        config.maxLightsPerCluster = 32;
        m_system.Init(nullptr, config);
    }

    void TearDown() override {
        m_system.Shutdown();
    }

    LightCullingSystem m_system;
};

TEST_F(LightCullingTest, InitialState) {
    EXPECT_EQ(m_system.GetLightCount(), 0u);
    EXPECT_EQ(m_system.GetPointLightCount(), 0u);
    EXPECT_EQ(m_system.GetSpotLightCount(), 0u);
    EXPECT_FALSE(m_system.HasDirectionalLight());
    EXPECT_EQ(m_system.GetTotalClusters(), 4u * 4u * 8u);
}

TEST_F(LightCullingTest, AddPointLight) {
    m_system.BeginFrame();

    LightInfo light;
    light.type = LightType::Point;
    light.position = {0, 5, 0};
    light.color = {1, 1, 1};
    light.intensity = 100.0f;
    light.range = 20.0f;

    u32 idx = m_system.AddLight(light);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(m_system.GetLightCount(), 1u);
    EXPECT_EQ(m_system.GetPointLightCount(), 1u);
}

TEST_F(LightCullingTest, AddSpotLight) {
    m_system.BeginFrame();

    LightInfo light;
    light.type = LightType::Spot;
    light.position = {0, 10, 0};
    light.direction = {0, -1, 0};
    light.color = {1, 0.9f, 0.8f};
    light.intensity = 500.0f;
    light.range = 30.0f;
    light.innerAngle = 0.3f;
    light.outerAngle = 0.6f;

    u32 idx = m_system.AddLight(light);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(m_system.GetSpotLightCount(), 1u);
}

TEST_F(LightCullingTest, SetDirectionalLight) {
    m_system.BeginFrame();

    LightInfo sun;
    sun.type = LightType::Directional;
    sun.direction = {-0.5f, -1.0f, -0.3f};
    sun.color = {1, 0.95f, 0.9f};
    sun.intensity = 10.0f;

    m_system.SetDirectionalLight(sun);
    EXPECT_TRUE(m_system.HasDirectionalLight());

    const auto& dirLight = m_system.GetDirectionalLight();
    EXPECT_FLOAT_EQ(dirLight.colorAndIntensity.w, 10.0f);
}

TEST_F(LightCullingTest, MultipleLights) {
    m_system.BeginFrame();

    LightInfo point;
    point.type = LightType::Point;
    point.position = {0, 5, 0};
    point.color = {1, 1, 1};
    point.intensity = 50.0f;
    point.range = 10.0f;

    for (u32 i = 0; i < 10; ++i) {
        point.position.x = static_cast<f32>(i) * 5.0f;
        m_system.AddLight(point);
    }

    EXPECT_EQ(m_system.GetLightCount(), 10u);
    EXPECT_EQ(m_system.GetPointLightCount(), 10u);
}

TEST_F(LightCullingTest, DisabledLightNotAdded) {
    m_system.BeginFrame();

    LightInfo light;
    light.type = LightType::Point;
    light.enabled = false;

    u32 idx = m_system.AddLight(light);
    EXPECT_EQ(idx, UINT32_MAX);
    EXPECT_EQ(m_system.GetLightCount(), 0u);
}

TEST_F(LightCullingTest, BeginFrameResetsLights) {
    m_system.BeginFrame();

    LightInfo light;
    light.type = LightType::Point;
    light.color = {1, 1, 1};
    light.intensity = 50.0f;
    light.range = 10.0f;
    m_system.AddLight(light);
    m_system.AddLight(light);
    EXPECT_EQ(m_system.GetLightCount(), 2u);

    m_system.BeginFrame(); // Reset
    EXPECT_EQ(m_system.GetLightCount(), 0u);
    EXPECT_EQ(m_system.GetPointLightCount(), 0u);
    EXPECT_FALSE(m_system.HasDirectionalLight());
}

TEST_F(LightCullingTest, ClusterGridConfig) {
    const auto& config = m_system.GetConfig();
    EXPECT_EQ(config.tilesX, 4u);
    EXPECT_EQ(config.tilesY, 4u);
    EXPECT_EQ(config.slices, 8u);
    EXPECT_EQ(config.maxLightsPerCluster, 32u);
}

TEST_F(LightCullingTest, MixedLightTypes) {
    m_system.BeginFrame();

    LightInfo point;
    point.type = LightType::Point;
    point.color = {1, 1, 1};
    point.intensity = 50.0f;
    point.range = 10.0f;

    LightInfo spot;
    spot.type = LightType::Spot;
    spot.direction = {0, -1, 0};
    spot.color = {1, 1, 1};
    spot.intensity = 200.0f;
    spot.range = 20.0f;
    spot.outerAngle = 0.5f;

    m_system.AddLight(point);
    m_system.AddLight(spot);
    m_system.AddLight(point);
    m_system.AddLight(spot);
    m_system.AddLight(point);

    EXPECT_EQ(m_system.GetLightCount(), 5u);
    EXPECT_EQ(m_system.GetPointLightCount(), 3u);
    EXPECT_EQ(m_system.GetSpotLightCount(), 2u);
}

TEST_F(LightCullingTest, ShadowMapIndex) {
    m_system.BeginFrame();

    LightInfo light;
    light.type = LightType::Spot;
    light.direction = {0, -1, 0};
    light.color = {1, 1, 1};
    light.intensity = 200.0f;
    light.range = 20.0f;
    light.outerAngle = 0.5f;
    light.castShadow = true;
    light.shadowMapIndex = 3;

    m_system.AddLight(light);
    EXPECT_EQ(m_system.GetLightCount(), 1u);
}
