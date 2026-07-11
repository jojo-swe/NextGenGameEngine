#include <gtest/gtest.h>

#include "engine/assets/gltf_importer.h"
#include "engine/core/logging/log.h"
#include <filesystem>

using namespace nge;
using namespace nge::assets;

namespace {
const char* FIXTURE_DIR = "tests/fixtures";
}

TEST(GLTFImporter, ImportNonExistentFile) {
    GLTFImporter importer;
    GLTFImportOptions options;
    auto result = importer.Import("nonexistent.gltf", options);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error.empty());
}

TEST(GLTFImporter, ImportTriangleFixture) {
    std::string path = std::string(FIXTURE_DIR) + "/triangle.gltf";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Fixture not found: " << path;
    }

    GLTFImporter importer;
    GLTFImportOptions options;
    options.generateTangents = false; // Zeros won't produce valid tangents
    auto result = importer.Import(path, options);

#ifdef NGE_HAS_CGLTF
    ASSERT_TRUE(result.success) << result.error;
    ASSERT_EQ(result.meshes.size(), 1u);
    EXPECT_EQ(result.meshes[0].positions.size(), 3u);
    EXPECT_EQ(result.meshes[0].normals.size(), 3u);
    EXPECT_EQ(result.meshes[0].texcoords0.size(), 3u);
    EXPECT_EQ(result.meshes[0].indices.size(), 3u);
    EXPECT_EQ(result.meshes[0].primitives.size(), 1u);
    EXPECT_EQ(result.meshes[0].primitives[0].indexCount, 3u);
    EXPECT_EQ(result.meshes[0].primitives[0].materialIndex, 0u);

    ASSERT_EQ(result.materials.size(), 1u);
    EXPECT_FLOAT_EQ(result.materials[0].baseColorFactor.x, 0.8f);
    EXPECT_FLOAT_EQ(result.materials[0].baseColorFactor.y, 0.2f);
    EXPECT_FLOAT_EQ(result.materials[0].metallicFactor, 0.1f);
    EXPECT_FLOAT_EQ(result.materials[0].roughnessFactor, 0.5f);
    EXPECT_TRUE(result.materials[0].doubleSided);

    ASSERT_EQ(result.nodes.size(), 1u);
    EXPECT_EQ(result.nodes[0].meshIndex, 0);
    ASSERT_EQ(result.rootNodes.size(), 1u);
    EXPECT_EQ(result.rootNodes[0], 0u);
#else
    GTEST_SKIP() << "Built without cgltf support";
#endif
}

TEST(GLTFImporter, ImportOptionsDisableAnimations) {
    std::string path = std::string(FIXTURE_DIR) + "/triangle.gltf";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Fixture not found: " << path;
    }

    GLTFImporter importer;
    GLTFImportOptions options;
    options.importAnimations = false;
    auto result = importer.Import(path, options);

#ifdef NGE_HAS_CGLTF
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.animations.empty());
#else
    GTEST_SKIP() << "Built without cgltf support";
#endif
}
