#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <mutex>

namespace nge::rhi {

// ─── GPU Shader Binding Table Manager (Ray Tracing SBT) ─────────────────
// Manages the construction and layout of Shader Binding Tables for
// VK_KHR_ray_tracing_pipeline. Handles alignment requirements, record
// packing, and hit group/miss/callable shader organization.
//
// Use cases:
//   - Build SBT for ray tracing dispatch (vkCmdTraceRaysKHR)
//   - Manage ray gen, miss, hit group, and callable shader records
//   - Handle per-record shader data (inline constants)
//   - Enforce alignment to shaderGroupHandleAlignment
//   - Support dynamic SBT rebuilds on material changes

enum class SBTRecordType : u8 {
    RayGen,
    Miss,
    HitGroup,
    Callable,
};

struct SBTShaderRecord {
    u32                recordIndex;
    SBTRecordType      type;
    u64                shaderGroupHandle;  // Opaque handle from pipeline
    std::vector<u8>    inlineData;         // Per-record constants
    std::string        debugName;
};

struct SBTRegion {
    u64 bufferOffset;     // Offset into SBT buffer
    u64 stride;           // Bytes per record (aligned)
    u64 size;             // Total region size
    u32 recordCount;
};

struct SBTLayout {
    SBTRegion rayGen;
    SBTRegion miss;
    SBTRegion hitGroup;
    SBTRegion callable;
    u64       totalSize;
};

struct SBTConfig {
    u32 handleSize = 32;              // shaderGroupHandleSize (device query)
    u32 handleAlignment = 64;         // shaderGroupHandleAlignment
    u32 baseAlignment = 64;           // shaderGroupBaseAlignment
    u32 maxRecordSize = 4096;         // Max bytes per record (handle + inline data)
    u32 maxRecords = 1024;
};

struct SBTStats {
    u32 rayGenRecords;
    u32 missRecords;
    u32 hitGroupRecords;
    u32 callableRecords;
    u32 totalRecords;
    u64 totalSizeBytes;
    u64 wastedAlignment;   // Bytes lost to alignment padding
};

class ShaderBindingTableManager {
public:
    bool Init(const SBTConfig& config = {});
    void Shutdown();

    // Add shader records
    u32 AddRayGenRecord(u64 shaderGroupHandle, const void* inlineData = nullptr,
                         u32 inlineDataSize = 0, const std::string& name = "");
    u32 AddMissRecord(u64 shaderGroupHandle, const void* inlineData = nullptr,
                       u32 inlineDataSize = 0, const std::string& name = "");
    u32 AddHitGroupRecord(u64 shaderGroupHandle, const void* inlineData = nullptr,
                           u32 inlineDataSize = 0, const std::string& name = "");
    u32 AddCallableRecord(u64 shaderGroupHandle, const void* inlineData = nullptr,
                           u32 inlineDataSize = 0, const std::string& name = "");

    // Build the SBT layout (compute offsets, strides, sizes)
    SBTLayout BuildLayout() const;

    // Write the SBT data into a destination buffer
    u64 WriteToBuffer(void* dstBuffer, u64 dstSize) const;

    // Get a specific record
    const SBTShaderRecord* GetRecord(SBTRecordType type, u32 index) const;

    // Get record count by type
    u32 GetRecordCount(SBTRecordType type) const;

    // Remove all records of a given type
    void ClearRecords(SBTRecordType type);

    // Clear all records
    void ClearAll();

    void Reset();

    SBTStats GetStats() const;

private:
    u32 AddRecord(SBTRecordType type, u64 handle, const void* data, u32 dataSize,
                   const std::string& name);
    u64 AlignUp(u64 value, u64 alignment) const;
    u64 ComputeRecordStride(const std::vector<SBTShaderRecord>& records) const;
    SBTRegion ComputeRegion(const std::vector<SBTShaderRecord>& records, u64 offset) const;

    SBTConfig m_config;

    std::vector<SBTShaderRecord> m_rayGenRecords;
    std::vector<SBTShaderRecord> m_missRecords;
    std::vector<SBTShaderRecord> m_hitGroupRecords;
    std::vector<SBTShaderRecord> m_callableRecords;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
