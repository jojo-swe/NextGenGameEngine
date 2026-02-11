#include <gtest/gtest.h>
#include "engine/assets/shader_reflection.h"
#include "engine/rhi/common/rhi_pso_builder.h"
#include "engine/rhi/common/rhi_pso_hash.h"
#include "engine/rhi/common/rhi_pipeline_layout_cache.h"

using namespace nge;
using namespace nge::assets;
using namespace nge::rhi;

// ─── Shader Reflection Tests ─────────────────────────────────────────────

TEST(ShaderReflection, BindingTypeNames) {
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::UniformBuffer), "UniformBuffer");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::StorageBuffer), "StorageBuffer");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::SampledImage), "SampledImage");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::StorageImage), "StorageImage");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::Sampler), "Sampler");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::CombinedImageSampler), "CombinedImageSampler");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::InputAttachment), "InputAttachment");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::AccelerationStructure), "AccelerationStructure");
    EXPECT_STREQ(ShaderReflector::BindingTypeName(BindingType::PushConstant), "PushConstant");
}

TEST(ShaderReflection, StageNames) {
    EXPECT_STREQ(ShaderReflector::StageName(assets::ShaderStage::Vertex), "Vertex");
    EXPECT_STREQ(ShaderReflector::StageName(assets::ShaderStage::Fragment), "Fragment");
    EXPECT_STREQ(ShaderReflector::StageName(assets::ShaderStage::Compute), "Compute");
    EXPECT_STREQ(ShaderReflector::StageName(assets::ShaderStage::Mesh), "Mesh");
    EXPECT_STREQ(ShaderReflector::StageName(assets::ShaderStage::RayGen), "RayGen");
}

TEST(ShaderReflection, RejectInvalidSPIRV) {
    ShaderReflectionData data;
    // Empty data
    EXPECT_FALSE(ShaderReflector::Reflect(nullptr, 0, data));

    // Too short
    std::vector<u32> tooShort = {0x07230203, 0, 0};
    EXPECT_FALSE(ShaderReflector::Reflect(tooShort, data));

    // Wrong magic
    std::vector<u32> wrongMagic = {0xDEADBEEF, 0x00010000, 0, 5, 0};
    EXPECT_FALSE(ShaderReflector::Reflect(wrongMagic, data));
}

TEST(ShaderReflection, MergeBindingsEmpty) {
    std::vector<ShaderReflectionData> stages;
    auto merged = ShaderReflector::MergeBindings(stages);
    EXPECT_TRUE(merged.empty());
}

TEST(ShaderReflection, MergeBindingsDeduplicates) {
    ShaderReflectionData vs;
    vs.stage = assets::ShaderStage::Vertex;
    ReflectedBinding b1;
    b1.name = "CameraUBO";
    b1.type = BindingType::UniformBuffer;
    b1.set = 0;
    b1.binding = 0;
    b1.count = 1;
    b1.stage = assets::ShaderStage::Vertex;
    vs.bindings.push_back(b1);

    ShaderReflectionData fs;
    fs.stage = assets::ShaderStage::Fragment;
    ReflectedBinding b2 = b1;
    b2.stage = assets::ShaderStage::Fragment;
    fs.bindings.push_back(b2);

    auto merged = ShaderReflector::MergeBindings({vs, fs});
    EXPECT_EQ(merged.size(), 1u); // Same set:binding → deduplicated
}

TEST(ShaderReflection, BuildSetLayouts) {
    ReflectedBinding b1;
    b1.set = 0; b1.binding = 0; b1.type = BindingType::UniformBuffer;
    ReflectedBinding b2;
    b2.set = 0; b2.binding = 1; b2.type = BindingType::SampledImage;
    ReflectedBinding b3;
    b3.set = 1; b3.binding = 0; b3.type = BindingType::StorageBuffer;

    auto layouts = ShaderReflector::BuildSetLayouts({b1, b2, b3});
    EXPECT_EQ(layouts.size(), 2u);
    EXPECT_EQ(layouts[0].set, 0u);
    EXPECT_EQ(layouts[0].bindings.size(), 2u);
    EXPECT_EQ(layouts[1].set, 1u);
    EXPECT_EQ(layouts[1].bindings.size(), 1u);
}

// ─── PSO Builder Tests ───────────────────────────────────────────────────

TEST(PSOBuilder, OpaquePresetValid) {
    auto builder = GraphicsPSOBuilder::Opaque();
    builder.SetVertexShader("shaders/default.vert.spv")
           .SetFragmentShader("shaders/default.frag.spv")
           .AddVertexBinding(0, 32)
           .AddVertexAttribute(0, Format::RGBA32_FLOAT, 0, 0)
           .AddColorFormat(Format::RGBA8_UNORM)
           .AddOpaqueAttachment()
           .SetDepthFormat(Format::D32_FLOAT)
           .SetLayout(1);

    auto result = builder.Validate();
    EXPECT_TRUE(result.valid);
    EXPECT_TRUE(result.errors.empty());
}

TEST(PSOBuilder, MissingShaderInvalid) {
    GraphicsPSOBuilder builder;
    // No vertex or mesh shader set
    auto result = builder.Validate();
    EXPECT_FALSE(result.valid);
    EXPECT_FALSE(result.errors.empty());
}

TEST(PSOBuilder, ConflictingShaderTypes) {
    GraphicsPSOBuilder builder;
    builder.SetVertexShader("vs.spv")
           .SetMeshShader("ms.spv")
           .SetFragmentShader("fs.spv");

    auto result = builder.Validate();
    EXPECT_FALSE(result.valid);
}

TEST(PSOBuilder, BlendFormatCountMismatch) {
    auto builder = GraphicsPSOBuilder::Opaque();
    builder.SetVertexShader("vs.spv")
           .SetFragmentShader("fs.spv")
           .AddColorFormat(Format::RGBA8_UNORM)
           .AddColorFormat(Format::RGBA8_UNORM) // 2 formats
           .AddOpaqueAttachment();               // 1 blend attachment

    auto result = builder.Validate();
    EXPECT_FALSE(result.valid);
}

TEST(PSOBuilder, InvalidSampleCount) {
    auto builder = GraphicsPSOBuilder::Opaque();
    builder.SetVertexShader("vs.spv")
           .SetFragmentShader("fs.spv")
           .SetSampleCount(3); // Not power of 2

    auto result = builder.Validate();
    EXPECT_FALSE(result.valid);
}

TEST(PSOBuilder, ComputePSOValid) {
    ComputePSOBuilder builder;
    builder.SetShader("shaders/compute.comp.spv")
           .SetEntryPoint("CSMain")
           .SetLayout(1)
           .SetName("TestCompute");

    auto result = builder.Validate();
    EXPECT_TRUE(result.valid);
}

TEST(PSOBuilder, ComputePSONoShader) {
    ComputePSOBuilder builder;
    auto result = builder.Validate();
    EXPECT_FALSE(result.valid);
}

// ─── PSO Hash Tests ──────────────────────────────────────────────────────

TEST(PSOHash, IdenticalDescsSameHash) {
    auto desc1 = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .AddColorFormat(Format::RGBA8_UNORM)
        .SetDepthFormat(Format::D32_FLOAT)
        .Build();

    auto desc2 = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .AddColorFormat(Format::RGBA8_UNORM)
        .SetDepthFormat(Format::D32_FLOAT)
        .Build();

    EXPECT_EQ(PSOHasher::Hash(desc1), PSOHasher::Hash(desc2));
}

TEST(PSOHash, DifferentShadersDifferentHash) {
    auto desc1 = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("vs_a.spv")
        .SetFragmentShader("fs.spv")
        .Build();

    auto desc2 = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("vs_b.spv")
        .SetFragmentShader("fs.spv")
        .Build();

    EXPECT_NE(PSOHasher::Hash(desc1), PSOHasher::Hash(desc2));
}

TEST(PSOHash, DifferentCullModeDifferentHash) {
    auto desc1 = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .SetCullMode(CullMode::Back)
        .Build();

    auto desc2 = GraphicsPSOBuilder::Opaque()
        .SetVertexShader("vs.spv")
        .SetFragmentShader("fs.spv")
        .SetCullMode(CullMode::None)
        .Build();

    EXPECT_NE(PSOHasher::Hash(desc1), PSOHasher::Hash(desc2));
}

TEST(PSOHash, ComputeHashDeterministic) {
    ComputePSODesc desc;
    desc.shader = "compute.spv";
    desc.entryPoint = "CSMain";
    desc.layoutHandle = 42;

    auto h1 = PSOHasher::Hash(desc);
    auto h2 = PSOHasher::Hash(desc);
    EXPECT_EQ(h1, h2);
}

TEST(PSOHash, IncrementalBuilder) {
    PSOHasher::Builder b;
    b.Feed(static_cast<u32>(42));
    b.Feed(std::string("test"));
    b.Feed(true);
    auto hash = b.Finalize();
    EXPECT_NE(hash.value, 0u);
}
