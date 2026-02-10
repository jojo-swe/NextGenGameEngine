#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>

namespace nge::rhi {

// ─── GPU Command Buffer State Validator ──────────────────────────────────
// Tracks currently bound pipeline state during command buffer recording
// and validates that all required bindings are present before draw/dispatch.
//
// Use cases:
//   - Detect missing pipeline bind before draw
//   - Detect missing descriptor set bindings
//   - Detect missing vertex/index buffer binds
//   - Detect missing render pass begin
//   - Detect push constant overflows
//   - Debug validation layer complement (catches logical errors)

enum class BoundStateFlag : u32 {
    Pipeline            = 0x0001,
    VertexBuffer        = 0x0002,
    IndexBuffer         = 0x0004,
    DescriptorSet0      = 0x0008,
    DescriptorSet1      = 0x0010,
    DescriptorSet2      = 0x0020,
    DescriptorSet3      = 0x0040,
    RenderPass          = 0x0080,
    Viewport            = 0x0100,
    Scissor             = 0x0200,
    PushConstants       = 0x0400,
    BlendConstants      = 0x0800,
    DepthBias           = 0x1000,
    StencilReference    = 0x2000,
};

inline BoundStateFlag operator|(BoundStateFlag a, BoundStateFlag b) {
    return static_cast<BoundStateFlag>(static_cast<u32>(a) | static_cast<u32>(b));
}

inline bool HasState(u32 mask, BoundStateFlag flag) {
    return (mask & static_cast<u32>(flag)) != 0;
}

struct ValidationError {
    std::string message;
    u32         drawCallIndex;
    u32         missingFlags;   // Bitmask of missing BoundStateFlags
};

struct CmdBufStateValidatorConfig {
    u32  requiredForDraw = static_cast<u32>(BoundStateFlag::Pipeline) |
                           static_cast<u32>(BoundStateFlag::RenderPass) |
                           static_cast<u32>(BoundStateFlag::Viewport) |
                           static_cast<u32>(BoundStateFlag::Scissor);
    u32  requiredForIndexedDraw = static_cast<u32>(BoundStateFlag::Pipeline) |
                                   static_cast<u32>(BoundStateFlag::RenderPass) |
                                   static_cast<u32>(BoundStateFlag::Viewport) |
                                   static_cast<u32>(BoundStateFlag::Scissor) |
                                   static_cast<u32>(BoundStateFlag::VertexBuffer) |
                                   static_cast<u32>(BoundStateFlag::IndexBuffer);
    u32  requiredForDispatch = static_cast<u32>(BoundStateFlag::Pipeline);
    bool breakOnError = false;
    bool logErrors = true;
    u32  maxErrors = 256;
};

struct CmdBufStateValidatorStats {
    u32 totalDrawCalls;
    u32 totalDispatches;
    u32 totalErrors;
    u32 missingPipelineErrors;
    u32 missingRenderPassErrors;
    u32 missingVertexBufferErrors;
    u32 missingDescriptorErrors;
    u32 missingViewportErrors;
};

class CmdBufStateValidator {
public:
    bool Init(const CmdBufStateValidatorConfig& config = {});
    void Shutdown();

    // ── State tracking (call during command recording) ───────────────
    void BeginRenderPass();
    void EndRenderPass();
    void BindPipeline(u64 psoHandle);
    void BindVertexBuffer(u32 binding);
    void BindIndexBuffer();
    void BindDescriptorSet(u32 setIndex);
    void SetViewport();
    void SetScissor();
    void PushConstants(u32 offset, u32 size);
    void SetBlendConstants();
    void SetDepthBias();
    void SetStencilReference();

    // ── Validation ───────────────────────────────────────────────────
    bool ValidateDraw();
    bool ValidateDrawIndexed();
    bool ValidateDispatch();

    // ── Query ────────────────────────────────────────────────────────
    bool IsInRenderPass() const;
    bool HasPipelineBound() const;
    u32  GetBoundState() const;
    u64  GetBoundPipeline() const;

    // Get all recorded errors
    const std::vector<ValidationError>& GetErrors() const;

    // Reset state (new command buffer)
    void Reset();

    CmdBufStateValidatorStats GetStats() const;

private:
    bool ValidateRequired(u32 requiredMask, const std::string& context);
    std::string DescribeMissing(u32 missingFlags) const;

    CmdBufStateValidatorConfig m_config;
    u32  m_boundState = 0;
    u64  m_boundPipeline = 0;
    u32  m_drawCallIndex = 0;

    std::vector<ValidationError> m_errors;

    u32 m_totalDrawCalls = 0;
    u32 m_totalDispatches = 0;
    u32 m_missingPipeline = 0;
    u32 m_missingRenderPass = 0;
    u32 m_missingVertexBuffer = 0;
    u32 m_missingDescriptor = 0;
    u32 m_missingViewport = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
