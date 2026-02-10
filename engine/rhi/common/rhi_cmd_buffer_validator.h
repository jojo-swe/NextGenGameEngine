#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Command Buffer State Validator ──────────────────────────────────
// Validates command buffer recording state machine transitions. Ensures
// commands are recorded in the correct order and that required state
// (pipeline, descriptor sets, vertex buffers) is bound before draw/dispatch.
//
// Use cases:
//   - Validate begin/end recording pairs
//   - Validate begin/end render pass pairs
//   - Ensure pipeline is bound before draw/dispatch
//   - Ensure vertex buffers bound before indexed draw
//   - Ensure descriptor sets bound before draw/dispatch
//   - Track and report recording errors
//   - Debug layer for command buffer misuse

enum class CmdBufferState : u8 {
    Initial,        // Created, not yet recording
    Recording,      // Between Begin and End
    InRenderPass,   // Between BeginRenderPass and EndRenderPass
    Executable,     // End called, ready to submit
};

enum class CmdValidationError : u8 {
    None,
    NotRecording,
    AlreadyRecording,
    NotInRenderPass,
    AlreadyInRenderPass,
    NoPipelineBound,
    NoVertexBufferBound,
    NoIndexBufferBound,
    NoDescriptorSetBound,
    DrawOutsideRenderPass,
    DispatchInsideRenderPass,
    EndWithoutBegin,
    EndRenderPassWithoutBegin,
    NestedRenderPass,
    InvalidStateTransition,
};

struct CmdBufferValidatorConfig {
    bool enabled = true;
    bool breakOnError = false;      // Trigger breakpoint on error (debug)
    bool logErrors = true;
    u32  maxErrors = 256;
};

struct CmdBufferValidatorStats {
    u32 totalCommands;
    u32 drawCalls;
    u32 dispatchCalls;
    u32 renderPasses;
    u32 errors;
    u32 pipelineBinds;
    u32 descriptorBinds;
};

class CmdBufferValidator {
public:
    bool Init(const CmdBufferValidatorConfig& config = {});
    void Shutdown();

    // State transitions
    bool BeginRecording();
    bool EndRecording();
    bool BeginRenderPass();
    bool EndRenderPass();

    // Bind commands
    void BindPipeline();
    void BindVertexBuffer();
    void BindIndexBuffer();
    void BindDescriptorSet();

    // Draw/dispatch validation
    bool ValidateDraw();
    bool ValidateDrawIndexed();
    bool ValidateDispatch();

    // Query state
    CmdBufferState GetState() const;
    bool IsPipelineBound() const;
    bool IsVertexBufferBound() const;
    bool IsIndexBufferBound() const;
    bool IsDescriptorSetBound() const;

    // Error access
    std::vector<CmdValidationError> GetErrors() const;
    u32 GetErrorCount() const;
    bool HasErrors() const;

    // Get error name
    static const char* GetErrorName(CmdValidationError err);

    void ResetState();   // Reset to Initial
    void ClearErrors();

    CmdBufferValidatorStats GetStats() const;

private:
    bool RecordError(CmdValidationError err);

    CmdBufferValidatorConfig m_config;

    CmdBufferState m_state = CmdBufferState::Initial;
    bool m_pipelineBound = false;
    bool m_vertexBufferBound = false;
    bool m_indexBufferBound = false;
    bool m_descriptorSetBound = false;

    std::vector<CmdValidationError> m_errors;

    u32 m_totalCommands = 0;
    u32 m_drawCalls = 0;
    u32 m_dispatchCalls = 0;
    u32 m_renderPasses = 0;
    u32 m_pipelineBinds = 0;
    u32 m_descriptorBinds = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
