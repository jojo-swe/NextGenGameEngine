#include "engine/rhi/common/rhi_cmd_buffer_validator.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool CmdBufferValidator::Init(const CmdBufferValidatorConfig& config) {
    m_config = config;
    m_state = CmdBufferState::Initial;
    m_pipelineBound = false;
    m_vertexBufferBound = false;
    m_indexBufferBound = false;
    m_descriptorSetBound = false;
    m_totalCommands = 0;
    m_drawCalls = 0;
    m_dispatchCalls = 0;
    m_renderPasses = 0;
    m_pipelineBinds = 0;
    m_descriptorBinds = 0;

    NGE_LOG_INFO("Command buffer validator initialized: enabled={}, breakOnError={}, log={}",
                 config.enabled, config.breakOnError, config.logErrors);
    return true;
}

void CmdBufferValidator::Shutdown() {
    m_errors.clear();
}

bool CmdBufferValidator::BeginRecording() {
    std::lock_guard lock(m_mutex);

    if (!m_config.enabled) return true;

    if (m_state == CmdBufferState::Recording || m_state == CmdBufferState::InRenderPass) {
        return RecordError(CmdValidationError::AlreadyRecording);
    }

    m_state = CmdBufferState::Recording;
    m_pipelineBound = false;
    m_vertexBufferBound = false;
    m_indexBufferBound = false;
    m_descriptorSetBound = false;
    m_totalCommands++;

    return true;
}

bool CmdBufferValidator::EndRecording() {
    std::lock_guard lock(m_mutex);

    if (!m_config.enabled) return true;

    if (m_state == CmdBufferState::InRenderPass) {
        RecordError(CmdValidationError::EndRenderPassWithoutBegin);
        // Fall through to end recording anyway
    }

    if (m_state != CmdBufferState::Recording && m_state != CmdBufferState::InRenderPass) {
        return RecordError(CmdValidationError::EndWithoutBegin);
    }

    m_state = CmdBufferState::Executable;
    m_totalCommands++;

    return true;
}

bool CmdBufferValidator::BeginRenderPass() {
    std::lock_guard lock(m_mutex);

    if (!m_config.enabled) return true;

    if (m_state != CmdBufferState::Recording) {
        if (m_state == CmdBufferState::InRenderPass) {
            return RecordError(CmdValidationError::NestedRenderPass);
        }
        return RecordError(CmdValidationError::NotRecording);
    }

    m_state = CmdBufferState::InRenderPass;
    m_renderPasses++;
    m_pipelineBound = false;
    m_totalCommands++;

    return true;
}

bool CmdBufferValidator::EndRenderPass() {
    std::lock_guard lock(m_mutex);

    if (!m_config.enabled) return true;

    if (m_state != CmdBufferState::InRenderPass) {
        return RecordError(CmdValidationError::NotInRenderPass);
    }

    m_state = CmdBufferState::Recording;
    m_pipelineBound = false;
    m_totalCommands++;

    return true;
}

void CmdBufferValidator::BindPipeline() {
    std::lock_guard lock(m_mutex);

    m_pipelineBound = true;
    m_pipelineBinds++;
    m_totalCommands++;
}

void CmdBufferValidator::BindVertexBuffer() {
    std::lock_guard lock(m_mutex);

    m_vertexBufferBound = true;
    m_totalCommands++;
}

void CmdBufferValidator::BindIndexBuffer() {
    std::lock_guard lock(m_mutex);

    m_indexBufferBound = true;
    m_totalCommands++;
}

void CmdBufferValidator::BindDescriptorSet() {
    std::lock_guard lock(m_mutex);

    m_descriptorSetBound = true;
    m_descriptorBinds++;
    m_totalCommands++;
}

bool CmdBufferValidator::ValidateDraw() {
    std::lock_guard lock(m_mutex);

    if (!m_config.enabled) return true;

    bool valid = true;

    if (m_state != CmdBufferState::InRenderPass) {
        RecordError(CmdValidationError::DrawOutsideRenderPass);
        valid = false;
    }

    if (!m_pipelineBound) {
        RecordError(CmdValidationError::NoPipelineBound);
        valid = false;
    }

    if (!m_vertexBufferBound) {
        RecordError(CmdValidationError::NoVertexBufferBound);
        valid = false;
    }

    if (valid) {
        m_drawCalls++;
        m_totalCommands++;
    }

    return valid;
}

bool CmdBufferValidator::ValidateDrawIndexed() {
    std::lock_guard lock(m_mutex);

    if (!m_config.enabled) return true;

    bool valid = true;

    if (m_state != CmdBufferState::InRenderPass) {
        RecordError(CmdValidationError::DrawOutsideRenderPass);
        valid = false;
    }

    if (!m_pipelineBound) {
        RecordError(CmdValidationError::NoPipelineBound);
        valid = false;
    }

    if (!m_vertexBufferBound) {
        RecordError(CmdValidationError::NoVertexBufferBound);
        valid = false;
    }

    if (!m_indexBufferBound) {
        RecordError(CmdValidationError::NoIndexBufferBound);
        valid = false;
    }

    if (valid) {
        m_drawCalls++;
        m_totalCommands++;
    }

    return valid;
}

bool CmdBufferValidator::ValidateDispatch() {
    std::lock_guard lock(m_mutex);

    if (!m_config.enabled) return true;

    bool valid = true;

    if (m_state == CmdBufferState::InRenderPass) {
        RecordError(CmdValidationError::DispatchInsideRenderPass);
        valid = false;
    }

    if (m_state != CmdBufferState::Recording && m_state != CmdBufferState::InRenderPass) {
        RecordError(CmdValidationError::NotRecording);
        valid = false;
    }

    if (!m_pipelineBound) {
        RecordError(CmdValidationError::NoPipelineBound);
        valid = false;
    }

    if (valid) {
        m_dispatchCalls++;
        m_totalCommands++;
    }

    return valid;
}

CmdBufferState CmdBufferValidator::GetState() const {
    std::lock_guard lock(m_mutex);
    return m_state;
}

bool CmdBufferValidator::IsPipelineBound() const {
    std::lock_guard lock(m_mutex);
    return m_pipelineBound;
}

bool CmdBufferValidator::IsVertexBufferBound() const {
    std::lock_guard lock(m_mutex);
    return m_vertexBufferBound;
}

bool CmdBufferValidator::IsIndexBufferBound() const {
    std::lock_guard lock(m_mutex);
    return m_indexBufferBound;
}

bool CmdBufferValidator::IsDescriptorSetBound() const {
    std::lock_guard lock(m_mutex);
    return m_descriptorSetBound;
}

std::vector<CmdValidationError> CmdBufferValidator::GetErrors() const {
    std::lock_guard lock(m_mutex);
    return m_errors;
}

u32 CmdBufferValidator::GetErrorCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_errors.size());
}

bool CmdBufferValidator::HasErrors() const {
    std::lock_guard lock(m_mutex);
    return !m_errors.empty();
}

const char* CmdBufferValidator::GetErrorName(CmdValidationError err) {
    switch (err) {
        case CmdValidationError::None:                     return "None";
        case CmdValidationError::NotRecording:             return "NotRecording";
        case CmdValidationError::AlreadyRecording:         return "AlreadyRecording";
        case CmdValidationError::NotInRenderPass:          return "NotInRenderPass";
        case CmdValidationError::AlreadyInRenderPass:      return "AlreadyInRenderPass";
        case CmdValidationError::NoPipelineBound:          return "NoPipelineBound";
        case CmdValidationError::NoVertexBufferBound:      return "NoVertexBufferBound";
        case CmdValidationError::NoIndexBufferBound:       return "NoIndexBufferBound";
        case CmdValidationError::NoDescriptorSetBound:     return "NoDescriptorSetBound";
        case CmdValidationError::DrawOutsideRenderPass:    return "DrawOutsideRenderPass";
        case CmdValidationError::DispatchInsideRenderPass: return "DispatchInsideRenderPass";
        case CmdValidationError::EndWithoutBegin:          return "EndWithoutBegin";
        case CmdValidationError::EndRenderPassWithoutBegin:return "EndRenderPassWithoutBegin";
        case CmdValidationError::NestedRenderPass:         return "NestedRenderPass";
        case CmdValidationError::InvalidStateTransition:   return "InvalidStateTransition";
        default:                                           return "Unknown";
    }
}

void CmdBufferValidator::ResetState() {
    std::lock_guard lock(m_mutex);

    m_state = CmdBufferState::Initial;
    m_pipelineBound = false;
    m_vertexBufferBound = false;
    m_indexBufferBound = false;
    m_descriptorSetBound = false;
    m_errors.clear();
    m_totalCommands = 0;
    m_drawCalls = 0;
    m_dispatchCalls = 0;
    m_renderPasses = 0;
    m_pipelineBinds = 0;
    m_descriptorBinds = 0;
}

void CmdBufferValidator::ClearErrors() {
    std::lock_guard lock(m_mutex);
    m_errors.clear();
}

CmdBufferValidatorStats CmdBufferValidator::GetStats() const {
    std::lock_guard lock(m_mutex);

    CmdBufferValidatorStats stats{};
    stats.totalCommands = m_totalCommands;
    stats.drawCalls = m_drawCalls;
    stats.dispatchCalls = m_dispatchCalls;
    stats.renderPasses = m_renderPasses;
    stats.errors = static_cast<u32>(m_errors.size());
    stats.pipelineBinds = m_pipelineBinds;
    stats.descriptorBinds = m_descriptorBinds;

    return stats;
}

bool CmdBufferValidator::RecordError(CmdValidationError err) {
    if (m_errors.size() >= m_config.maxErrors) return false;

    m_errors.push_back(err);

    if (m_config.logErrors) {
        NGE_LOG_ERROR("CmdBuffer validation error: {}", GetErrorName(err));
    }

    return false;
}

} // namespace nge::rhi
