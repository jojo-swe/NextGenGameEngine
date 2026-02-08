#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>

namespace nge::rhi {

// ─── Vulkan Debug Marker/Label System ────────────────────────────────────
// Integrates with GPU debuggers (RenderDoc, Nsight, etc.) by inserting
// debug labels, regions, and object names into the Vulkan command stream.
// Uses VK_EXT_debug_utils for labeling.
//
// Usage:
//   markers.BeginRegion(cmd, "GBuffer Pass", {0.2f, 0.8f, 0.2f, 1.0f});
//   // ... draw commands
//   markers.EndRegion(cmd);
//   markers.InsertLabel(cmd, "Shadow cascade 0");
//   markers.SetObjectName(buffer, "SceneInstances");

struct DebugColor {
    f32 r = 1.0f, g = 1.0f, b = 1.0f, a = 1.0f;

    static DebugColor Red()    { return {1, 0, 0, 1}; }
    static DebugColor Green()  { return {0, 1, 0, 1}; }
    static DebugColor Blue()   { return {0, 0, 1, 1}; }
    static DebugColor Yellow() { return {1, 1, 0, 1}; }
    static DebugColor Cyan()   { return {0, 1, 1, 1}; }
    static DebugColor Magenta(){ return {1, 0, 1, 1}; }
    static DebugColor Orange() { return {1, 0.5f, 0, 1}; }
    static DebugColor White()  { return {1, 1, 1, 1}; }
};

// RAII scoped region (auto-ends when destroyed)
class ScopedDebugRegion {
public:
    ScopedDebugRegion(ICommandList* cmd, const std::string& name, const DebugColor& color = DebugColor::White());
    ~ScopedDebugRegion();
    ScopedDebugRegion(const ScopedDebugRegion&) = delete;
    ScopedDebugRegion& operator=(const ScopedDebugRegion&) = delete;
    ScopedDebugRegion(ScopedDebugRegion&& other) noexcept;
    ScopedDebugRegion& operator=(ScopedDebugRegion&&) = delete;

private:
    ICommandList* m_cmd = nullptr;
};

class DebugMarkerSystem {
public:
    bool Init(IDevice* device);
    void Shutdown();

    // Check if debug markers are available (debug utils extension loaded)
    bool IsAvailable() const { return m_available; }

    // Command buffer regions (nested begin/end)
    void BeginRegion(ICommandList* cmd, const std::string& name, const DebugColor& color = DebugColor::White());
    void EndRegion(ICommandList* cmd);

    // Single-point label in the command stream
    void InsertLabel(ICommandList* cmd, const std::string& name, const DebugColor& color = DebugColor::White());

    // RAII scoped region
    ScopedDebugRegion Scope(ICommandList* cmd, const std::string& name, const DebugColor& color = DebugColor::White());

    // Queue regions
    void BeginQueueRegion(u64 queueHandle, const std::string& name, const DebugColor& color = DebugColor::White());
    void EndQueueRegion(u64 queueHandle);

    // Object naming (for RenderDoc, Nsight, etc.)
    void SetBufferName(BufferHandle buffer, const std::string& name);
    void SetTextureName(TextureHandle texture, const std::string& name);
    void SetPipelineName(PipelineHandle pipeline, const std::string& name);
    void SetSamplerName(SamplerHandle sampler, const std::string& name);
    void SetObjectName(u64 objectHandle, u32 objectType, const std::string& name);

private:
    IDevice* m_device = nullptr;
    bool m_available = false;
};

} // namespace nge::rhi
