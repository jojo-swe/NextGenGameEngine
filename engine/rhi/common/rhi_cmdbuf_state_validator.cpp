#include "engine/rhi/common/rhi_cmdbuf_state_validator.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool CmdBufStateValidator::Init(const CmdBufStateValidatorConfig& config) {
    m_config = config;
    m_boundState = 0;
    m_boundPipeline = 0;
    m_drawCallIndex = 0;
    m_totalDrawCalls = 0;
    m_totalDispatches = 0;
    m_missingPipeline = 0;
    m_missingRenderPass = 0;
    m_missingVertexBuffer = 0;
    m_missingDescriptor = 0;
    m_missingViewport = 0;

    NGE_LOG_INFO("Command buffer state validator initialized: breakOnError={}, maxErrors={}",
                 config.breakOnError, config.maxErrors);
    return true;
}

void CmdBufStateValidator::Shutdown() {
    m_errors.clear();
}

void CmdBufStateValidator::BeginRenderPass() {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::RenderPass);
}

void CmdBufStateValidator::EndRenderPass() {
    std::lock_guard lock(m_mutex);
    m_boundState &= ~static_cast<u32>(BoundStateFlag::RenderPass);
}

void CmdBufStateValidator::BindPipeline(u64 psoHandle) {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::Pipeline);
    m_boundPipeline = psoHandle;
}

void CmdBufStateValidator::BindVertexBuffer(u32 binding) {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::VertexBuffer);
}

void CmdBufStateValidator::BindIndexBuffer() {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::IndexBuffer);
}

void CmdBufStateValidator::BindDescriptorSet(u32 setIndex) {
    std::lock_guard lock(m_mutex);
    switch (setIndex) {
        case 0: m_boundState |= static_cast<u32>(BoundStateFlag::DescriptorSet0); break;
        case 1: m_boundState |= static_cast<u32>(BoundStateFlag::DescriptorSet1); break;
        case 2: m_boundState |= static_cast<u32>(BoundStateFlag::DescriptorSet2); break;
        case 3: m_boundState |= static_cast<u32>(BoundStateFlag::DescriptorSet3); break;
        default: break;
    }
}

void CmdBufStateValidator::SetViewport() {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::Viewport);
}

void CmdBufStateValidator::SetScissor() {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::Scissor);
}

void CmdBufStateValidator::PushConstants(u32 offset, u32 size) {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::PushConstants);
}

void CmdBufStateValidator::SetBlendConstants() {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::BlendConstants);
}

void CmdBufStateValidator::SetDepthBias() {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::DepthBias);
}

void CmdBufStateValidator::SetStencilReference() {
    std::lock_guard lock(m_mutex);
    m_boundState |= static_cast<u32>(BoundStateFlag::StencilReference);
}

bool CmdBufStateValidator::ValidateDraw() {
    std::lock_guard lock(m_mutex);
    m_totalDrawCalls++;
    m_drawCallIndex++;
    return ValidateRequired(m_config.requiredForDraw, "Draw");
}

bool CmdBufStateValidator::ValidateDrawIndexed() {
    std::lock_guard lock(m_mutex);
    m_totalDrawCalls++;
    m_drawCallIndex++;
    return ValidateRequired(m_config.requiredForIndexedDraw, "DrawIndexed");
}

bool CmdBufStateValidator::ValidateDispatch() {
    std::lock_guard lock(m_mutex);
    m_totalDispatches++;
    m_drawCallIndex++;
    return ValidateRequired(m_config.requiredForDispatch, "Dispatch");
}

bool CmdBufStateValidator::IsInRenderPass() const {
    std::lock_guard lock(m_mutex);
    return HasState(m_boundState, BoundStateFlag::RenderPass);
}

bool CmdBufStateValidator::HasPipelineBound() const {
    std::lock_guard lock(m_mutex);
    return HasState(m_boundState, BoundStateFlag::Pipeline);
}

u32 CmdBufStateValidator::GetBoundState() const {
    std::lock_guard lock(m_mutex);
    return m_boundState;
}

u64 CmdBufStateValidator::GetBoundPipeline() const {
    std::lock_guard lock(m_mutex);
    return m_boundPipeline;
}

const std::vector<ValidationError>& CmdBufStateValidator::GetErrors() const {
    return m_errors;
}

void CmdBufStateValidator::Reset() {
    std::lock_guard lock(m_mutex);
    m_boundState = 0;
    m_boundPipeline = 0;
    m_drawCallIndex = 0;
    m_errors.clear();
}

CmdBufStateValidatorStats CmdBufStateValidator::GetStats() const {
    std::lock_guard lock(m_mutex);
    CmdBufStateValidatorStats stats{};
    stats.totalDrawCalls = m_totalDrawCalls;
    stats.totalDispatches = m_totalDispatches;
    stats.totalErrors = static_cast<u32>(m_errors.size());
    stats.missingPipelineErrors = m_missingPipeline;
    stats.missingRenderPassErrors = m_missingRenderPass;
    stats.missingVertexBufferErrors = m_missingVertexBuffer;
    stats.missingDescriptorErrors = m_missingDescriptor;
    stats.missingViewportErrors = m_missingViewport;
    return stats;
}

bool CmdBufStateValidator::ValidateRequired(u32 requiredMask, const std::string& context) {
    u32 missing = requiredMask & ~m_boundState;
    if (missing == 0) return true;

    if (m_errors.size() >= m_config.maxErrors) return false;

    // Track specific categories
    if (missing & static_cast<u32>(BoundStateFlag::Pipeline)) m_missingPipeline++;
    if (missing & static_cast<u32>(BoundStateFlag::RenderPass)) m_missingRenderPass++;
    if (missing & static_cast<u32>(BoundStateFlag::VertexBuffer)) m_missingVertexBuffer++;
    if (missing & static_cast<u32>(BoundStateFlag::Viewport)) m_missingViewport++;
    if (missing & (static_cast<u32>(BoundStateFlag::DescriptorSet0) |
                   static_cast<u32>(BoundStateFlag::DescriptorSet1) |
                   static_cast<u32>(BoundStateFlag::DescriptorSet2) |
                   static_cast<u32>(BoundStateFlag::DescriptorSet3))) {
        m_missingDescriptor++;
    }

    ValidationError err;
    err.drawCallIndex = m_drawCallIndex;
    err.missingFlags = missing;
    err.message = context + " #" + std::to_string(m_drawCallIndex) +
                  ": missing " + DescribeMissing(missing);

    if (m_config.logErrors) {
        NGE_LOG_ERROR("CmdBuf validation: {}", err.message);
    }

    m_errors.push_back(std::move(err));
    return false;
}

std::string CmdBufStateValidator::DescribeMissing(u32 missingFlags) const {
    std::string desc;
    auto append = [&](BoundStateFlag flag, const char* name) {
        if (missingFlags & static_cast<u32>(flag)) {
            if (!desc.empty()) desc += ", ";
            desc += name;
        }
    };

    append(BoundStateFlag::Pipeline, "Pipeline");
    append(BoundStateFlag::VertexBuffer, "VertexBuffer");
    append(BoundStateFlag::IndexBuffer, "IndexBuffer");
    append(BoundStateFlag::DescriptorSet0, "DescriptorSet0");
    append(BoundStateFlag::DescriptorSet1, "DescriptorSet1");
    append(BoundStateFlag::DescriptorSet2, "DescriptorSet2");
    append(BoundStateFlag::DescriptorSet3, "DescriptorSet3");
    append(BoundStateFlag::RenderPass, "RenderPass");
    append(BoundStateFlag::Viewport, "Viewport");
    append(BoundStateFlag::Scissor, "Scissor");
    append(BoundStateFlag::PushConstants, "PushConstants");
    append(BoundStateFlag::BlendConstants, "BlendConstants");
    append(BoundStateFlag::DepthBias, "DepthBias");
    append(BoundStateFlag::StencilReference, "StencilReference");

    return desc;
}

} // namespace nge::rhi
