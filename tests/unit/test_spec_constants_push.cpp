#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_spec_constants.h"
#include "engine/rhi/common/rhi_push_constants.h"

using namespace nge::rhi;

// ─── Specialization Constant Tests ───────────────────────────────────────

TEST(SpecConstants, RegisterAndGetDefaults) {
    SpecConstantManager mgr;
    auto map = SpecConstantManager::PBRMaterial();
    u32 id = mgr.RegisterMap(map);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(mgr.GetMapCount(), 1u);

    // Default: USE_NORMAL_MAP = true (constant 0)
    auto val = mgr.GetValue(id, 0);
    EXPECT_TRUE(std::holds_alternative<bool>(val));
    EXPECT_TRUE(std::get<bool>(val));

    // Default: MAX_LIGHTS = 256 (constant 6)
    auto lightsVal = mgr.GetValue(id, 6);
    EXPECT_TRUE(std::holds_alternative<u32>(lightsVal));
    EXPECT_EQ(std::get<u32>(lightsVal), 256u);
}

TEST(SpecConstants, SetBoolById) {
    SpecConstantManager mgr;
    auto map = SpecConstantManager::PBRMaterial();
    u32 id = mgr.RegisterMap(map);

    mgr.SetBool(id, 0, false); // USE_NORMAL_MAP = false
    auto val = mgr.GetValue(id, 0);
    EXPECT_TRUE(std::holds_alternative<bool>(val));
    EXPECT_FALSE(std::get<bool>(val));
}

TEST(SpecConstants, SetByName) {
    SpecConstantManager mgr;
    auto map = SpecConstantManager::PBRMaterial();
    u32 id = mgr.RegisterMap(map);

    mgr.SetUint(id, "MAX_LIGHTS", 512);
    auto val = mgr.GetValue(id, 6);
    EXPECT_TRUE(std::holds_alternative<u32>(val));
    EXPECT_EQ(std::get<u32>(val), 512u);
}

TEST(SpecConstants, SetFloat) {
    SpecConstantManager mgr;
    SpecConstantMap map;
    map.debugName = "test";
    map.entries = {{0, "GAMMA", 2.2f}};
    u32 id = mgr.RegisterMap(map);

    mgr.SetFloat(id, 0, 1.8f);
    auto val = mgr.GetValue(id, 0);
    EXPECT_TRUE(std::holds_alternative<f32>(val));
    EXPECT_FLOAT_EQ(std::get<f32>(val), 1.8f);
}

TEST(SpecConstants, ResetToDefaults) {
    SpecConstantManager mgr;
    auto map = SpecConstantManager::PBRMaterial();
    u32 id = mgr.RegisterMap(map);

    mgr.SetBool(id, 0, false);
    mgr.SetUint(id, 6, 1024);

    mgr.ResetToDefaults(id);

    auto normalMap = mgr.GetValue(id, 0);
    EXPECT_TRUE(std::get<bool>(normalMap));
    auto lights = mgr.GetValue(id, 6);
    EXPECT_EQ(std::get<u32>(lights), 256u);
}

TEST(SpecConstants, BuildDataNonEmpty) {
    SpecConstantManager mgr;
    SpecConstantMap map;
    map.debugName = "test";
    map.entries = {
        {0, "ENABLE_A", true},
        {1, "COUNT", u32(16)},
        {2, "SCALE", 1.5f},
    };
    u32 id = mgr.RegisterMap(map);

    auto data = mgr.BuildData(id);
    EXPECT_FALSE(data.data.empty());
    EXPECT_EQ(data.mapEntryIds.size(), 3u);
    EXPECT_EQ(data.mapEntryOffsets.size(), 3u);
    EXPECT_EQ(data.mapEntrySizes.size(), 3u);

    // Bool is 1 byte, u32 is 4, f32 is 4 → total 9 bytes
    EXPECT_EQ(data.data.size(), sizeof(bool) + sizeof(u32) + sizeof(f32));
}

TEST(SpecConstants, PostProcessPreset) {
    SpecConstantManager mgr;
    auto map = SpecConstantManager::PostProcess();
    u32 id = mgr.RegisterMap(map);

    // ENABLE_BLOOM default = true
    auto bloom = mgr.GetValue(id, 0);
    EXPECT_TRUE(std::get<bool>(bloom));

    // TONEMAP_OPERATOR default = 0 (ACES)
    auto tonemap = mgr.GetValue(id, 4);
    EXPECT_EQ(std::get<u32>(tonemap), 0u);
}

TEST(SpecConstants, MultipleMapIds) {
    SpecConstantManager mgr;
    u32 id0 = mgr.RegisterMap(SpecConstantManager::PBRMaterial());
    u32 id1 = mgr.RegisterMap(SpecConstantManager::PostProcess());
    EXPECT_NE(id0, id1);
    EXPECT_EQ(mgr.GetMapCount(), 2u);
}

// ─── Push Constant Validation Tests ──────────────────────────────────────

TEST(PushConstants, ValidLayout) {
    PushConstantLayout layout;
    layout.totalSize = 64;
    layout.ranges = {
        {ShaderStage::Vertex | ShaderStage::Fragment, 0, 64},
    };

    auto result = PushConstantManager::Validate(layout);
    EXPECT_TRUE(result.valid);
}

TEST(PushConstants, ExceedsMaxSize) {
    PushConstantLayout layout;
    layout.totalSize = 256; // > 128
    layout.ranges = {
        {ShaderStage::Vertex, 0, 256},
    };

    auto result = PushConstantManager::Validate(layout);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("exceeds maximum"), std::string::npos);
}

TEST(PushConstants, RangeExceedsBounds) {
    PushConstantLayout layout;
    layout.totalSize = 128;
    layout.ranges = {
        {ShaderStage::Vertex, 64, 128}, // 64 + 128 = 192 > 128
    };

    auto result = PushConstantManager::Validate(layout);
    EXPECT_FALSE(result.valid);
}

TEST(PushConstants, ZeroSizeRange) {
    PushConstantLayout layout;
    layout.totalSize = 64;
    layout.ranges = {
        {ShaderStage::Vertex, 0, 0},
    };

    auto result = PushConstantManager::Validate(layout);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("zero size"), std::string::npos);
}

TEST(PushConstants, MisalignedOffset) {
    PushConstantLayout layout;
    layout.totalSize = 64;
    layout.ranges = {
        {ShaderStage::Vertex, 3, 4}, // offset 3 not 4-byte aligned
    };

    auto result = PushConstantManager::Validate(layout);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("aligned"), std::string::npos);
}

TEST(PushConstants, MisalignedSize) {
    PushConstantLayout layout;
    layout.totalSize = 64;
    layout.ranges = {
        {ShaderStage::Vertex, 0, 5}, // size not multiple of 4
    };

    auto result = PushConstantManager::Validate(layout);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("multiple of 4"), std::string::npos);
}

TEST(PushConstants, OverlappingRanges) {
    PushConstantLayout layout;
    layout.totalSize = 64;
    layout.ranges = {
        {ShaderStage::Vertex, 0, 32},
        {ShaderStage::Vertex, 16, 32}, // Overlaps with first range, same stage
    };

    auto result = PushConstantManager::Validate(layout);
    EXPECT_FALSE(result.valid);
    EXPECT_NE(result.error.find("Overlapping"), std::string::npos);
}

TEST(PushConstants, NonOverlappingDifferentStages) {
    PushConstantLayout layout;
    layout.totalSize = 64;
    layout.ranges = {
        {ShaderStage::Vertex, 0, 32},
        {ShaderStage::Fragment, 0, 32}, // Same offset, different stages → overlapping byte range with shared stages? No, different flags
    };

    // Different stage flags, same byte range → no validation error
    // (Vulkan allows different stages to share the same byte range)
    auto result = PushConstantManager::Validate(layout);
    EXPECT_TRUE(result.valid);
}

TEST(PushConstants, RegisterAndCount) {
    PushConstantManager mgr;
    PushConstantLayout layout;
    layout.totalSize = 64;
    layout.ranges = {{ShaderStage::All, 0, 64}};

    u32 id = mgr.RegisterLayout(layout);
    EXPECT_EQ(id, 0u);
    EXPECT_EQ(mgr.GetRegisteredCount(), 1u);
}

TEST(PushConstants, MergeAdjacentRanges) {
    std::vector<PushConstantRange> ranges = {
        {ShaderStage::Vertex, 0, 16},
        {ShaderStage::Vertex, 16, 16},
        {ShaderStage::Vertex, 32, 16},
    };

    auto merged = PushConstantManager::MergeRanges(ranges);
    EXPECT_EQ(merged.size(), 1u);
    EXPECT_EQ(merged[0].offset, 0u);
    EXPECT_EQ(merged[0].size, 48u);
}

TEST(PushConstants, NoMergeDifferentStages) {
    std::vector<PushConstantRange> ranges = {
        {ShaderStage::Vertex, 0, 32},
        {ShaderStage::Fragment, 32, 32},
    };

    auto merged = PushConstantManager::MergeRanges(ranges);
    EXPECT_EQ(merged.size(), 2u);
}
