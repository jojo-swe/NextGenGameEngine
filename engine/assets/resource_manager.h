#pragma once

#include "engine/core/types.h"
#include "engine/core/hash.h"
#include "engine/rhi/common/rhi_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <memory>
#include <atomic>
#include <functional>

namespace nge::assets {

// ─── Resource Handle ─────────────────────────────────────────────────────

using ResourceId = u64;
inline constexpr ResourceId INVALID_RESOURCE = 0;

enum class ResourceType : u8 {
    Mesh,
    Texture,
    Material,
    Shader,
    AnimationClip,
    Skeleton,
    Sound,
    Script,
    Prefab,
    Scene,
    Font,
    Count,
};

enum class ResourceState : u8 {
    Unloaded,
    Loading,     // Async load in progress
    Loaded,      // CPU-side data ready
    Uploading,   // GPU upload in progress
    Ready,       // Fully ready for use
    Failed,
};

// ─── Resource Entry ──────────────────────────────────────────────────────

struct ResourceEntry {
    ResourceId    id       = INVALID_RESOURCE;
    ResourceType  type     = ResourceType::Mesh;
    ResourceState state    = ResourceState::Unloaded;
    std::string   path;          // Source file path
    std::string   name;          // Display name
    u64           sizeBytes = 0; // Memory footprint
    u32           refCount  = 0; // Reference counting
    u64           lastUsedFrame = 0;
    void*         data     = nullptr; // Type-erased loaded data

    // GPU handles (if applicable)
    rhi::BufferHandle  gpuBuffer;
    rhi::TextureHandle gpuTexture;
};

// ─── Resource Load Request ───────────────────────────────────────────────

struct ResourceLoadRequest {
    ResourceId   id;
    ResourceType type;
    std::string  path;
    u32          priority = 0;  // Higher = load sooner
    bool         streaming = false;
    std::function<void(ResourceId, bool)> onComplete;
};

// ─── Resource Manager ────────────────────────────────────────────────────
// Central registry for all engine resources. Handles:
//   - Loading from disk (sync and async)
//   - GPU upload for textures/meshes
//   - Reference counting and automatic unloading
//   - Hot-reload on file change
//   - LRU eviction for streaming budget

class ResourceManager {
public:
    bool Init(rhi::IDevice* device, u32 streamingBudgetMB = 512);
    void Shutdown();

    // Synchronous loading (blocks until ready)
    ResourceId LoadSync(ResourceType type, const std::string& path);

    // Asynchronous loading (returns immediately, resource enters Loading state)
    ResourceId LoadAsync(ResourceType type, const std::string& path,
                          std::function<void(ResourceId, bool)> onComplete = nullptr);

    // Unload a resource (decrements ref count; freed when count reaches 0)
    void Unload(ResourceId id);

    // Reference counting
    void AddRef(ResourceId id);
    void Release(ResourceId id);

    // Query
    ResourceState GetState(ResourceId id) const;
    const ResourceEntry* GetEntry(ResourceId id) const;
    bool IsReady(ResourceId id) const;

    // Type-safe data access
    template<typename T>
    T* GetData(ResourceId id) {
        auto* entry = GetEntryMutable(id);
        return entry ? static_cast<T*>(entry->data) : nullptr;
    }

    // GPU handles
    rhi::BufferHandle  GetBuffer(ResourceId id) const;
    rhi::TextureHandle GetTexture(ResourceId id) const;

    // Per-frame update: processes async loads, GPU uploads, eviction
    void Update(u64 frameIndex);

    // Hot-reload: check for changed files and reload
    void CheckHotReload();

    // Find resource by path
    ResourceId FindByPath(const std::string& path) const;

    // Stats
    u64 GetTotalMemoryUsage() const { return m_totalMemoryUsage; }
    u64 GetGPUMemoryUsage() const { return m_gpuMemoryUsage; }
    u32 GetLoadedResourceCount() const;
    u32 GetPendingLoadCount() const;

private:
    ResourceId GenerateId(const std::string& path) const;
    ResourceEntry* GetEntryMutable(ResourceId id);
    void ProcessLoadQueue();
    void ProcessGPUUploads();
    void EvictLRU();

    // Type-specific loaders
    bool LoadMesh(ResourceEntry& entry);
    bool LoadTexture(ResourceEntry& entry);
    bool LoadShader(ResourceEntry& entry);
    bool LoadSound(ResourceEntry& entry);

    // GPU upload
    bool UploadMeshToGPU(ResourceEntry& entry);
    bool UploadTextureToGPU(ResourceEntry& entry);

    rhi::IDevice* m_device = nullptr;

    mutable std::mutex m_mutex;
    std::unordered_map<ResourceId, ResourceEntry> m_resources;
    std::unordered_map<std::string, ResourceId>   m_pathToId;

    // Async load queue
    std::vector<ResourceLoadRequest> m_loadQueue;
    std::vector<ResourceId>          m_gpuUploadQueue;

    // Memory tracking
    std::atomic<u64> m_totalMemoryUsage{0};
    std::atomic<u64> m_gpuMemoryUsage{0};
    u64              m_streamingBudget = 512ULL * 1024 * 1024; // 512 MB default

    u64 m_currentFrame = 0;
};

} // namespace nge::assets
