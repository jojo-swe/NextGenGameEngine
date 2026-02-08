#include <gtest/gtest.h>
#include "engine/assets/shader_permutations.h"

using namespace nge;
using namespace nge::assets;

class ShaderPermutationTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_manager.Init();
    }

    void TearDown() override {
        m_manager.Shutdown();
    }

    ShaderPermutationManager m_manager;
};

TEST_F(ShaderPermutationTest, MakeKeyFromBits) {
    auto key = ShaderPermutationManager::MakeKey({0, 2, 5});
    EXPECT_EQ(key, (1u << 0) | (1u << 2) | (1u << 5));
    EXPECT_EQ(key, 0b100101u);
}

TEST_F(ShaderPermutationTest, MakeKeyEmpty) {
    auto key = ShaderPermutationManager::MakeKey({});
    EXPECT_EQ(key, 0u);
}

TEST_F(ShaderPermutationTest, MakeKeyFromVector) {
    std::vector<u32> bits = {1, 3};
    auto key = ShaderPermutationManager::MakeKey(bits);
    EXPECT_EQ(key, (1u << 1) | (1u << 3));
}

TEST_F(ShaderPermutationTest, RegisterShader) {
    ShaderPermutationDesc desc;
    desc.sourcePath = "shaders/test.hlsl";
    desc.entryPoint = "CSMain";
    desc.profile = "cs_6_6";
    desc.features.push_back({"HAS_NORMAL_MAP", 0, "Enable normal mapping"});
    desc.features.push_back({"HAS_EMISSIVE", 1, "Enable emissive"});

    m_manager.RegisterShader("test_shader", desc);

    auto shaders = m_manager.GetRegisteredShaders();
    EXPECT_EQ(shaders.size(), 1u);
    EXPECT_EQ(shaders[0], "test_shader");
}

TEST_F(ShaderPermutationTest, GetDefines) {
    ShaderPermutationDesc desc;
    desc.sourcePath = "shaders/test.hlsl";
    desc.entryPoint = "CSMain";
    desc.profile = "cs_6_6";
    desc.staticDefines = {"VULKAN=1"};
    desc.features.push_back({"HAS_NORMAL_MAP", 0, ""});
    desc.features.push_back({"HAS_EMISSIVE", 1, ""});

    m_manager.RegisterShader("test_shader", desc);

    auto key = ShaderPermutationManager::MakeKey({0}); // Only normal map
    auto defines = m_manager.GetDefines("test_shader", key);

    EXPECT_EQ(defines.size(), 3u); // 1 static + 2 feature
    EXPECT_EQ(defines[0], "VULKAN=1");
    EXPECT_EQ(defines[1], "HAS_NORMAL_MAP=1");
    EXPECT_EQ(defines[2], "HAS_EMISSIVE=0");
}

TEST_F(ShaderPermutationTest, CompileWithCallback) {
    ShaderPermutationDesc desc;
    desc.sourcePath = "shaders/test.hlsl";
    desc.entryPoint = "CSMain";
    desc.profile = "cs_6_6";
    desc.features.push_back({"FEATURE_A", 0, ""});

    m_manager.RegisterShader("test_shader", desc);

    u32 compileCalled = 0;
    m_manager.SetCompileCallback([&](const std::string&, const std::string&,
                                      const std::string&, const std::vector<std::string>&) {
        compileCalled++;
        return std::vector<u8>{0xDE, 0xAD};
    });

    auto key = ShaderPermutationManager::MakeKey({0});
    const auto* perm = m_manager.GetOrCompile("test_shader", key);

    EXPECT_NE(perm, nullptr);
    EXPECT_TRUE(perm->compiled);
    EXPECT_EQ(perm->compiledSpirV.size(), 2u);
    EXPECT_EQ(compileCalled, 1u);

    // Second call should use cache
    const auto* perm2 = m_manager.GetOrCompile("test_shader", key);
    EXPECT_EQ(compileCalled, 1u); // Not called again
    EXPECT_EQ(perm, perm2);
}

TEST_F(ShaderPermutationTest, InvalidateShader) {
    ShaderPermutationDesc desc;
    desc.sourcePath = "shaders/test.hlsl";
    desc.entryPoint = "CSMain";
    desc.profile = "cs_6_6";
    desc.features.push_back({"FEATURE_A", 0, ""});

    m_manager.RegisterShader("test_shader", desc);
    m_manager.SetCompileCallback([](const std::string&, const std::string&,
                                     const std::string&, const std::vector<std::string>&) {
        return std::vector<u8>{0x01};
    });

    m_manager.GetOrCompile("test_shader", 0);
    EXPECT_EQ(m_manager.GetVariantCount("test_shader"), 1u);

    m_manager.InvalidateShader("test_shader");
    EXPECT_EQ(m_manager.GetVariantCount("test_shader"), 0u);
}

TEST_F(ShaderPermutationTest, GetOrCompileUnknownShader) {
    m_manager.SetCompileCallback([](const std::string&, const std::string&,
                                     const std::string&, const std::vector<std::string>&) {
        return std::vector<u8>{0x01};
    });

    const auto* perm = m_manager.GetOrCompile("nonexistent", 0);
    EXPECT_EQ(perm, nullptr);
}

TEST_F(ShaderPermutationTest, TotalVariantCount) {
    ShaderPermutationDesc descA;
    descA.sourcePath = "a.hlsl";
    descA.entryPoint = "main";
    descA.profile = "cs_6_6";
    m_manager.RegisterShader("a", descA);

    ShaderPermutationDesc descB;
    descB.sourcePath = "b.hlsl";
    descB.entryPoint = "main";
    descB.profile = "cs_6_6";
    m_manager.RegisterShader("b", descB);

    m_manager.SetCompileCallback([](const std::string&, const std::string&,
                                     const std::string&, const std::vector<std::string>&) {
        return std::vector<u8>{0x01};
    });

    m_manager.GetOrCompile("a", 0);
    m_manager.GetOrCompile("a", 1);
    m_manager.GetOrCompile("b", 0);

    EXPECT_EQ(m_manager.GetTotalVariantCount(), 3u);
}
