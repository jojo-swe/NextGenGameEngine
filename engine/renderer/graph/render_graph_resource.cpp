#include "engine/renderer/graph/render_graph_resource.h"
#include <algorithm>

namespace nge::renderer {

RenderGraphResource::RenderGraphResource(u32 id, const std::string& name, bool isTexture)
    : m_id(id), m_name(name), m_isTexture(isTexture) {
    // Version 0 = initial (undefined) state
    ResourceVersion v0;
    v0.version = 0;
    v0.writerPass = UINT32_MAX;
    v0.writerState = rhi::ResourceState::Undefined;
    v0.writerQueue = rhi::QueueType::Graphics;
    m_versions.push_back(v0);
}

void RenderGraphResource::RecordRead(u32 passIndex, rhi::ResourceState state,
                                       rhi::QueueType queue, u32 subresource) {
    ResourceAccess access;
    access.passIndex = passIndex;
    access.type = ResourceAccessType::Read;
    access.state = state;
    access.queue = queue;
    access.version = m_currentVersion;
    access.subresource = subresource;
    m_accesses.push_back(access);

    // Track readers on the current version
    if (m_currentVersion < m_versions.size()) {
        m_versions[m_currentVersion].readerPasses.push_back(passIndex);
    }

    m_firstPass = std::min(m_firstPass, passIndex);
    m_lastPass = std::max(m_lastPass, passIndex);
}

void RenderGraphResource::RecordWrite(u32 passIndex, rhi::ResourceState state,
                                        rhi::QueueType queue, u32 subresource) {
    // Create a new version
    m_currentVersion++;

    ResourceVersion newVersion;
    newVersion.version = m_currentVersion;
    newVersion.writerPass = passIndex;
    newVersion.writerState = state;
    newVersion.writerQueue = queue;
    m_versions.push_back(newVersion);

    ResourceAccess access;
    access.passIndex = passIndex;
    access.type = ResourceAccessType::Write;
    access.state = state;
    access.queue = queue;
    access.version = m_currentVersion;
    access.subresource = subresource;
    m_accesses.push_back(access);

    m_firstPass = std::min(m_firstPass, passIndex);
    m_lastPass = std::max(m_lastPass, passIndex);
}

bool RenderGraphResource::HasRAWHazard(u32 writerPass, u32 readerPass) const {
    // Read-after-write: reader reads a version written by writerPass
    for (const auto& ver : m_versions) {
        if (ver.writerPass == writerPass) {
            for (u32 reader : ver.readerPasses) {
                if (reader == readerPass) return true;
            }
        }
    }
    return false;
}

bool RenderGraphResource::HasWARHazard(u32 readerPass, u32 writerPass) const {
    // Write-after-read: writer creates a new version after readerPass read the old one
    for (const auto& ver : m_versions) {
        for (u32 reader : ver.readerPasses) {
            if (reader == readerPass) {
                // Check if writerPass writes to a later version
                for (const auto& laterVer : m_versions) {
                    if (laterVer.version > ver.version && laterVer.writerPass == writerPass) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

bool RenderGraphResource::HasWAWHazard(u32 writerPassA, u32 writerPassB) const {
    // Write-after-write: both passes write to this resource
    bool aWrites = false, bWrites = false;
    for (const auto& ver : m_versions) {
        if (ver.writerPass == writerPassA) aWrites = true;
        if (ver.writerPass == writerPassB) bWrites = true;
    }
    return aWrites && bWrites;
}

bool RenderGraphResource::HasCrossQueueHazard(u32 passA, u32 passB) const {
    const ResourceAccess* accessA = FindAccess(passA);
    const ResourceAccess* accessB = FindAccess(passB);
    if (!accessA || !accessB) return false;
    return accessA->queue != accessB->queue;
}

RenderGraphResource::BarrierInfo RenderGraphResource::GetBarrier(u32 srcPass, u32 dstPass) const {
    BarrierInfo info{};
    info.needsBarrier = false;
    info.isCrossQueue = false;

    const ResourceAccess* src = FindAccess(srcPass);
    const ResourceAccess* dst = FindAccess(dstPass);
    if (!src || !dst) return info;

    // Same state, same queue, and both reads → no barrier needed
    if (src->state == dst->state && src->queue == dst->queue &&
        src->type == ResourceAccessType::Read && dst->type == ResourceAccessType::Read) {
        return info;
    }

    // At least one is a write, or states differ → barrier needed
    if (src->type != ResourceAccessType::Read || dst->type != ResourceAccessType::Read ||
        src->state != dst->state) {
        info.needsBarrier = true;
        info.srcState = src->state;
        info.dstState = dst->state;
        info.srcQueue = src->queue;
        info.dstQueue = dst->queue;
        info.isCrossQueue = (src->queue != dst->queue);
    }

    return info;
}

void RenderGraphResource::Reset() {
    m_currentVersion = 0;
    m_firstPass = UINT32_MAX;
    m_lastPass = 0;
    m_accesses.clear();
    m_versions.clear();

    ResourceVersion v0;
    v0.version = 0;
    v0.writerPass = UINT32_MAX;
    v0.writerState = rhi::ResourceState::Undefined;
    v0.writerQueue = rhi::QueueType::Graphics;
    m_versions.push_back(v0);
}

const ResourceAccess* RenderGraphResource::FindAccess(u32 passIndex) const {
    for (const auto& access : m_accesses) {
        if (access.passIndex == passIndex) return &access;
    }
    return nullptr;
}

} // namespace nge::renderer
