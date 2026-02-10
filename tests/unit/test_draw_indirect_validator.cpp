#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_draw_indirect_validator.h"

using namespace nge;
using namespace nge::rhi;

TEST(DrawIndirectValidator, InitAndShutdown) {
    DrawIndirectValidator validator;
    EXPECT_TRUE(validator.Init());

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDrawsValidated, 0u);
    EXPECT_EQ(stats.totalErrors, 0u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ValidDrawPasses) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndirectArgs args{100, 1, 0, 0};
    EXPECT_TRUE(validator.ValidateDraw(args));

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDrawsValidated, 1u);
    EXPECT_EQ(stats.totalErrors, 0u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ZeroVertexCount) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.warnOnZeroCounts = true;
    validator.Init(config);

    DrawIndirectArgs args{0, 1, 0, 0};
    EXPECT_FALSE(validator.ValidateDraw(args));

    EXPECT_EQ(validator.GetStats().zeroCountWarnings, 1u);
    EXPECT_EQ(validator.GetErrors().size(), 1u);
    EXPECT_EQ(validator.GetErrors()[0].issue, ValidationIssue::ZeroVertexCount);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ZeroInstanceCount) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.warnOnZeroCounts = true;
    validator.Init(config);

    DrawIndirectArgs args{100, 0, 0, 0};
    EXPECT_FALSE(validator.ValidateDraw(args));

    EXPECT_EQ(validator.GetErrors()[0].issue, ValidationIssue::ZeroInstanceCount);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ExcessiveVertexCount) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxVertexCount = 1000;
    validator.Init(config);

    DrawIndirectArgs args{5000, 1, 0, 0};
    EXPECT_FALSE(validator.ValidateDraw(args));

    EXPECT_EQ(validator.GetStats().excessiveCountErrors, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ExcessiveInstanceCount) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxInstanceCount = 100;
    validator.Init(config);

    DrawIndirectArgs args{10, 500, 0, 0};
    EXPECT_FALSE(validator.ValidateDraw(args));

    EXPECT_EQ(validator.GetStats().excessiveCountErrors, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, VertexOutOfBounds) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndirectArgs args{100, 1, 950, 0};
    // firstVertex(950) + vertexCount(100) = 1050 > bufferSize(1000)
    EXPECT_FALSE(validator.ValidateDraw(args, 0, 1000));

    EXPECT_EQ(validator.GetStats().outOfBoundsErrors, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, VertexInBounds) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndirectArgs args{100, 1, 900, 0};
    // firstVertex(900) + vertexCount(100) = 1000 == bufferSize(1000)
    EXPECT_TRUE(validator.ValidateDraw(args, 0, 1000));

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ValidIndexedDraw) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndexedIndirectArgs args{300, 1, 0, 0, 0};
    EXPECT_TRUE(validator.ValidateDrawIndexed(args));

    EXPECT_EQ(validator.GetStats().totalIndexedDrawsValidated, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ZeroIndexCount) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.warnOnZeroCounts = true;
    validator.Init(config);

    DrawIndexedIndirectArgs args{0, 1, 0, 0, 0};
    EXPECT_FALSE(validator.ValidateDrawIndexed(args));

    EXPECT_EQ(validator.GetErrors()[0].issue, ValidationIssue::ZeroIndexCount);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ExcessiveIndexCount) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxIndexCount = 1000;
    validator.Init(config);

    DrawIndexedIndirectArgs args{5000, 1, 0, 0, 0};
    EXPECT_FALSE(validator.ValidateDrawIndexed(args));

    EXPECT_EQ(validator.GetStats().excessiveCountErrors, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, IndexOutOfBounds) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndexedIndirectArgs args{100, 1, 950, 0, 0};
    EXPECT_FALSE(validator.ValidateDrawIndexed(args, 0, 1000));

    EXPECT_EQ(validator.GetStats().outOfBoundsErrors, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, NegativeVertexOffset) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndexedIndirectArgs args{100, 1, 0, -5, 0};
    EXPECT_FALSE(validator.ValidateDrawIndexed(args));

    EXPECT_EQ(validator.GetErrors()[0].issue, ValidationIssue::NegativeVertexOffset);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ValidDispatch) {
    DrawIndirectValidator validator;
    validator.Init();

    DispatchIndirectArgs args{16, 16, 1};
    EXPECT_TRUE(validator.ValidateDispatch(args));

    EXPECT_EQ(validator.GetStats().totalDispatchesValidated, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ZeroDispatchGroups) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.warnOnZeroCounts = true;
    validator.Init(config);

    DispatchIndirectArgs args{16, 0, 1};
    EXPECT_FALSE(validator.ValidateDispatch(args));

    EXPECT_EQ(validator.GetErrors()[0].issue, ValidationIssue::ZeroDispatchGroups);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ExcessiveDispatchGroups) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxDispatchGroupsPerDim = 1024;
    validator.Init(config);

    DispatchIndirectArgs args{2000, 1, 1};
    EXPECT_FALSE(validator.ValidateDispatch(args));

    EXPECT_EQ(validator.GetStats().excessiveCountErrors, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, DrawBatchValidation) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndirectArgs batch[3] = {
        {100, 1, 0, 0},   // Valid
        {0, 1, 0, 0},     // Zero vertex count
        {200, 1, 0, 0},   // Valid
    };

    u32 invalidCount = validator.ValidateDrawBatch(batch, 3);
    EXPECT_EQ(invalidCount, 1u);
    EXPECT_EQ(validator.GetStats().totalDrawsValidated, 3u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, DrawIndexedBatchValidation) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndexedIndirectArgs batch[2] = {
        {300, 1, 0, 0, 0},    // Valid
        {100, 1, 950, 0, 0},  // OOB with indexBufferSize=1000
    };

    u32 invalidCount = validator.ValidateDrawIndexedBatch(batch, 2, 1000);
    EXPECT_EQ(invalidCount, 1u);
    EXPECT_EQ(validator.GetStats().totalIndexedDrawsValidated, 2u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, SanitizeDraw) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxVertexCount = 100;
    config.maxInstanceCount = 10;
    validator.Init(config);

    DrawIndirectArgs args{500, 50, 0, 0};
    validator.SanitizeDraw(args);

    EXPECT_EQ(args.vertexCount, 100u);
    EXPECT_EQ(args.instanceCount, 10u);
    EXPECT_GE(validator.GetStats().drawsClamped, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, SanitizeDrawIndexed) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxIndexCount = 500;
    config.maxInstanceCount = 10;
    validator.Init(config);

    DrawIndexedIndirectArgs args{1000, 50, 0, 0, 0};
    validator.SanitizeDrawIndexed(args);

    EXPECT_EQ(args.indexCount, 500u);
    EXPECT_EQ(args.instanceCount, 10u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, SanitizeDispatch) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxDispatchGroupsPerDim = 64;
    validator.Init(config);

    DispatchIndirectArgs args{128, 256, 1};
    validator.SanitizeDispatch(args);

    EXPECT_EQ(args.groupCountX, 64u);
    EXPECT_EQ(args.groupCountY, 64u);
    EXPECT_EQ(args.groupCountZ, 1u);

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ClearErrors) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndirectArgs args{0, 1, 0, 0};
    validator.ValidateDraw(args);
    EXPECT_FALSE(validator.GetErrors().empty());

    validator.ClearErrors();
    EXPECT_TRUE(validator.GetErrors().empty());

    validator.Shutdown();
}

TEST(DrawIndirectValidator, ResetClearsAll) {
    DrawIndirectValidator validator;
    validator.Init();

    DrawIndirectArgs args{0, 1, 0, 0};
    validator.ValidateDraw(args);

    validator.Reset();

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDrawsValidated, 0u);
    EXPECT_EQ(stats.totalErrors, 0u);
    EXPECT_EQ(stats.zeroCountWarnings, 0u);
    EXPECT_TRUE(validator.GetErrors().empty());

    validator.Shutdown();
}

TEST(DrawIndirectValidator, MultipleErrorsAccumulate) {
    DrawIndirectValidator validator;
    DrawIndirectValidatorConfig config;
    config.maxVertexCount = 100;
    config.warnOnZeroCounts = true;
    validator.Init(config);

    validator.ValidateDraw({0, 1, 0, 0});       // Zero vtx
    validator.ValidateDraw({500, 1, 0, 0});      // Excessive vtx
    validator.ValidateDraw({50, 0, 0, 0});       // Zero inst
    validator.ValidateDraw({50, 1, 980, 0}, 3, 1000); // OOB

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDrawsValidated, 4u);
    EXPECT_EQ(stats.zeroCountWarnings, 2u);
    EXPECT_EQ(stats.excessiveCountErrors, 1u);
    EXPECT_EQ(stats.outOfBoundsErrors, 1u);
    EXPECT_EQ(validator.GetErrors().size(), 4u);

    validator.Shutdown();
}
