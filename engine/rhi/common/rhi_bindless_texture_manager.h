#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <queue>
#include <mutex>

namespace nge::rhi {

// ─── GPU Bindless Texture Manager ────────────────────────────────────────
// Manages a global texture descriptor array for bindless rendering.
// All scene textures are registered here and accessed by index in shaders.
// Tracks residency (which textures are currently on GPU) and handles
// slot recycling when textures are unloaded.
//
// Shaders access: Texture2D g_Textures[] : register(t0, space0);
// Material stores: uint albedoIndex, normalIndex, etc.

struct BindlessTextureSlot {
    TextureHandle texture;
    u32           slotIndex;
    std::string   debugName;
    bool          resident;     // Currently uploaded to GPU
    u64           lastUsedFrame;
};

struct BindlessTextureConfig {
    u32 maxTextures = 16384;     // Global descriptor array size
    u32 reservedSlots = 4;       // Slots 0-3 for default textures (white, black, normal, error)
};

struct BindlessTextureStats {
    u32 totalSlots;
    u32 usedSlots;
    u32 residentTextures;
    u32 freeSlots;
    u32 pendingUploads;
};

class BindlessTextureManager {
public:
    bool Init(IDevice* device, const BindlessTextureConfig& config = {});
    void Shutdown();

    // Register a texture and get its bindless index
    u32 Register(TextureHandle texture, const std::string& debugName = "");

    // Unregister a texture (frees its slot for reuse)
    void Unregister(u32 slotIndex);

    // Update the texture at a slot (e.g., after streaming a higher mip)
    void Update(u32 slotIndex, TextureHandle newTexture);

    // Mark a texture as resident/non-resident
    void SetResident(u32 slotIndex, bool resident);

    // Check if a slot is valid and resident
    bool IsResident(u32 slotIndex) const;

    // Get the texture handle at a slot
    TextureHandle GetTexture(u32 slotIndex) const;

    // Get default texture indices
    u32 GetWhiteTextureIndex() const { return 0; }
    u32 GetBlackTextureIndex() const { return 1; }
    u32 GetDefaultNormalIndex() const { return 2; }
    u32 GetErrorTextureIndex() const { return 3; }

    // Get the descriptor set/array handle for shader binding
    u64 GetDescriptorSet() const { return m_descriptorSet; }

    // Flush pending descriptor writes
    void FlushUpdates();

    // Per-frame update
    void BeginFrame(u64 frameNumber);

    BindlessTextureStats GetStats() const;

private:
    IDevice* m_device = nullptr;
    BindlessTextureConfig m_config;

    std::vector<BindlessTextureSlot> m_slots;
    std::stack<u32> m_freeSlots;
    std::vector<u32> m_pendingUpdates; // Slots needing descriptor writes

    u64 m_descriptorSet = 0;  // VkDescriptorSet for the global array
    u64 m_currentFrame = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
