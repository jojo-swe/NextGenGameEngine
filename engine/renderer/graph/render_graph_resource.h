#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>

namespace nge::renderer {

// ─── Render Graph Resource Versioning ────────────────────────────────────
// Tracks read-after-write (RAW), write-after-read (WAR), and
// write-after-write (WAW) hazards per resource across render graph passes.
//
// Each write to a resource creates a new "version". Reads reference a
// specific version. This enables:
//   - Precise barrier placement (only between conflicting accesses)
//   - Detecting redundant barriers
//   - Split-barrier optimization (begin barrier early, end late)
//   - Cross-queue hazard tracking for async compute

enum class ResourceAccessType : u8 {
    Read,
    Write,
    ReadWrite,
};

struct ResourceAccess {
    u32               passIndex;
    ResourceAccessType type;
    rhi::ResourceState state;
    rhi::QueueType     queue;
    u32               version;        // Resource version at this access
    u32               subresource;    // UINT32_MAX = all
};

struct ResourceVersion {
    u32  version;
    u32  writerPass;                  // Pass that created this version
    rhi::ResourceState writerState;
    rhi::QueueType     writerQueue;
    std::vector<u32>   readerPasses;  // Passes that read this version
};

class RenderGraphResource {
public:
    RenderGraphResource() = default;
    explicit RenderGraphResource(u32 id, const std::string& name, bool isTexture);

    // Record an access to this resource from a pass
    void RecordRead(u32 passIndex, rhi::ResourceState state, rhi::QueueType queue,
                      u32 subresource = UINT32_MAX);
    void RecordWrite(u32 passIndex, rhi::ResourceState state, rhi::QueueType queue,
                       u32 subresource = UINT32_MAX);

    // Query current version
    u32 GetCurrentVersion() const { return m_currentVersion; }
    u32 GetVersionCount() const { return static_cast<u32>(m_versions.size()); }

    // Get all accesses for barrier generation
    const std::vector<ResourceAccess>& GetAccesses() const { return m_accesses; }
    const std::vector<ResourceVersion>& GetVersions() const { return m_versions; }

    // Hazard detection between two passes
    bool HasRAWHazard(u32 writerPass, u32 readerPass) const;
    bool HasWARHazard(u32 readerPass, u32 writerPass) const;
    bool HasWAWHazard(u32 writerPassA, u32 writerPassB) const;

    // Cross-queue hazard (needs semaphore sync)
    bool HasCrossQueueHazard(u32 passA, u32 passB) const;

    // Get the required barrier between two passes
    struct BarrierInfo {
        rhi::ResourceState srcState;
        rhi::ResourceState dstState;
        rhi::QueueType     srcQueue;
        rhi::QueueType     dstQueue;
        bool               needsBarrier;
        bool               isCrossQueue;
    };
    BarrierInfo GetBarrier(u32 srcPass, u32 dstPass) const;

    // Lifetime
    u32 GetFirstPass() const { return m_firstPass; }
    u32 GetLastPass() const { return m_lastPass; }
    bool IsUsed() const { return !m_accesses.empty(); }

    // Identity
    u32 GetId() const { return m_id; }
    const std::string& GetName() const { return m_name; }
    bool IsTexture() const { return m_isTexture; }

    // Reset for next frame
    void Reset();

private:
    const ResourceAccess* FindAccess(u32 passIndex) const;

    u32 m_id = 0;
    std::string m_name;
    bool m_isTexture = true;

    u32 m_currentVersion = 0;
    u32 m_firstPass = UINT32_MAX;
    u32 m_lastPass = 0;

    std::vector<ResourceAccess> m_accesses;
    std::vector<ResourceVersion> m_versions;
};

} // namespace nge::renderer
