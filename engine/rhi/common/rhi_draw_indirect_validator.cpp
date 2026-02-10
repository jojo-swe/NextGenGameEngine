#include "engine/rhi/common/rhi_draw_indirect_validator.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

bool DrawIndirectValidator::Init(const DrawIndirectValidatorConfig& config) {
    m_config = config;
    m_errors.reserve(config.maxErrors);
    m_totalDraws = 0;
    m_totalIndexedDraws = 0;
    m_totalDispatches = 0;
    m_zeroCountWarnings = 0;
    m_excessiveErrors = 0;
    m_oobErrors = 0;
    m_drawsClamped = 0;

    NGE_LOG_INFO("Draw indirect validator initialized: maxVtx={}, maxIdx={}, maxInst={}, clamp={}",
                 config.maxVertexCount, config.maxIndexCount, config.maxInstanceCount, config.clampToSafeValues);
    return true;
}

void DrawIndirectValidator::Shutdown() {
    m_errors.clear();
}

bool DrawIndirectValidator::ValidateDraw(const DrawIndirectArgs& args, u32 drawIndex,
                                           u32 vertexBufferSize) {
    std::lock_guard lock(m_mutex);
    m_totalDraws++;
    bool valid = true;

    if (m_config.logAllDraws) {
        NGE_LOG_DEBUG("Draw[{}]: vtx={} inst={} firstVtx={} firstInst={}",
                      drawIndex, args.vertexCount, args.instanceCount, args.firstVertex, args.firstInstance);
    }

    // Zero count checks
    if (args.vertexCount == 0 && m_config.warnOnZeroCounts) {
        RecordError(ValidationIssue::ZeroVertexCount, drawIndex, "vertexCount is 0");
        m_zeroCountWarnings++;
        valid = false;
    }

    if (args.instanceCount == 0 && m_config.warnOnZeroCounts) {
        RecordError(ValidationIssue::ZeroInstanceCount, drawIndex, "instanceCount is 0");
        m_zeroCountWarnings++;
        valid = false;
    }

    // Excessive count checks
    if (args.vertexCount > m_config.maxVertexCount) {
        RecordError(ValidationIssue::ExcessiveVertexCount, drawIndex,
                    "vertexCount " + std::to_string(args.vertexCount) + " exceeds max " + std::to_string(m_config.maxVertexCount));
        m_excessiveErrors++;
        valid = false;
    }

    if (args.instanceCount > m_config.maxInstanceCount) {
        RecordError(ValidationIssue::ExcessiveInstanceCount, drawIndex,
                    "instanceCount " + std::to_string(args.instanceCount) + " exceeds max " + std::to_string(m_config.maxInstanceCount));
        m_excessiveErrors++;
        valid = false;
    }

    // Out-of-bounds check
    if (vertexBufferSize != UINT32_MAX) {
        u64 lastVertex = static_cast<u64>(args.firstVertex) + args.vertexCount;
        if (lastVertex > vertexBufferSize) {
            RecordError(ValidationIssue::VertexOutOfBounds, drawIndex,
                        "firstVertex(" + std::to_string(args.firstVertex) + ") + vertexCount(" +
                        std::to_string(args.vertexCount) + ") > bufferSize(" + std::to_string(vertexBufferSize) + ")");
            m_oobErrors++;
            valid = false;
        }
    }

    return valid;
}

bool DrawIndirectValidator::ValidateDrawIndexed(const DrawIndexedIndirectArgs& args, u32 drawIndex,
                                                  u32 indexBufferSize, u32 vertexBufferSize) {
    std::lock_guard lock(m_mutex);
    m_totalIndexedDraws++;
    bool valid = true;

    if (m_config.logAllDraws) {
        NGE_LOG_DEBUG("DrawIndexed[{}]: idx={} inst={} firstIdx={} vtxOff={} firstInst={}",
                      drawIndex, args.indexCount, args.instanceCount, args.firstIndex,
                      args.vertexOffset, args.firstInstance);
    }

    // Zero count checks
    if (args.indexCount == 0 && m_config.warnOnZeroCounts) {
        RecordError(ValidationIssue::ZeroIndexCount, drawIndex, "indexCount is 0");
        m_zeroCountWarnings++;
        valid = false;
    }

    if (args.instanceCount == 0 && m_config.warnOnZeroCounts) {
        RecordError(ValidationIssue::ZeroInstanceCount, drawIndex, "instanceCount is 0");
        m_zeroCountWarnings++;
        valid = false;
    }

    // Excessive count checks
    if (args.indexCount > m_config.maxIndexCount) {
        RecordError(ValidationIssue::ExcessiveIndexCount, drawIndex,
                    "indexCount " + std::to_string(args.indexCount) + " exceeds max " + std::to_string(m_config.maxIndexCount));
        m_excessiveErrors++;
        valid = false;
    }

    if (args.instanceCount > m_config.maxInstanceCount) {
        RecordError(ValidationIssue::ExcessiveInstanceCount, drawIndex,
                    "instanceCount " + std::to_string(args.instanceCount) + " exceeds max " + std::to_string(m_config.maxInstanceCount));
        m_excessiveErrors++;
        valid = false;
    }

    // Index buffer out-of-bounds
    if (indexBufferSize != UINT32_MAX) {
        u64 lastIndex = static_cast<u64>(args.firstIndex) + args.indexCount;
        if (lastIndex > indexBufferSize) {
            RecordError(ValidationIssue::IndexOutOfBounds, drawIndex,
                        "firstIndex(" + std::to_string(args.firstIndex) + ") + indexCount(" +
                        std::to_string(args.indexCount) + ") > indexBufferSize(" + std::to_string(indexBufferSize) + ")");
            m_oobErrors++;
            valid = false;
        }
    }

    // Negative vertex offset warning
    if (args.vertexOffset < 0) {
        RecordError(ValidationIssue::NegativeVertexOffset, drawIndex,
                    "vertexOffset is negative: " + std::to_string(args.vertexOffset));
        valid = false;
    }

    return valid;
}

bool DrawIndirectValidator::ValidateDispatch(const DispatchIndirectArgs& args, u32 dispatchIndex) {
    std::lock_guard lock(m_mutex);
    m_totalDispatches++;
    bool valid = true;

    if (m_config.logAllDraws) {
        NGE_LOG_DEBUG("Dispatch[{}]: groups=({},{},{})", dispatchIndex,
                      args.groupCountX, args.groupCountY, args.groupCountZ);
    }

    // Zero groups
    if ((args.groupCountX == 0 || args.groupCountY == 0 || args.groupCountZ == 0) &&
        m_config.warnOnZeroCounts) {
        RecordError(ValidationIssue::ZeroDispatchGroups, dispatchIndex, "dispatch has zero group dimension");
        m_zeroCountWarnings++;
        valid = false;
    }

    // Excessive groups
    if (args.groupCountX > m_config.maxDispatchGroupsPerDim ||
        args.groupCountY > m_config.maxDispatchGroupsPerDim ||
        args.groupCountZ > m_config.maxDispatchGroupsPerDim) {
        RecordError(ValidationIssue::ExcessiveDispatchGroups, dispatchIndex,
                    "dispatch groups (" + std::to_string(args.groupCountX) + "," +
                    std::to_string(args.groupCountY) + "," + std::to_string(args.groupCountZ) +
                    ") exceed max " + std::to_string(m_config.maxDispatchGroupsPerDim));
        m_excessiveErrors++;
        valid = false;
    }

    u64 totalGroups = static_cast<u64>(args.groupCountX) * args.groupCountY * args.groupCountZ;
    if (totalGroups > m_config.maxTotalDispatchGroups) {
        RecordError(ValidationIssue::ExcessiveDispatchGroups, dispatchIndex,
                    "total dispatch groups " + std::to_string(totalGroups) + " exceed max " +
                    std::to_string(m_config.maxTotalDispatchGroups));
        m_excessiveErrors++;
        valid = false;
    }

    return valid;
}

u32 DrawIndirectValidator::ValidateDrawBatch(const DrawIndirectArgs* args, u32 drawCount,
                                               u32 vertexBufferSize) {
    u32 invalidCount = 0;
    for (u32 i = 0; i < drawCount; ++i) {
        if (!ValidateDraw(args[i], i, vertexBufferSize)) {
            invalidCount++;
        }
    }
    return invalidCount;
}

u32 DrawIndirectValidator::ValidateDrawIndexedBatch(const DrawIndexedIndirectArgs* args, u32 drawCount,
                                                       u32 indexBufferSize, u32 vertexBufferSize) {
    u32 invalidCount = 0;
    for (u32 i = 0; i < drawCount; ++i) {
        if (!ValidateDrawIndexed(args[i], i, indexBufferSize, vertexBufferSize)) {
            invalidCount++;
        }
    }
    return invalidCount;
}

void DrawIndirectValidator::SanitizeDraw(DrawIndirectArgs& args) {
    std::lock_guard lock(m_mutex);

    if (args.vertexCount > m_config.maxVertexCount) {
        args.vertexCount = m_config.maxVertexCount;
        m_drawsClamped++;
    }
    if (args.instanceCount > m_config.maxInstanceCount) {
        args.instanceCount = m_config.maxInstanceCount;
        m_drawsClamped++;
    }
}

void DrawIndirectValidator::SanitizeDrawIndexed(DrawIndexedIndirectArgs& args) {
    std::lock_guard lock(m_mutex);

    if (args.indexCount > m_config.maxIndexCount) {
        args.indexCount = m_config.maxIndexCount;
        m_drawsClamped++;
    }
    if (args.instanceCount > m_config.maxInstanceCount) {
        args.instanceCount = m_config.maxInstanceCount;
        m_drawsClamped++;
    }
}

void DrawIndirectValidator::SanitizeDispatch(DispatchIndirectArgs& args) {
    std::lock_guard lock(m_mutex);

    args.groupCountX = std::min(args.groupCountX, m_config.maxDispatchGroupsPerDim);
    args.groupCountY = std::min(args.groupCountY, m_config.maxDispatchGroupsPerDim);
    args.groupCountZ = std::min(args.groupCountZ, m_config.maxDispatchGroupsPerDim);
    m_drawsClamped++;
}

const std::vector<DrawValidationError>& DrawIndirectValidator::GetErrors() const {
    return m_errors;
}

void DrawIndirectValidator::ClearErrors() {
    std::lock_guard lock(m_mutex);
    m_errors.clear();
}

void DrawIndirectValidator::Reset() {
    std::lock_guard lock(m_mutex);
    m_errors.clear();
    m_totalDraws = 0;
    m_totalIndexedDraws = 0;
    m_totalDispatches = 0;
    m_zeroCountWarnings = 0;
    m_excessiveErrors = 0;
    m_oobErrors = 0;
    m_drawsClamped = 0;
}

DrawIndirectValidatorStats DrawIndirectValidator::GetStats() const {
    std::lock_guard lock(m_mutex);
    DrawIndirectValidatorStats stats{};
    stats.totalDrawsValidated = m_totalDraws;
    stats.totalIndexedDrawsValidated = m_totalIndexedDraws;
    stats.totalDispatchesValidated = m_totalDispatches;
    stats.totalErrors = static_cast<u32>(m_errors.size());
    stats.zeroCountWarnings = m_zeroCountWarnings;
    stats.excessiveCountErrors = m_excessiveErrors;
    stats.outOfBoundsErrors = m_oobErrors;
    stats.drawsClamped = m_drawsClamped;
    return stats;
}

void DrawIndirectValidator::RecordError(ValidationIssue issue, u32 drawIndex, const std::string& desc) {
    if (m_errors.size() >= m_config.maxErrors) return;

    DrawValidationError error;
    error.issue = issue;
    error.drawIndex = drawIndex;
    error.description = desc;
    m_errors.push_back(std::move(error));

    NGE_LOG_WARN("Draw validation error [{}]: {}", drawIndex, desc);
}

} // namespace nge::rhi
