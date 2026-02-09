#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>

namespace nge::rhi {

// ─── GPU Render Pass Dependency Analyzer ─────────────────────────────────
// Automatically infers subpass dependencies from resource read/write
// declarations. Generates VkSubpassDependency arrays and detects
// hazards (WAW, RAW, WAR) between passes.
//
// Use cases:
//   - Render graph pass ordering validation
//   - Automatic VkSubpassDependency generation
//   - Cross-queue dependency detection
//   - Hazard identification for debugging

enum class AccessType : u8 {
    Read,
    Write,
    ReadWrite,
};

enum class PipelineStage : u32 {
    TopOfPipe       = 0x00000001,
    VertexInput     = 0x00000004,
    VertexShader    = 0x00000008,
    FragmentShader  = 0x00000080,
    EarlyFragTest   = 0x00000100,
    LateFragTest    = 0x00000200,
    ColorOutput     = 0x00000400,
    ComputeShader   = 0x00000800,
    Transfer        = 0x00001000,
    BottomOfPipe    = 0x00002000,
    AllGraphics     = 0x00008000,
    AllCommands     = 0x00010000,
};

inline PipelineStage operator|(PipelineStage a, PipelineStage b) {
    return static_cast<PipelineStage>(static_cast<u32>(a) | static_cast<u32>(b));
}

struct ResourceAccess {
    u64           resourceId;
    std::string   resourceName;
    AccessType    access;
    PipelineStage stage;
};

struct PassDeclaration {
    u32                        passIndex;
    std::string                passName;
    u32                        queueFamily;   // 0=graphics, 1=compute, 2=transfer
    std::vector<ResourceAccess> accesses;
};

struct InferredDependency {
    u32           srcPass;
    u32           dstPass;
    std::string   srcPassName;
    std::string   dstPassName;
    PipelineStage srcStage;
    PipelineStage dstStage;
    u64           resourceId;
    std::string   resourceName;
    std::string   hazardType;    // "RAW", "WAR", "WAW"
    bool          isCrossQueue;
};

struct PassDependencyConfig {
    bool detectCrossQueue = true;
    bool warnOnRedundant = true;
    u32  maxPasses = 256;
};

struct PassDependencyStats {
    u32 totalPasses;
    u32 totalDependencies;
    u32 rawHazards;
    u32 warHazards;
    u32 wawHazards;
    u32 crossQueueDeps;
    u32 redundantDepsRemoved;
};

class PassDependencyAnalyzer {
public:
    bool Init(const PassDependencyConfig& config = {});
    void Shutdown();

    // Declare passes with their resource accesses
    void DeclarePass(const PassDeclaration& pass);

    // Run analysis: infer all dependencies
    void Analyze();

    // Get inferred dependencies
    const std::vector<InferredDependency>& GetDependencies() const;

    // Get dependencies for a specific destination pass
    std::vector<InferredDependency> GetDependenciesForPass(u32 passIndex) const;

    // Check if adding a pass would create a cycle
    bool WouldCreateCycle(u32 srcPass, u32 dstPass) const;

    // Get topological ordering of passes
    std::vector<u32> GetTopologicalOrder() const;

    // Clear for next frame
    void Clear();

    PassDependencyStats GetStats() const;

private:
    bool HasPath(u32 from, u32 to, const std::unordered_map<u32, std::vector<u32>>& adj) const;
    void RemoveRedundantDeps();

    PassDependencyConfig m_config;
    std::vector<PassDeclaration> m_passes;
    std::vector<InferredDependency> m_dependencies;

    u32 m_redundantRemoved = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
