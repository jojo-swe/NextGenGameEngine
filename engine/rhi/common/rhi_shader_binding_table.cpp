#include "engine/rhi/common/rhi_shader_binding_table.h"
#include "engine/core/logging/log.h"
#include <cstring>
#include <algorithm>

namespace nge::rhi {

bool ShaderBindingTableManager::Init(const SBTConfig& config) {
    m_config = config;

    m_rayGenRecords.reserve(4);
    m_missRecords.reserve(16);
    m_hitGroupRecords.reserve(config.maxRecords / 2);
    m_callableRecords.reserve(16);

    NGE_LOG_INFO("SBT manager initialized: handleSize={}, handleAlign={}, baseAlign={}, maxRecords={}",
                 config.handleSize, config.handleAlignment, config.baseAlignment, config.maxRecords);
    return true;
}

void ShaderBindingTableManager::Shutdown() {
    m_rayGenRecords.clear();
    m_missRecords.clear();
    m_hitGroupRecords.clear();
    m_callableRecords.clear();
}

u32 ShaderBindingTableManager::AddRayGenRecord(u64 shaderGroupHandle, const void* inlineData,
                                                 u32 inlineDataSize, const std::string& name) {
    return AddRecord(SBTRecordType::RayGen, shaderGroupHandle, inlineData, inlineDataSize, name);
}

u32 ShaderBindingTableManager::AddMissRecord(u64 shaderGroupHandle, const void* inlineData,
                                               u32 inlineDataSize, const std::string& name) {
    return AddRecord(SBTRecordType::Miss, shaderGroupHandle, inlineData, inlineDataSize, name);
}

u32 ShaderBindingTableManager::AddHitGroupRecord(u64 shaderGroupHandle, const void* inlineData,
                                                   u32 inlineDataSize, const std::string& name) {
    return AddRecord(SBTRecordType::HitGroup, shaderGroupHandle, inlineData, inlineDataSize, name);
}

u32 ShaderBindingTableManager::AddCallableRecord(u64 shaderGroupHandle, const void* inlineData,
                                                   u32 inlineDataSize, const std::string& name) {
    return AddRecord(SBTRecordType::Callable, shaderGroupHandle, inlineData, inlineDataSize, name);
}

SBTLayout ShaderBindingTableManager::BuildLayout() const {
    SBTLayout layout{};
    u64 offset = 0;

    // Ray gen region
    layout.rayGen = ComputeRegion(m_rayGenRecords, offset);
    offset += AlignUp(layout.rayGen.size, m_config.baseAlignment);

    // Miss region
    layout.miss = ComputeRegion(m_missRecords, offset);
    offset += AlignUp(layout.miss.size, m_config.baseAlignment);

    // Hit group region
    layout.hitGroup = ComputeRegion(m_hitGroupRecords, offset);
    offset += AlignUp(layout.hitGroup.size, m_config.baseAlignment);

    // Callable region
    layout.callable = ComputeRegion(m_callableRecords, offset);
    offset += AlignUp(layout.callable.size, m_config.baseAlignment);

    layout.totalSize = offset;

    return layout;
}

u64 ShaderBindingTableManager::WriteToBuffer(void* dstBuffer, u64 dstSize) const {
    std::lock_guard lock(m_mutex);

    SBTLayout layout = BuildLayout();
    if (layout.totalSize > dstSize) {
        NGE_LOG_ERROR("SBT buffer too small: need {} bytes, have {}", layout.totalSize, dstSize);
        return 0;
    }

    u8* dst = static_cast<u8*>(dstBuffer);
    std::memset(dst, 0, static_cast<size_t>(dstSize));

    auto writeRegion = [&](const std::vector<SBTShaderRecord>& records, const SBTRegion& region) {
        for (u32 i = 0; i < static_cast<u32>(records.size()); ++i) {
            u8* recordPtr = dst + region.bufferOffset + i * region.stride;

            // Write shader group handle
            std::memcpy(recordPtr, &records[i].shaderGroupHandle, m_config.handleSize);

            // Write inline data after handle
            if (!records[i].inlineData.empty()) {
                std::memcpy(recordPtr + m_config.handleSize,
                            records[i].inlineData.data(),
                            records[i].inlineData.size());
            }
        }
    };

    writeRegion(m_rayGenRecords, layout.rayGen);
    writeRegion(m_missRecords, layout.miss);
    writeRegion(m_hitGroupRecords, layout.hitGroup);
    writeRegion(m_callableRecords, layout.callable);

    return layout.totalSize;
}

const SBTShaderRecord* ShaderBindingTableManager::GetRecord(SBTRecordType type, u32 index) const {
    std::lock_guard lock(m_mutex);

    const std::vector<SBTShaderRecord>* records = nullptr;
    switch (type) {
        case SBTRecordType::RayGen:   records = &m_rayGenRecords; break;
        case SBTRecordType::Miss:     records = &m_missRecords; break;
        case SBTRecordType::HitGroup: records = &m_hitGroupRecords; break;
        case SBTRecordType::Callable: records = &m_callableRecords; break;
    }

    if (!records || index >= records->size()) return nullptr;
    return &(*records)[index];
}

u32 ShaderBindingTableManager::GetRecordCount(SBTRecordType type) const {
    std::lock_guard lock(m_mutex);

    switch (type) {
        case SBTRecordType::RayGen:   return static_cast<u32>(m_rayGenRecords.size());
        case SBTRecordType::Miss:     return static_cast<u32>(m_missRecords.size());
        case SBTRecordType::HitGroup: return static_cast<u32>(m_hitGroupRecords.size());
        case SBTRecordType::Callable: return static_cast<u32>(m_callableRecords.size());
    }
    return 0;
}

void ShaderBindingTableManager::ClearRecords(SBTRecordType type) {
    std::lock_guard lock(m_mutex);

    switch (type) {
        case SBTRecordType::RayGen:   m_rayGenRecords.clear(); break;
        case SBTRecordType::Miss:     m_missRecords.clear(); break;
        case SBTRecordType::HitGroup: m_hitGroupRecords.clear(); break;
        case SBTRecordType::Callable: m_callableRecords.clear(); break;
    }
}

void ShaderBindingTableManager::ClearAll() {
    std::lock_guard lock(m_mutex);
    m_rayGenRecords.clear();
    m_missRecords.clear();
    m_hitGroupRecords.clear();
    m_callableRecords.clear();
}

void ShaderBindingTableManager::Reset() {
    ClearAll();
}

SBTStats ShaderBindingTableManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    SBTStats stats{};
    stats.rayGenRecords = static_cast<u32>(m_rayGenRecords.size());
    stats.missRecords = static_cast<u32>(m_missRecords.size());
    stats.hitGroupRecords = static_cast<u32>(m_hitGroupRecords.size());
    stats.callableRecords = static_cast<u32>(m_callableRecords.size());
    stats.totalRecords = stats.rayGenRecords + stats.missRecords +
                          stats.hitGroupRecords + stats.callableRecords;

    // Compute total size and wasted alignment
    SBTLayout layout = BuildLayout();
    stats.totalSizeBytes = layout.totalSize;

    u64 rawSize = 0;
    auto addRaw = [&](const std::vector<SBTShaderRecord>& records) {
        for (const auto& rec : records) {
            rawSize += m_config.handleSize + rec.inlineData.size();
        }
    };
    addRaw(m_rayGenRecords);
    addRaw(m_missRecords);
    addRaw(m_hitGroupRecords);
    addRaw(m_callableRecords);

    stats.wastedAlignment = layout.totalSize > rawSize ? layout.totalSize - rawSize : 0;

    return stats;
}

u32 ShaderBindingTableManager::AddRecord(SBTRecordType type, u64 handle, const void* data,
                                           u32 dataSize, const std::string& name) {
    std::lock_guard lock(m_mutex);

    u32 totalRecordSize = m_config.handleSize + dataSize;
    if (totalRecordSize > m_config.maxRecordSize) {
        NGE_LOG_ERROR("SBT record too large: {} bytes (max {})", totalRecordSize, m_config.maxRecordSize);
        return UINT32_MAX;
    }

    std::vector<SBTShaderRecord>* records = nullptr;
    switch (type) {
        case SBTRecordType::RayGen:   records = &m_rayGenRecords; break;
        case SBTRecordType::Miss:     records = &m_missRecords; break;
        case SBTRecordType::HitGroup: records = &m_hitGroupRecords; break;
        case SBTRecordType::Callable: records = &m_callableRecords; break;
    }

    u32 index = static_cast<u32>(records->size());

    SBTShaderRecord record;
    record.recordIndex = index;
    record.type = type;
    record.shaderGroupHandle = handle;
    record.debugName = name;

    if (data && dataSize > 0) {
        record.inlineData.resize(dataSize);
        std::memcpy(record.inlineData.data(), data, dataSize);
    }

    records->push_back(std::move(record));

    return index;
}

u64 ShaderBindingTableManager::AlignUp(u64 value, u64 alignment) const {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

u64 ShaderBindingTableManager::ComputeRecordStride(const std::vector<SBTShaderRecord>& records) const {
    if (records.empty()) return 0;

    // Stride must be the max record size, aligned to handleAlignment
    u64 maxRecordSize = m_config.handleSize;
    for (const auto& rec : records) {
        u64 size = m_config.handleSize + static_cast<u64>(rec.inlineData.size());
        if (size > maxRecordSize) maxRecordSize = size;
    }

    return AlignUp(maxRecordSize, m_config.handleAlignment);
}

SBTRegion ShaderBindingTableManager::ComputeRegion(const std::vector<SBTShaderRecord>& records,
                                                     u64 offset) const {
    SBTRegion region{};
    region.bufferOffset = AlignUp(offset, m_config.baseAlignment);
    region.recordCount = static_cast<u32>(records.size());

    if (records.empty()) {
        region.stride = 0;
        region.size = 0;
        return region;
    }

    region.stride = ComputeRecordStride(records);
    region.size = region.stride * region.recordCount;

    return region;
}

} // namespace nge::rhi
