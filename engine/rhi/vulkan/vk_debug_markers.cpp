#include "engine/rhi/vulkan/vk_debug_markers.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

// ─── ScopedDebugRegion ───────────────────────────────────────────────────

ScopedDebugRegion::ScopedDebugRegion(ICommandList* cmd, const std::string& name, const DebugColor& color)
    : m_cmd(cmd) {
    // TODO: vkCmdBeginDebugUtilsLabelEXT
    (void)name;
    (void)color;
}

ScopedDebugRegion::~ScopedDebugRegion() {
    if (m_cmd) {
        // TODO: vkCmdEndDebugUtilsLabelEXT
    }
}

ScopedDebugRegion::ScopedDebugRegion(ScopedDebugRegion&& other) noexcept
    : m_cmd(other.m_cmd) {
    other.m_cmd = nullptr;
}

// ─── DebugMarkerSystem ───────────────────────────────────────────────────

bool DebugMarkerSystem::Init(IDevice* device) {
    m_device = device;

    // TODO: Check if VK_EXT_debug_utils is available
    // PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT =
    //     (PFN_vkSetDebugUtilsObjectNameEXT)vkGetDeviceProcAddr(device, "vkSetDebugUtilsObjectNameEXT");
    // m_available = (vkSetDebugUtilsObjectNameEXT != nullptr);

    m_available = true; // Assume available in debug builds

    if (m_available) {
        NGE_LOG_INFO("Debug marker system initialized (VK_EXT_debug_utils)");
    } else {
        NGE_LOG_WARN("Debug marker system: VK_EXT_debug_utils not available");
    }
    return true;
}

void DebugMarkerSystem::Shutdown() {
    m_available = false;
}

void DebugMarkerSystem::BeginRegion(ICommandList* cmd, const std::string& name, const DebugColor& color) {
    if (!m_available) return;

    // TODO:
    // VkDebugUtilsLabelEXT label{};
    // label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    // label.pLabelName = name.c_str();
    // label.color[0] = color.r;
    // label.color[1] = color.g;
    // label.color[2] = color.b;
    // label.color[3] = color.a;
    // vkCmdBeginDebugUtilsLabelEXT(cmd->GetVkCommandBuffer(), &label);

    (void)cmd;
    (void)name;
    (void)color;
}

void DebugMarkerSystem::EndRegion(ICommandList* cmd) {
    if (!m_available) return;
    // TODO: vkCmdEndDebugUtilsLabelEXT(cmd->GetVkCommandBuffer());
    (void)cmd;
}

void DebugMarkerSystem::InsertLabel(ICommandList* cmd, const std::string& name, const DebugColor& color) {
    if (!m_available) return;

    // TODO:
    // VkDebugUtilsLabelEXT label{};
    // label.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT;
    // label.pLabelName = name.c_str();
    // label.color[0] = color.r; ...
    // vkCmdInsertDebugUtilsLabelEXT(cmd->GetVkCommandBuffer(), &label);

    (void)cmd;
    (void)name;
    (void)color;
}

ScopedDebugRegion DebugMarkerSystem::Scope(ICommandList* cmd, const std::string& name, const DebugColor& color) {
    BeginRegion(cmd, name, color);
    return ScopedDebugRegion(cmd, name, color);
}

void DebugMarkerSystem::BeginQueueRegion(u64 queueHandle, const std::string& name, const DebugColor& color) {
    if (!m_available) return;
    // TODO: vkQueueBeginDebugUtilsLabelEXT
    (void)queueHandle;
    (void)name;
    (void)color;
}

void DebugMarkerSystem::EndQueueRegion(u64 queueHandle) {
    if (!m_available) return;
    // TODO: vkQueueEndDebugUtilsLabelEXT
    (void)queueHandle;
}

void DebugMarkerSystem::SetBufferName(BufferHandle buffer, const std::string& name) {
    if (!buffer.IsValid()) return;
    SetObjectName(static_cast<u64>(buffer.id), 9 /* VK_OBJECT_TYPE_BUFFER */, name);
}

void DebugMarkerSystem::SetTextureName(TextureHandle texture, const std::string& name) {
    if (!texture.IsValid()) return;
    SetObjectName(static_cast<u64>(texture.id), 10 /* VK_OBJECT_TYPE_IMAGE */, name);
}

void DebugMarkerSystem::SetPipelineName(PipelineHandle pipeline, const std::string& name) {
    if (!pipeline.IsValid()) return;
    SetObjectName(static_cast<u64>(pipeline.id), 19 /* VK_OBJECT_TYPE_PIPELINE */, name);
}

void DebugMarkerSystem::SetSamplerName(SamplerHandle sampler, const std::string& name) {
    SetObjectName(static_cast<u64>(sampler.id), 21 /* VK_OBJECT_TYPE_SAMPLER */, name);
}

void DebugMarkerSystem::SetObjectName(u64 objectHandle, u32 objectType, const std::string& name) {
    if (!m_available || objectHandle == 0) return;

    // TODO:
    // VkDebugUtilsObjectNameInfoEXT nameInfo{};
    // nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    // nameInfo.objectType = static_cast<VkObjectType>(objectType);
    // nameInfo.objectHandle = objectHandle;
    // nameInfo.pObjectName = name.c_str();
    // vkSetDebugUtilsObjectNameEXT(m_device->GetVkDevice(), &nameInfo);

    (void)objectHandle;
    (void)objectType;
    (void)name;
}

} // namespace nge::rhi
