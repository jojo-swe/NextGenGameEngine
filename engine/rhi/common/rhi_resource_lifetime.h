#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <mutex>
#include <functional>

namespace nge::rhi {

// ─── GPU Resource Lifetime Manager ───────────────────────────────────────
// Ref-counted GPU handles with deferred deletion. Resources are released
// only after the GPU has finished using them (frame latency).
//
// Prevents use-after-free on GPU resources that may still be referenced
// by in-flight command buffers. Integrates with the frame fence system.

enum class GPUResourceType : u8 {
    Buffer,
    Texture,
    Sampler,
    Pipeline,
    DescriptorSet,
    RenderPass,
    Framebuffer,
    QueryPool,
};

struct GPUResourceEntry {
    u64             handle;
    GPUResourceType type;
    u32             refCount;
    u64             retireFrame;   // Frame when last ref was released
    std::string     debugName;
};

using ResourceDestroyFn = std::function<void(u64 handle, GPUResourceType type)>;

class ResourceLifetimeManager {
public:
    struct Config {
        u32 framesToKeepAlive = 3;  // Frames after last ref before destruction
        u32 maxDeferredDeletes = 4096;
    };

    // No default argument: Config's default member initializers cannot be
    // used in a default argument while the enclosing class is incomplete.
    bool Init(IDevice* device, const Config& config);
    bool Init(IDevice* device) { return Init(device, Config{}); }
    void Shutdown();

    // Register a new resource (starts with refCount=1)
    u64 Register(u64 handle, GPUResourceType type, const std::string& debugName = "");

    // Add a reference
    void AddRef(u64 resourceId);

    // Release a reference (defers deletion if refCount reaches 0)
    void Release(u64 resourceId);

    // Set custom destroy callback
    void SetDestroyCallback(ResourceDestroyFn callback);

    // Process deferred deletions (call once per frame with current frame number)
    u32 ProcessDeletions(u64 currentFrame);

    // Force destroy all pending (for shutdown)
    void FlushAll();

    // Query
    u32 GetRefCount(u64 resourceId) const;
    u32 GetActiveCount() const;
    u32 GetPendingDeletionCount() const;

private:
    IDevice* m_device = nullptr;
    Config m_config;
    ResourceDestroyFn m_destroyFn;

    std::vector<GPUResourceEntry> m_resources;
    std::vector<u64> m_pendingDeletion;  // Resource IDs waiting for GPU to finish
    std::vector<u64> m_freeSlots;

    u64 m_nextId = 1;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
