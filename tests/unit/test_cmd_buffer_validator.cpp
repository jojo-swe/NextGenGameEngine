#include <gtest/gtest.h>
#include "engine/rhi/common/rhi_cmd_buffer_validator.h"

using namespace nge::rhi;

TEST(CmdBufferValidator, InitAndShutdown) {
    CmdBufferValidator val;
    EXPECT_TRUE(val.Init());
    EXPECT_EQ(val.GetState(), CmdBufferState::Initial);
    EXPECT_FALSE(val.HasErrors());
    val.Shutdown();
}

TEST(CmdBufferValidator, BeginEndRecording) {
    CmdBufferValidator val;
    val.Init();

    EXPECT_TRUE(val.BeginRecording());
    EXPECT_EQ(val.GetState(), CmdBufferState::Recording);

    EXPECT_TRUE(val.EndRecording());
    EXPECT_EQ(val.GetState(), CmdBufferState::Executable);

    val.Shutdown();
}

TEST(CmdBufferValidator, DoubleBeginFails) {
    CmdBufferValidator val;
    val.Init();

    EXPECT_TRUE(val.BeginRecording());
    EXPECT_FALSE(val.BeginRecording()); // Already recording

    EXPECT_TRUE(val.HasErrors());
    auto errors = val.GetErrors();
    EXPECT_EQ(errors[0], CmdValidationError::AlreadyRecording);

    val.Shutdown();
}

TEST(CmdBufferValidator, EndWithoutBeginFails) {
    CmdBufferValidator val;
    val.Init();

    EXPECT_FALSE(val.EndRecording());

    EXPECT_TRUE(val.HasErrors());
    auto errors = val.GetErrors();
    EXPECT_EQ(errors[0], CmdValidationError::EndWithoutBegin);

    val.Shutdown();
}

TEST(CmdBufferValidator, BeginEndRenderPass) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    EXPECT_TRUE(val.BeginRenderPass());
    EXPECT_EQ(val.GetState(), CmdBufferState::InRenderPass);

    EXPECT_TRUE(val.EndRenderPass());
    EXPECT_EQ(val.GetState(), CmdBufferState::Recording);

    val.EndRecording();
    val.Shutdown();
}

TEST(CmdBufferValidator, NestedRenderPassFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    EXPECT_FALSE(val.BeginRenderPass()); // Nested

    auto errors = val.GetErrors();
    EXPECT_EQ(errors[0], CmdValidationError::NestedRenderPass);

    val.Shutdown();
}

TEST(CmdBufferValidator, EndRenderPassWithoutBeginFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    EXPECT_FALSE(val.EndRenderPass()); // Not in render pass

    auto errors = val.GetErrors();
    EXPECT_EQ(errors[0], CmdValidationError::NotInRenderPass);

    val.Shutdown();
}

TEST(CmdBufferValidator, ValidDrawCall) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    val.BindPipeline();
    val.BindVertexBuffer();

    EXPECT_TRUE(val.ValidateDraw());
    EXPECT_FALSE(val.HasErrors());

    val.Shutdown();
}

TEST(CmdBufferValidator, DrawWithoutPipelineFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    val.BindVertexBuffer();

    EXPECT_FALSE(val.ValidateDraw());

    bool foundError = false;
    for (auto err : val.GetErrors()) {
        if (err == CmdValidationError::NoPipelineBound) foundError = true;
    }
    EXPECT_TRUE(foundError);

    val.Shutdown();
}

TEST(CmdBufferValidator, DrawWithoutVertexBufferFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    val.BindPipeline();

    EXPECT_FALSE(val.ValidateDraw());

    bool foundError = false;
    for (auto err : val.GetErrors()) {
        if (err == CmdValidationError::NoVertexBufferBound) foundError = true;
    }
    EXPECT_TRUE(foundError);

    val.Shutdown();
}

TEST(CmdBufferValidator, DrawOutsideRenderPassFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BindPipeline();
    val.BindVertexBuffer();

    EXPECT_FALSE(val.ValidateDraw());

    bool foundError = false;
    for (auto err : val.GetErrors()) {
        if (err == CmdValidationError::DrawOutsideRenderPass) foundError = true;
    }
    EXPECT_TRUE(foundError);

    val.Shutdown();
}

TEST(CmdBufferValidator, ValidDrawIndexed) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    val.BindPipeline();
    val.BindVertexBuffer();
    val.BindIndexBuffer();

    EXPECT_TRUE(val.ValidateDrawIndexed());
    EXPECT_FALSE(val.HasErrors());

    val.Shutdown();
}

TEST(CmdBufferValidator, DrawIndexedWithoutIndexBufferFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    val.BindPipeline();
    val.BindVertexBuffer();

    EXPECT_FALSE(val.ValidateDrawIndexed());

    bool foundError = false;
    for (auto err : val.GetErrors()) {
        if (err == CmdValidationError::NoIndexBufferBound) foundError = true;
    }
    EXPECT_TRUE(foundError);

    val.Shutdown();
}

TEST(CmdBufferValidator, ValidDispatch) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BindPipeline();

    EXPECT_TRUE(val.ValidateDispatch());
    EXPECT_FALSE(val.HasErrors());

    val.Shutdown();
}

TEST(CmdBufferValidator, DispatchInsideRenderPassFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    val.BindPipeline();

    EXPECT_FALSE(val.ValidateDispatch());

    bool foundError = false;
    for (auto err : val.GetErrors()) {
        if (err == CmdValidationError::DispatchInsideRenderPass) foundError = true;
    }
    EXPECT_TRUE(foundError);

    val.Shutdown();
}

TEST(CmdBufferValidator, DispatchWithoutPipelineFails) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();

    EXPECT_FALSE(val.ValidateDispatch());

    bool foundError = false;
    for (auto err : val.GetErrors()) {
        if (err == CmdValidationError::NoPipelineBound) foundError = true;
    }
    EXPECT_TRUE(foundError);

    val.Shutdown();
}

TEST(CmdBufferValidator, BindStateTracking) {
    CmdBufferValidator val;
    val.Init();

    EXPECT_FALSE(val.IsPipelineBound());
    EXPECT_FALSE(val.IsVertexBufferBound());
    EXPECT_FALSE(val.IsIndexBufferBound());
    EXPECT_FALSE(val.IsDescriptorSetBound());

    val.BindPipeline();
    val.BindVertexBuffer();
    val.BindIndexBuffer();
    val.BindDescriptorSet();

    EXPECT_TRUE(val.IsPipelineBound());
    EXPECT_TRUE(val.IsVertexBufferBound());
    EXPECT_TRUE(val.IsIndexBufferBound());
    EXPECT_TRUE(val.IsDescriptorSetBound());

    val.Shutdown();
}

TEST(CmdBufferValidator, PipelineResetOnRenderPass) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BindPipeline();
    EXPECT_TRUE(val.IsPipelineBound());

    val.BeginRenderPass();
    EXPECT_FALSE(val.IsPipelineBound()); // Reset on enter

    val.BindPipeline();
    val.EndRenderPass();
    EXPECT_FALSE(val.IsPipelineBound()); // Reset on exit

    val.Shutdown();
}

TEST(CmdBufferValidator, StatsTracking) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BeginRenderPass();
    val.BindPipeline();
    val.BindVertexBuffer();
    val.BindIndexBuffer();
    val.BindDescriptorSet();
    val.ValidateDraw();
    val.ValidateDrawIndexed();
    val.EndRenderPass();
    val.BindPipeline();
    val.ValidateDispatch();
    val.EndRecording();

    auto stats = val.GetStats();
    EXPECT_EQ(stats.drawCalls, 2u);
    EXPECT_EQ(stats.dispatchCalls, 1u);
    EXPECT_EQ(stats.renderPasses, 1u);
    EXPECT_EQ(stats.pipelineBinds, 2u);
    EXPECT_EQ(stats.descriptorBinds, 1u);
    EXPECT_EQ(stats.errors, 0u);
    EXPECT_GT(stats.totalCommands, 0u);

    val.Shutdown();
}

TEST(CmdBufferValidator, ResetState) {
    CmdBufferValidator val;
    val.Init();

    val.BeginRecording();
    val.BindPipeline();
    val.EndRecording();

    val.ResetState();

    EXPECT_EQ(val.GetState(), CmdBufferState::Initial);
    EXPECT_FALSE(val.IsPipelineBound());
    EXPECT_FALSE(val.HasErrors());

    auto stats = val.GetStats();
    EXPECT_EQ(stats.totalCommands, 0u);

    val.Shutdown();
}

TEST(CmdBufferValidator, ClearErrors) {
    CmdBufferValidator val;
    val.Init();

    val.EndRecording(); // Error
    EXPECT_TRUE(val.HasErrors());

    val.ClearErrors();
    EXPECT_FALSE(val.HasErrors());
    EXPECT_EQ(val.GetErrorCount(), 0u);

    val.Shutdown();
}

TEST(CmdBufferValidator, DisabledPassesAll) {
    CmdBufferValidator val;
    CmdBufferValidatorConfig config;
    config.enabled = false;
    val.Init(config);

    // All calls should pass when disabled
    EXPECT_TRUE(val.BeginRecording());
    EXPECT_TRUE(val.ValidateDraw()); // Would normally fail
    EXPECT_TRUE(val.ValidateDispatch());
    EXPECT_TRUE(val.EndRecording());
    EXPECT_FALSE(val.HasErrors());

    val.Shutdown();
}

TEST(CmdBufferValidator, GetErrorName) {
    EXPECT_STREQ(CmdBufferValidator::GetErrorName(CmdValidationError::None), "None");
    EXPECT_STREQ(CmdBufferValidator::GetErrorName(CmdValidationError::NoPipelineBound), "NoPipelineBound");
    EXPECT_STREQ(CmdBufferValidator::GetErrorName(CmdValidationError::DrawOutsideRenderPass), "DrawOutsideRenderPass");
    EXPECT_STREQ(CmdBufferValidator::GetErrorName(CmdValidationError::NestedRenderPass), "NestedRenderPass");
}

TEST(CmdBufferValidator, FullRecordingCycle) {
    CmdBufferValidator val;
    val.Init();

    // Full valid cycle
    EXPECT_TRUE(val.BeginRecording());

    // Compute dispatch
    val.BindPipeline();
    val.BindDescriptorSet();
    EXPECT_TRUE(val.ValidateDispatch());

    // Render pass with draws
    EXPECT_TRUE(val.BeginRenderPass());
    val.BindPipeline();
    val.BindVertexBuffer();
    val.BindDescriptorSet();
    EXPECT_TRUE(val.ValidateDraw());

    val.BindIndexBuffer();
    EXPECT_TRUE(val.ValidateDrawIndexed());
    EXPECT_TRUE(val.EndRenderPass());

    EXPECT_TRUE(val.EndRecording());
    EXPECT_EQ(val.GetState(), CmdBufferState::Executable);
    EXPECT_FALSE(val.HasErrors());

    val.Shutdown();
}

TEST(CmdBufferValidator, BeginRenderPassNotRecordingFails) {
    CmdBufferValidator val;
    val.Init();

    EXPECT_FALSE(val.BeginRenderPass()); // Not recording

    bool foundError = false;
    for (auto err : val.GetErrors()) {
        if (err == CmdValidationError::NotRecording) foundError = true;
    }
    EXPECT_TRUE(foundError);

    val.Shutdown();
}
