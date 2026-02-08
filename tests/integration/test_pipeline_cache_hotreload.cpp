#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_pipeline_cache_manager.h"
#include "engine/rhi/common/rhi_pso_builder.h"
#include "engine/rhi/common/rhi_pso_hash.h"
#include "engine/assets/shader_variant_cache.h"

using namespace nge;
using namespace nge::rhi;
using namespace nge::assets;

// ─── Pipeline Cache Manager Tests ────────────────────────────────────────

TEST(PipelineCacheManager, GraphicsPSOCacheHit) {
    auto desc1 = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("shaders/default.vert.spv")
        .SetFragmentShader("shaders/default.frag.spv")
        .AddColorFormat(Format::RGBA8_UNORM)
        .SetDepthFormat(Format::D32_FLOAT)
        .Build();

    auto desc2 = desc1; // Identical

    PSOHash h1 = PSOHasher::Hash(desc1);
    PSOHash h2 = PSOHasher::Hash(desc2);
    EXPECT_EQ(h1, h2);
}

TEST(PipelineCacheManager, DifferentPresetsProduceDifferentHashes) {
    auto opaque = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .Build();

    auto transparent = GraphicsPSOBuilder::Transparent()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .Build();

    auto shadow = GraphicsPSOBuilder::ShadowMap()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .Build();

    auto wireframe = GraphicsPSOBuilder::Wireframe()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .Build();

    PSOHash hOpaque = PSOHasher::Hash(opaque);
    PSOHash hTransparent = PSOHasher::Hash(transparent);
    PSOHash hShadow = PSOHasher::Hash(shadow);
    PSOHash hWireframe = PSOHasher::Hash(wireframe);

    EXPECT_NE(hOpaque, hTransparent);
    EXPECT_NE(hOpaque, hShadow);
    EXPECT_NE(hOpaque, hWireframe);
    EXPECT_NE(hTransparent, hShadow);
}

// ─── Shader Variant Cache Tests ──────────────────────────────────────────

TEST(ShaderVariantCache, HashSourceDeterministic) {
    std::string source = "float4 main() { return float4(1,0,0,1); }";
    u64 h1 = ShaderVariantCache::HashSource(source);
    u64 h2 = ShaderVariantCache::HashSource(source);
    EXPECT_EQ(h1, h2);
    EXPECT_NE(h1, 0u);
}

TEST(ShaderVariantCache, HashSourceDiffersForDifferentCode) {
    u64 h1 = ShaderVariantCache::HashSource("void main() {}");
    u64 h2 = ShaderVariantCache::HashSource("void main() { return; }");
    EXPECT_NE(h1, h2);
}

TEST(ShaderVariantCache, HashDefinesDeterministic) {
    std::vector<std::pair<std::string, std::string>> defines = {
        {"USE_NORMAL_MAP", "1"},
        {"MAX_LIGHTS", "256"}
    };
    u64 h1 = ShaderVariantCache::HashDefines(defines);
    u64 h2 = ShaderVariantCache::HashDefines(defines);
    EXPECT_EQ(h1, h2);
}

TEST(ShaderVariantCache, HashDefinesOrderMatters) {
    std::vector<std::pair<std::string, std::string>> d1 = {{"A", "1"}, {"B", "2"}};
    std::vector<std::pair<std::string, std::string>> d2 = {{"B", "2"}, {"A", "1"}};
    u64 h1 = ShaderVariantCache::HashDefines(d1);
    u64 h2 = ShaderVariantCache::HashDefines(d2);
    EXPECT_NE(h1, h2); // Order-dependent hashing
}

TEST(ShaderVariantCache, KeyEquality) {
    ShaderVariantKey k1;
    k1.sourceHash = 123;
    k1.definesHash = 456;
    k1.entryPoint = "VSMain";
    k1.targetProfile = "vs_6_6";

    ShaderVariantKey k2 = k1;
    EXPECT_TRUE(k1 == k2);

    k2.entryPoint = "PSMain";
    EXPECT_FALSE(k1 == k2);
}

TEST(ShaderVariantCache, KeyHashConsistency) {
    ShaderVariantKey key;
    key.sourceHash = 0xDEADBEEF;
    key.definesHash = 0xCAFEBABE;
    key.entryPoint = "CSMain";
    key.targetProfile = "cs_6_6";

    ShaderVariantKeyHash hasher;
    size_t h1 = hasher(key);
    size_t h2 = hasher(key);
    EXPECT_EQ(h1, h2);
}

// ─── PSO Builder Preset Tests ────────────────────────────────────────────

TEST(PSOBuilderPresets, FullscreenTriangleNoDepth) {
    auto desc = GraphicsPSOBuilder::FullscreenTriangle()
        .SetVertexShader("fullscreen.vert.spv")
        .SetFragmentShader("postprocess.frag.spv")
        .AddColorFormat(Format::RGBA16_FLOAT)
        .AddOpaqueAttachment()
        .Build();

    EXPECT_FALSE(desc.depthTestEnable);
    EXPECT_FALSE(desc.depthWriteEnable);
    EXPECT_EQ(desc.cullMode, CullMode::None);
}

TEST(PSOBuilderPresets, ShadowMapDepthBias) {
    auto desc = GraphicsPSOBuilder::ShadowMap()
        .SetVertexShader("shadow.vert.spv")
        .SetFragmentShader("shadow.frag.spv")
        .SetDepthFormat(Format::D32_FLOAT)
        .Build();

    EXPECT_EQ(desc.cullMode, CullMode::Front);
    EXPECT_TRUE(desc.depthTestEnable);
    EXPECT_TRUE(desc.depthWriteEnable);
    EXPECT_GT(desc.depthBiasConstant, 0.0f);
    EXPECT_GT(desc.depthBiasSlope, 0.0f);
}

TEST(PSOBuilderPresets, WireframeNoDepthWrite) {
    auto desc = GraphicsPSOBuilder::Wireframe()
        .SetVertexShader("wire.vert.spv")
        .SetFragmentShader("wire.frag.spv")
        .Build();

    EXPECT_EQ(desc.polygonMode, PolygonMode::Line);
    EXPECT_EQ(desc.cullMode, CullMode::None);
    EXPECT_FALSE(desc.depthWriteEnable);
}
