#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Mesh LOD Selector ───────────────────────────────────────────────
// Selects mesh LOD levels based on screen-space error, distance, and
// importance. Supports Nanite-style continuous LOD, discrete LOD chains,
// and hysteresis to prevent LOD popping.
//
// Use cases:
//   - Select LOD for each mesh instance per frame
//   - Screen-space error metric (projected bounding sphere)
//   - Hysteresis band to prevent rapid LOD switching
//   - Force LOD override for debug/quality presets
//   - LOD transition fade (dithered crossfade support)
//   - Batch LOD selection for GPU-driven rendering

struct MeshLODInfo {
    u32   lodLevel;
    u32   triangleCount;
    float screenSpaceError;   // Max error at this LOD (in pixels)
    float switchInDistance;    // Distance at which this LOD becomes active
    float switchOutDistance;   // Distance at which next LOD takes over
};

struct MeshLODChain {
    u32                     meshId;
    std::vector<MeshLODInfo> lods;
    float                   boundingSphereRadius;
    float                   importance;         // Priority weight (default 1.0)
    std::string             debugName;
};

struct LODInstance {
    u32   meshId;
    u32   instanceId;
    float distanceToCamera;
    float screenSize;          // Projected screen-space size (pixels)
    u32   currentLOD;
    u32   previousLOD;
    float transitionFactor;    // 0..1 for crossfade (0 = fully current)
};

struct LODSelectorConfig {
    u32   maxMeshes = 4096;
    u32   maxInstances = 65536;
    float errorThreshold = 1.0f;      // Max acceptable pixel error
    float hysteresisBand = 0.1f;      // Fraction of switch distance (prevents popping)
    float transitionDuration = 0.2f;  // Seconds for LOD crossfade
    bool  enableHysteresis = true;
    bool  enableTransitions = true;
    i32   forceLOD = -1;              // -1 = auto, 0+ = force specific LOD
};

struct LODSelectorStats {
    u32 totalMeshes;
    u32 totalInstances;
    u32 lodChangesThisFrame;
    u32 instancesAtLOD0;
    u32 instancesAtMaxLOD;
    u64 totalTrianglesSelected;
    float avgScreenError;
};

class MeshLODSelector {
public:
    bool Init(const LODSelectorConfig& config = {});
    void Shutdown();

    // Register a mesh with its LOD chain
    u32 RegisterMesh(float boundingSphereRadius, float importance = 1.0f,
                      const std::string& name = "");

    // Add a LOD level to a registered mesh
    void AddLODLevel(u32 meshId, u32 triangleCount, float screenSpaceError,
                      float switchInDist, float switchOutDist);

    // Select LOD for an instance. Returns selected LOD index.
    u32 SelectLOD(u32 meshId, float distanceToCamera, float screenSize);

    // Select LOD with hysteresis (considers previous LOD)
    u32 SelectLODWithHysteresis(u32 meshId, u32 previousLOD,
                                  float distanceToCamera, float screenSize);

    // Batch LOD selection for multiple instances
    void BatchSelectLOD(LODInstance* instances, u32 count);

    // Get mesh LOD chain info
    const MeshLODChain* GetMeshInfo(u32 meshId) const;

    // Get the LOD count for a mesh
    u32 GetLODCount(u32 meshId) const;

    // Get triangle count for a specific LOD
    u32 GetTriangleCount(u32 meshId, u32 lodLevel) const;

    // Force a specific LOD level globally (-1 for auto)
    void SetForceLOD(i32 lodLevel);

    u32 GetMeshCount() const;

    void Reset();

    LODSelectorStats GetStats() const;

private:
    u32 SelectLODInternal(const MeshLODChain& mesh, float distance, float screenSize) const;

    LODSelectorConfig m_config;
    std::unordered_map<u32, MeshLODChain> m_meshes;

    u32 m_nextMeshId = 0;
    u32 m_lodChanges = 0;
    u32 m_instancesLOD0 = 0;
    u32 m_instancesMaxLOD = 0;
    u64 m_totalTriangles = 0;
    float m_totalError = 0.0f;
    u32 m_totalSelections = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
