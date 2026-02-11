#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Draw Indirect Argument Validator ────────────────────────────────
// Validates multi-draw-indirect (MDI) arguments before GPU dispatch to
// catch invalid parameters that would cause GPU hangs, crashes, or
// undefined behavior.
//
// Use cases:
//   - Validate vertex/index counts against buffer bounds
//   - Detect zero-count draws (wasteful dispatches)
//   - Detect excessive instance counts (potential runaway)
//   - Validate first-vertex/first-index offsets
//   - Debug mode: log all draw args for replay analysis
//   - Pre-dispatch sanitization (clamp to safe values)

struct DrawIndirectArgs {
    u32 vertexCount;
    u32 instanceCount;
    u32 firstVertex;
    u32 firstInstance;
};

struct DrawIndexedIndirectArgs {
    u32 indexCount;
    u32 instanceCount;
    u32 firstIndex;
    i32 vertexOffset;
    u32 firstInstance;
};

struct DispatchIndirectArgs {
    u32 groupCountX;
    u32 groupCountY;
    u32 groupCountZ;
};

enum class ValidationIssue : u8 {
    ZeroVertexCount,
    ZeroIndexCount,
    ZeroInstanceCount,
    ZeroDispatchGroups,
    ExcessiveVertexCount,
    ExcessiveIndexCount,
    ExcessiveInstanceCount,
    ExcessiveDispatchGroups,
    VertexOutOfBounds,
    IndexOutOfBounds,
    NegativeVertexOffset,
};

struct DrawValidationError {
    ValidationIssue issue;
    u32             drawIndex;
    std::string     description;
};

struct DrawIndirectValidatorConfig {
    u32  maxVertexCount = 10000000;      // 10M vertices
    u32  maxIndexCount = 30000000;       // 30M indices
    u32  maxInstanceCount = 100000;      // 100K instances
    u32  maxDispatchGroupsPerDim = 65535;
    u32  maxTotalDispatchGroups = 65535u * 65535u;
    bool warnOnZeroCounts = true;
    bool clampToSafeValues = false;      // Auto-clamp instead of reject
    bool logAllDraws = false;            // Debug: log every draw call
    u32  maxErrors = 256;
};

struct DrawIndirectValidatorStats {
    u32 totalDrawsValidated;
    u32 totalIndexedDrawsValidated;
    u32 totalDispatchesValidated;
    u32 totalErrors;
    u32 zeroCountWarnings;
    u32 excessiveCountErrors;
    u32 outOfBoundsErrors;
    u32 drawsClamped;
};

class DrawIndirectValidator {
public:
    bool Init(const DrawIndirectValidatorConfig& config = {});
    void Shutdown();

    // Validate a single draw call
    bool ValidateDraw(const DrawIndirectArgs& args, u32 drawIndex = 0,
                       u32 vertexBufferSize = UINT32_MAX);

    // Validate a single indexed draw call
    bool ValidateDrawIndexed(const DrawIndexedIndirectArgs& args, u32 drawIndex = 0,
                              u32 indexBufferSize = UINT32_MAX, u32 vertexBufferSize = UINT32_MAX);

    // Validate a compute dispatch
    bool ValidateDispatch(const DispatchIndirectArgs& args, u32 dispatchIndex = 0);

    // Validate a buffer of draw args (MDI batch)
    u32 ValidateDrawBatch(const DrawIndirectArgs* args, u32 drawCount,
                           u32 vertexBufferSize = UINT32_MAX);

    // Validate a buffer of indexed draw args (MDI batch)
    u32 ValidateDrawIndexedBatch(const DrawIndexedIndirectArgs* args, u32 drawCount,
                                   u32 indexBufferSize = UINT32_MAX,
                                   u32 vertexBufferSize = UINT32_MAX);

    // Sanitize: clamp args to safe values in-place
    void SanitizeDraw(DrawIndirectArgs& args);
    void SanitizeDrawIndexed(DrawIndexedIndirectArgs& args);
    void SanitizeDispatch(DispatchIndirectArgs& args);

    // Get all recorded errors
    const std::vector<DrawValidationError>& GetErrors() const;

    // Clear errors
    void ClearErrors();

    void Reset();

    DrawIndirectValidatorStats GetStats() const;

private:
    void RecordError(ValidationIssue issue, u32 drawIndex, const std::string& desc);

    DrawIndirectValidatorConfig m_config;
    std::vector<DrawValidationError> m_errors;

    u32 m_totalDraws = 0;
    u32 m_totalIndexedDraws = 0;
    u32 m_totalDispatches = 0;
    u32 m_zeroCountWarnings = 0;
    u32 m_excessiveErrors = 0;
    u32 m_oobErrors = 0;
    u32 m_drawsClamped = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
