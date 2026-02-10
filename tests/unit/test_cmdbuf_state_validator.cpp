#include <gtest/gtest.h>
#include "engine/core/types.h"
#include "engine/rhi/common/rhi_cmdbuf_state_validator.h"

using namespace nge;
using namespace nge::rhi;

TEST(CmdBufStateValidator, InitAndShutdown) {
    CmdBufStateValidator validator;
    EXPECT_TRUE(validator.Init());

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDrawCalls, 0u);
    EXPECT_EQ(stats.totalErrors, 0u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, ValidDrawWithAllState) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BeginRenderPass();
    validator.BindPipeline(42);
    validator.SetViewport();
    validator.SetScissor();

    EXPECT_TRUE(validator.ValidateDraw());
    EXPECT_TRUE(validator.GetErrors().empty());

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDrawCalls, 1u);
    EXPECT_EQ(stats.totalErrors, 0u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DrawMissingPipeline) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BeginRenderPass();
    validator.SetViewport();
    validator.SetScissor();
    // No pipeline bound

    EXPECT_FALSE(validator.ValidateDraw());
    EXPECT_EQ(validator.GetErrors().size(), 1u);
    EXPECT_NE(validator.GetErrors()[0].message.find("Pipeline"), std::string::npos);

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.missingPipelineErrors, 1u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DrawMissingRenderPass) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BindPipeline(1);
    validator.SetViewport();
    validator.SetScissor();
    // No render pass begun

    EXPECT_FALSE(validator.ValidateDraw());
    EXPECT_EQ(validator.GetErrors().size(), 1u);

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.missingRenderPassErrors, 1u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DrawMissingViewportAndScissor) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BeginRenderPass();
    validator.BindPipeline(1);
    // No viewport or scissor

    EXPECT_FALSE(validator.ValidateDraw());
    EXPECT_EQ(validator.GetErrors().size(), 1u);

    auto& err = validator.GetErrors()[0];
    EXPECT_NE(err.message.find("Viewport"), std::string::npos);
    EXPECT_NE(err.message.find("Scissor"), std::string::npos);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DrawIndexedMissingBuffers) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BeginRenderPass();
    validator.BindPipeline(1);
    validator.SetViewport();
    validator.SetScissor();
    // No vertex or index buffer

    EXPECT_FALSE(validator.ValidateDrawIndexed());
    EXPECT_EQ(validator.GetErrors().size(), 1u);

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.missingVertexBufferErrors, 1u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, ValidDrawIndexed) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BeginRenderPass();
    validator.BindPipeline(1);
    validator.SetViewport();
    validator.SetScissor();
    validator.BindVertexBuffer(0);
    validator.BindIndexBuffer();

    EXPECT_TRUE(validator.ValidateDrawIndexed());
    EXPECT_TRUE(validator.GetErrors().empty());

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DispatchOnlyNeedsPipeline) {
    CmdBufStateValidator validator;
    validator.Init();

    // Dispatch doesn't need render pass, viewport, etc.
    validator.BindPipeline(99);
    EXPECT_TRUE(validator.ValidateDispatch());

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDispatches, 1u);
    EXPECT_EQ(stats.totalErrors, 0u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DispatchMissingPipeline) {
    CmdBufStateValidator validator;
    validator.Init();

    EXPECT_FALSE(validator.ValidateDispatch());
    EXPECT_EQ(validator.GetErrors().size(), 1u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, EndRenderPassClearsFlag) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BeginRenderPass();
    EXPECT_TRUE(validator.IsInRenderPass());

    validator.EndRenderPass();
    EXPECT_FALSE(validator.IsInRenderPass());

    validator.Shutdown();
}

TEST(CmdBufStateValidator, BindPipelineTracked) {
    CmdBufStateValidator validator;
    validator.Init();

    EXPECT_FALSE(validator.HasPipelineBound());
    EXPECT_EQ(validator.GetBoundPipeline(), 0u);

    validator.BindPipeline(42);
    EXPECT_TRUE(validator.HasPipelineBound());
    EXPECT_EQ(validator.GetBoundPipeline(), 42u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DescriptorSetBinding) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BindDescriptorSet(0);
    validator.BindDescriptorSet(2);

    u32 state = validator.GetBoundState();
    EXPECT_TRUE(HasState(state, BoundStateFlag::DescriptorSet0));
    EXPECT_FALSE(HasState(state, BoundStateFlag::DescriptorSet1));
    EXPECT_TRUE(HasState(state, BoundStateFlag::DescriptorSet2));
    EXPECT_FALSE(HasState(state, BoundStateFlag::DescriptorSet3));

    validator.Shutdown();
}

TEST(CmdBufStateValidator, DynamicStateFlagsTracked) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.PushConstants(0, 64);
    validator.SetBlendConstants();
    validator.SetDepthBias();
    validator.SetStencilReference();

    u32 state = validator.GetBoundState();
    EXPECT_TRUE(HasState(state, BoundStateFlag::PushConstants));
    EXPECT_TRUE(HasState(state, BoundStateFlag::BlendConstants));
    EXPECT_TRUE(HasState(state, BoundStateFlag::DepthBias));
    EXPECT_TRUE(HasState(state, BoundStateFlag::StencilReference));

    validator.Shutdown();
}

TEST(CmdBufStateValidator, ResetClearsAllState) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.BeginRenderPass();
    validator.BindPipeline(1);
    validator.SetViewport();
    validator.SetScissor();

    // Trigger an error
    validator.EndRenderPass();
    validator.ValidateDraw(); // Missing render pass

    EXPECT_FALSE(validator.GetErrors().empty());

    validator.Reset();

    EXPECT_EQ(validator.GetBoundState(), 0u);
    EXPECT_EQ(validator.GetBoundPipeline(), 0u);
    EXPECT_FALSE(validator.IsInRenderPass());
    EXPECT_TRUE(validator.GetErrors().empty());

    validator.Shutdown();
}

TEST(CmdBufStateValidator, MultipleErrors) {
    CmdBufStateValidator validator;
    validator.Init();

    // Draw with nothing bound
    EXPECT_FALSE(validator.ValidateDraw());
    EXPECT_FALSE(validator.ValidateDraw());
    EXPECT_FALSE(validator.ValidateDrawIndexed());

    EXPECT_EQ(validator.GetErrors().size(), 3u);

    auto stats = validator.GetStats();
    EXPECT_EQ(stats.totalDrawCalls, 3u);
    EXPECT_EQ(stats.totalErrors, 3u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, MaxErrorsLimit) {
    CmdBufStateValidator validator;
    CmdBufStateValidatorConfig config;
    config.maxErrors = 3;
    validator.Init(config);

    for (int i = 0; i < 10; ++i) {
        validator.ValidateDraw(); // All fail
    }

    EXPECT_EQ(validator.GetErrors().size(), 3u);

    validator.Shutdown();
}

TEST(CmdBufStateValidator, CustomRequiredFlags) {
    CmdBufStateValidator validator;
    CmdBufStateValidatorConfig config;
    // Require descriptor set 0 for dispatch
    config.requiredForDispatch = static_cast<u32>(BoundStateFlag::Pipeline) |
                                  static_cast<u32>(BoundStateFlag::DescriptorSet0);
    validator.Init(config);

    validator.BindPipeline(1);
    // Missing descriptor set 0
    EXPECT_FALSE(validator.ValidateDispatch());

    validator.BindDescriptorSet(0);
    EXPECT_TRUE(validator.ValidateDispatch());

    validator.Shutdown();
}

TEST(CmdBufStateValidator, ErrorContainsDrawCallIndex) {
    CmdBufStateValidator validator;
    validator.Init();

    validator.ValidateDraw(); // Error at draw call 1
    validator.BeginRenderPass();
    validator.BindPipeline(1);
    validator.SetViewport();
    validator.SetScissor();
    validator.ValidateDraw(); // OK at draw call 2

    validator.EndRenderPass();
    validator.ValidateDraw(); // Error at draw call 3

    EXPECT_EQ(validator.GetErrors().size(), 2u);
    EXPECT_EQ(validator.GetErrors()[0].drawCallIndex, 1u);
    EXPECT_EQ(validator.GetErrors()[1].drawCallIndex, 3u);

    validator.Shutdown();
}
