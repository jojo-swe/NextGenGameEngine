#include <gtest/gtest.h>
#include "engine/renderer/materials/material_system.h"

using namespace nge;
using namespace nge::renderer;

TEST(MaterialSystem, GPUMaterialDataSize) {
    EXPECT_EQ(sizeof(GPUMaterialData), 96u);
}

TEST(MaterialSystem, DefaultTextureIndices) {
    GPUMaterialData data{};
    // Verify uninitialized texture indices are 0 (memset)
    // After proper initialization via CreateMaterial, they should be UINT32_MAX
    EXPECT_EQ(data.albedoTexIdx, 0u);
}

TEST(MaterialSystem, MaterialFlagsOperators) {
    auto flags = MaterialFlags::AlphaTest | MaterialFlags::DoubleSided;
    EXPECT_TRUE(flags & MaterialFlags::AlphaTest);
    EXPECT_TRUE(flags & MaterialFlags::DoubleSided);
    EXPECT_FALSE(flags & MaterialFlags::AlphaBlend);
    EXPECT_FALSE(flags & MaterialFlags::Emissive);
}

TEST(MaterialSystem, TextureSlotCount) {
    EXPECT_EQ(static_cast<u32>(TextureSlot::Count), 8u);
}

TEST(MaterialSystem, InvalidMaterialConstant) {
    EXPECT_EQ(INVALID_MATERIAL, UINT32_MAX);
}

TEST(MaterialSystem, MaterialFlagsBitValues) {
    EXPECT_EQ(static_cast<u32>(MaterialFlags::None), 0u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::AlphaTest), 1u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::AlphaBlend), 2u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::DoubleSided), 4u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::Emissive), 8u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::HasNormalMap), 16u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::HasHeightMap), 32u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::HasDetailTextures), 64u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::Unlit), 128u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::Subsurface), 256u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::ClearCoat), 512u);
    EXPECT_EQ(static_cast<u32>(MaterialFlags::Anisotropic), 1024u);
}

TEST(MaterialSystem, GPUMaterialDataLayout) {
    GPUMaterialData data{};
    data.baseColorFactor = {1, 0.5f, 0.25f, 1};
    data.metallicFactor = 0.8f;
    data.roughnessFactor = 0.2f;
    data.emissiveStrength = 5.0f;
    data.alphaCutoff = 0.33f;
    data.normalScale = 2.0f;
    data.heightScale = 0.05f;
    data.detailTiling = 4.0f;
    data.clearCoatFactor = 0.5f;
    data.flags = static_cast<u32>(MaterialFlags::Emissive | MaterialFlags::ClearCoat);

    EXPECT_FLOAT_EQ(data.baseColorFactor.x, 1.0f);
    EXPECT_FLOAT_EQ(data.baseColorFactor.y, 0.5f);
    EXPECT_FLOAT_EQ(data.metallicFactor, 0.8f);
    EXPECT_FLOAT_EQ(data.roughnessFactor, 0.2f);
    EXPECT_FLOAT_EQ(data.emissiveStrength, 5.0f);
    EXPECT_FLOAT_EQ(data.alphaCutoff, 0.33f);
    EXPECT_FLOAT_EQ(data.normalScale, 2.0f);
    EXPECT_FLOAT_EQ(data.heightScale, 0.05f);
    EXPECT_FLOAT_EQ(data.detailTiling, 4.0f);
    EXPECT_FLOAT_EQ(data.clearCoatFactor, 0.5f);
    EXPECT_EQ(data.flags, static_cast<u32>(MaterialFlags::Emissive | MaterialFlags::ClearCoat));
}
