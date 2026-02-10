#include "engine/rhi/common/rhi_mesh_lod_selector.h"
#include "engine/core/logging/log.h"
#include <algorithm>
#include <cmath>

namespace nge::rhi {

bool MeshLODSelector::Init(const LODSelectorConfig& config) {
    m_config = config;
    m_nextMeshId = 0;
    m_lodChanges = 0;
    m_instancesLOD0 = 0;
    m_instancesMaxLOD = 0;
    m_totalTriangles = 0;
    m_totalError = 0.0f;
    m_totalSelections = 0;

    NGE_LOG_INFO("Mesh LOD selector initialized: maxMeshes={}, errorThreshold={}, hysteresis={}, transitions={}",
                 config.maxMeshes, config.errorThreshold, config.enableHysteresis, config.enableTransitions);
    return true;
}

void MeshLODSelector::Shutdown() {
    m_meshes.clear();
}

u32 MeshLODSelector::RegisterMesh(float boundingSphereRadius, float importance,
                                    const std::string& name) {
    std::lock_guard lock(m_mutex);

    if (m_meshes.size() >= m_config.maxMeshes) {
        NGE_LOG_WARN("Mesh LOD selector: max meshes reached ({})", m_config.maxMeshes);
        return UINT32_MAX;
    }

    u32 id = m_nextMeshId++;

    MeshLODChain chain;
    chain.meshId = id;
    chain.boundingSphereRadius = boundingSphereRadius;
    chain.importance = importance;
    chain.debugName = name;

    m_meshes[id] = std::move(chain);
    return id;
}

void MeshLODSelector::AddLODLevel(u32 meshId, u32 triangleCount, float screenSpaceError,
                                    float switchInDist, float switchOutDist) {
    std::lock_guard lock(m_mutex);

    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) return;

    MeshLODInfo lod;
    lod.lodLevel = static_cast<u32>(it->second.lods.size());
    lod.triangleCount = triangleCount;
    lod.screenSpaceError = screenSpaceError;
    lod.switchInDistance = switchInDist;
    lod.switchOutDistance = switchOutDist;

    it->second.lods.push_back(lod);
}

u32 MeshLODSelector::SelectLOD(u32 meshId, float distanceToCamera, float screenSize) {
    std::lock_guard lock(m_mutex);

    if (m_config.forceLOD >= 0) {
        return static_cast<u32>(m_config.forceLOD);
    }

    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) return 0;

    u32 selected = SelectLODInternal(it->second, distanceToCamera, screenSize);

    // Update stats
    m_totalSelections++;
    if (selected == 0) m_instancesLOD0++;
    if (selected == static_cast<u32>(it->second.lods.size()) - 1) m_instancesMaxLOD++;
    if (selected < it->second.lods.size()) {
        m_totalTriangles += it->second.lods[selected].triangleCount;
        m_totalError += it->second.lods[selected].screenSpaceError;
    }

    return selected;
}

u32 MeshLODSelector::SelectLODWithHysteresis(u32 meshId, u32 previousLOD,
                                                float distanceToCamera, float screenSize) {
    std::lock_guard lock(m_mutex);

    if (m_config.forceLOD >= 0) {
        return static_cast<u32>(m_config.forceLOD);
    }

    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) return 0;

    u32 desired = SelectLODInternal(it->second, distanceToCamera, screenSize);

    if (!m_config.enableHysteresis || previousLOD == desired) {
        return desired;
    }

    // Apply hysteresis: only switch if we're past the hysteresis band
    if (previousLOD < it->second.lods.size() && desired < it->second.lods.size()) {
        const auto& prevInfo = it->second.lods[previousLOD];
        float switchDist = (desired > previousLOD) ? prevInfo.switchOutDistance : prevInfo.switchInDistance;
        float hystBand = switchDist * m_config.hysteresisBand;

        if (desired > previousLOD) {
            // Going to lower quality: require passing beyond switch point + hysteresis
            if (distanceToCamera < switchDist + hystBand) {
                return previousLOD; // Stay at current
            }
        } else {
            // Going to higher quality: require being well inside switch range
            if (distanceToCamera > switchDist - hystBand) {
                return previousLOD; // Stay at current
            }
        }
    }

    m_lodChanges++;
    return desired;
}

void MeshLODSelector::BatchSelectLOD(LODInstance* instances, u32 count) {
    std::lock_guard lock(m_mutex);

    for (u32 i = 0; i < count; ++i) {
        auto& inst = instances[i];
        auto it = m_meshes.find(inst.meshId);
        if (it == m_meshes.end()) continue;

        u32 desired;
        if (m_config.forceLOD >= 0) {
            desired = static_cast<u32>(m_config.forceLOD);
        } else {
            desired = SelectLODInternal(it->second, inst.distanceToCamera, inst.screenSize);
        }

        inst.previousLOD = inst.currentLOD;
        inst.currentLOD = desired;

        if (inst.previousLOD != inst.currentLOD) {
            inst.transitionFactor = m_config.enableTransitions ? 1.0f : 0.0f;
            m_lodChanges++;
        }

        m_totalSelections++;
        if (desired == 0) m_instancesLOD0++;
        u32 maxLOD = it->second.lods.empty() ? 0 : static_cast<u32>(it->second.lods.size()) - 1;
        if (desired == maxLOD) m_instancesMaxLOD++;
        if (desired < it->second.lods.size()) {
            m_totalTriangles += it->second.lods[desired].triangleCount;
        }
    }
}

const MeshLODChain* MeshLODSelector::GetMeshInfo(u32 meshId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) return nullptr;
    return &it->second;
}

u32 MeshLODSelector::GetLODCount(u32 meshId) const {
    std::lock_guard lock(m_mutex);

    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) return 0;
    return static_cast<u32>(it->second.lods.size());
}

u32 MeshLODSelector::GetTriangleCount(u32 meshId, u32 lodLevel) const {
    std::lock_guard lock(m_mutex);

    auto it = m_meshes.find(meshId);
    if (it == m_meshes.end()) return 0;
    if (lodLevel >= it->second.lods.size()) return 0;
    return it->second.lods[lodLevel].triangleCount;
}

void MeshLODSelector::SetForceLOD(i32 lodLevel) {
    std::lock_guard lock(m_mutex);
    m_config.forceLOD = lodLevel;
}

u32 MeshLODSelector::GetMeshCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_meshes.size());
}

void MeshLODSelector::Reset() {
    std::lock_guard lock(m_mutex);
    m_meshes.clear();
    m_nextMeshId = 0;
    m_lodChanges = 0;
    m_instancesLOD0 = 0;
    m_instancesMaxLOD = 0;
    m_totalTriangles = 0;
    m_totalError = 0.0f;
    m_totalSelections = 0;
}

LODSelectorStats MeshLODSelector::GetStats() const {
    std::lock_guard lock(m_mutex);

    LODSelectorStats stats{};
    stats.totalMeshes = static_cast<u32>(m_meshes.size());
    stats.totalInstances = m_totalSelections;
    stats.lodChangesThisFrame = m_lodChanges;
    stats.instancesAtLOD0 = m_instancesLOD0;
    stats.instancesAtMaxLOD = m_instancesMaxLOD;
    stats.totalTrianglesSelected = m_totalTriangles;
    stats.avgScreenError = m_totalSelections > 0
        ? m_totalError / static_cast<float>(m_totalSelections)
        : 0.0f;

    return stats;
}

u32 MeshLODSelector::SelectLODInternal(const MeshLODChain& mesh, float distance, float screenSize) const {
    if (mesh.lods.empty()) return 0;

    // Screen-space error selection: find the coarsest LOD whose error is below threshold
    float adjustedThreshold = m_config.errorThreshold / std::max(mesh.importance, 0.01f);

    for (u32 i = 0; i < static_cast<u32>(mesh.lods.size()); ++i) {
        const auto& lod = mesh.lods[i];

        // Project screen-space error based on distance
        float projectedError = lod.screenSpaceError * mesh.boundingSphereRadius / std::max(distance, 0.1f);

        if (projectedError <= adjustedThreshold) {
            return i;
        }
    }

    // Distance-based fallback
    for (u32 i = 0; i < static_cast<u32>(mesh.lods.size()); ++i) {
        if (distance >= mesh.lods[i].switchInDistance &&
            distance < mesh.lods[i].switchOutDistance) {
            return i;
        }
    }

    // Default to coarsest LOD
    return static_cast<u32>(mesh.lods.size()) - 1;
}

} // namespace nge::rhi
