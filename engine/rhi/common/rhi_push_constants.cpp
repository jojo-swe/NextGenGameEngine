#include "engine/rhi/common/rhi_push_constants.h"
#include "engine/core/logging/log.h"
#include <algorithm>

namespace nge::rhi {

u32 PushConstantManager::RegisterLayout(const PushConstantLayout& layout) {
    auto validation = Validate(layout);
    if (!validation.valid) {
        NGE_LOG_ERROR("Push constant layout validation failed: {}", validation.error);
        return UINT32_MAX;
    }

    u32 id = static_cast<u32>(m_layouts.size());
    m_layouts.push_back(layout);
    return id;
}

PushConstantValidation PushConstantManager::Validate(const PushConstantLayout& layout) {
    PushConstantValidation result;

    if (layout.totalSize > MAX_PUSH_CONSTANT_SIZE) {
        result.valid = false;
        result.error = "Total push constant size (" + std::to_string(layout.totalSize) +
                       ") exceeds maximum (" + std::to_string(MAX_PUSH_CONSTANT_SIZE) + ")";
        return result;
    }

    // Check each range
    for (const auto& range : layout.ranges) {
        if (range.offset + range.size > MAX_PUSH_CONSTANT_SIZE) {
            result.valid = false;
            result.error = "Range at offset " + std::to_string(range.offset) +
                           " with size " + std::to_string(range.size) +
                           " exceeds maximum push constant size";
            return result;
        }

        if (range.size == 0) {
            result.valid = false;
            result.error = "Range at offset " + std::to_string(range.offset) + " has zero size";
            return result;
        }

        if (range.offset % 4 != 0) {
            result.valid = false;
            result.error = "Range offset " + std::to_string(range.offset) +
                           " is not 4-byte aligned";
            return result;
        }

        if (range.size % 4 != 0) {
            result.valid = false;
            result.error = "Range size " + std::to_string(range.size) +
                           " is not a multiple of 4";
            return result;
        }
    }

    // Check for overlapping ranges with same stage flags
    for (size_t i = 0; i < layout.ranges.size(); ++i) {
        for (size_t j = i + 1; j < layout.ranges.size(); ++j) {
            const auto& a = layout.ranges[i];
            const auto& b = layout.ranges[j];

            u32 stageOverlap = static_cast<u32>(a.stageFlags) & static_cast<u32>(b.stageFlags);
            if (stageOverlap != 0) {
                bool rangeOverlap = (a.offset < b.offset + b.size) && (b.offset < a.offset + a.size);
                if (rangeOverlap) {
                    result.valid = false;
                    result.error = "Overlapping push constant ranges at offsets " +
                                   std::to_string(a.offset) + " and " + std::to_string(b.offset) +
                                   " with shared stage flags";
                    return result;
                }
            }
        }
    }

    return result;
}

const PushConstantLayout& PushConstantManager::GetLayout(u32 layoutId) const {
    return m_layouts[layoutId];
}

void PushConstantManager::SetConstants(ICommandList* cmd, u32 layoutId, ShaderStage stage,
                                         u32 offset, u32 size, const void* data) {
    if (layoutId >= m_layouts.size()) {
        NGE_LOG_ERROR("Invalid push constant layout ID: {}", layoutId);
        return;
    }

    // TODO: vkCmdPushConstants(cmd->GetVkCommandBuffer(),
    //                           pipelineLayout,
    //                           toVkShaderStageFlags(stage),
    //                           offset, size, data);
    (void)cmd;
    (void)stage;
    (void)offset;
    (void)size;
    (void)data;
}

std::vector<PushConstantRange> PushConstantManager::MergeRanges(
    const std::vector<PushConstantRange>& ranges) {
    if (ranges.empty()) return {};

    // Sort by offset
    auto sorted = ranges;
    std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
        return a.offset < b.offset;
    });

    std::vector<PushConstantRange> merged;
    merged.push_back(sorted[0]);

    for (size_t i = 1; i < sorted.size(); ++i) {
        auto& last = merged.back();
        const auto& curr = sorted[i];

        // Merge if adjacent or overlapping with same stage flags
        if (static_cast<u32>(last.stageFlags) == static_cast<u32>(curr.stageFlags) &&
            last.offset + last.size >= curr.offset) {
            u32 end = std::max(last.offset + last.size, curr.offset + curr.size);
            last.size = end - last.offset;
        } else {
            merged.push_back(curr);
        }
    }

    return merged;
}

} // namespace nge::rhi
