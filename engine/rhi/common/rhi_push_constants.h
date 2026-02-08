#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>

namespace nge::rhi {

// ─── GPU Push Constant Manager ───────────────────────────────────────────
// Tracks and validates push constant ranges per pipeline layout.
// Vulkan limits push constants to 128 bytes (guaranteed minimum),
// and ranges must not overlap between stages.
//
// Provides type-safe push constant updates with validation.

static constexpr u32 MAX_PUSH_CONSTANT_SIZE = 128; // Vulkan guaranteed min

enum class ShaderStage : u32 {
    Vertex   = 0x01,
    Fragment = 0x10,
    Compute  = 0x20,
    Geometry = 0x08,
    TessCtrl = 0x02,
    TessEval = 0x04,
    Mesh     = 0x40,
    Task     = 0x80,
    All      = 0x7F,
};

inline ShaderStage operator|(ShaderStage a, ShaderStage b) {
    return static_cast<ShaderStage>(static_cast<u32>(a) | static_cast<u32>(b));
}

struct PushConstantRange {
    ShaderStage stageFlags;
    u32         offset;
    u32         size;
};

struct PushConstantLayout {
    std::vector<PushConstantRange> ranges;
    u32 totalSize = 0;
    std::string debugName;
};

struct PushConstantValidation {
    bool valid = true;
    std::string error;
};

class PushConstantManager {
public:
    // Register a push constant layout for a pipeline
    u32 RegisterLayout(const PushConstantLayout& layout);

    // Validate a layout before registration
    static PushConstantValidation Validate(const PushConstantLayout& layout);

    // Get a registered layout
    const PushConstantLayout& GetLayout(u32 layoutId) const;

    // Set push constants on a command list
    void SetConstants(ICommandList* cmd, u32 layoutId, ShaderStage stage,
                      u32 offset, u32 size, const void* data);

    // Type-safe push constant setter
    template<typename T>
    void Set(ICommandList* cmd, u32 layoutId, ShaderStage stage, u32 offset, const T& value) {
        static_assert(sizeof(T) <= MAX_PUSH_CONSTANT_SIZE, "Push constant exceeds max size");
        SetConstants(cmd, layoutId, stage, offset, sizeof(T), &value);
    }

    // Convenience: set entire push constant block for vertex+fragment
    template<typename T>
    void SetGraphics(ICommandList* cmd, u32 layoutId, const T& value) {
        Set(cmd, layoutId, ShaderStage::Vertex | ShaderStage::Fragment, 0, value);
    }

    // Convenience: set entire push constant block for compute
    template<typename T>
    void SetCompute(ICommandList* cmd, u32 layoutId, const T& value) {
        Set(cmd, layoutId, ShaderStage::Compute, 0, value);
    }

    u32 GetRegisteredCount() const { return static_cast<u32>(m_layouts.size()); }

    // Build VkPushConstantRange array for pipeline layout creation
    static std::vector<PushConstantRange> MergeRanges(const std::vector<PushConstantRange>& ranges);

private:
    std::vector<PushConstantLayout> m_layouts;
};

} // namespace nge::rhi
