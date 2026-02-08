#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_types.h"
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>
#include <memory>

namespace nge::renderer {

// ─── Render Graph ────────────────────────────────────────────────────────
// Declarative frame graph that automatically:
//   - Schedules render/compute passes in dependency order
//   - Manages transient resource lifetimes (allocate/alias/free)
//   - Inserts barriers between passes
//   - Culls unused passes (dead-code elimination)
//
// Usage:
//   RenderGraph graph(device);
//   auto& depthPass = graph.AddPass("DepthPrepass", PassType::Graphics);
//   auto depthTex = depthPass.CreateTexture("SceneDepth", depthDesc);
//   depthPass.WriteDepth(depthTex);
//   depthPass.SetExecute([&](rhi::ICommandList* cmd) { ... });
//
//   auto& lightPass = graph.AddPass("Lighting", PassType::Compute);
//   lightPass.Read(depthTex);
//   auto hdrTex = lightPass.CreateTexture("HDRColor", hdrDesc);
//   lightPass.Write(hdrTex);
//   lightPass.SetExecute([&](rhi::ICommandList* cmd) { ... });
//
//   graph.Compile();
//   graph.Execute(cmd);

// ─── Resource Handle ─────────────────────────────────────────────────────

struct RGResourceHandle {
    u32 index = UINT32_MAX;
    bool IsValid() const { return index != UINT32_MAX; }
    bool operator==(const RGResourceHandle& o) const { return index == o.index; }
    bool operator!=(const RGResourceHandle& o) const { return index != o.index; }
};

// ─── Resource Descriptor ─────────────────────────────────────────────────

struct RGTextureDesc {
    u32              width = 0;
    u32              height = 0;
    u32              depth = 1;
    u32              mipLevels = 1;
    rhi::Format      format = rhi::Format::RGBA8_UNORM;
    rhi::TextureUsage usage = rhi::TextureUsage::Sampled;
    std::string      name;
};

struct RGBufferDesc {
    usize            size = 0;
    rhi::BufferUsage usage = rhi::BufferUsage::Storage;
    std::string      name;
};

// ─── Pass Types ──────────────────────────────────────────────────────────

enum class PassType : u8 {
    Graphics,
    Compute,
    Transfer,
    AsyncCompute,
};

// ─── Pass Builder ────────────────────────────────────────────────────────

class RenderGraph;

class RGPassBuilder {
public:
    RGPassBuilder(RenderGraph& graph, u32 passIndex)
        : m_graph(graph), m_passIndex(passIndex) {}

    // Create transient resources
    RGResourceHandle CreateTexture(const std::string& name, const RGTextureDesc& desc);
    RGResourceHandle CreateBuffer(const std::string& name, const RGBufferDesc& desc);

    // Import external (persistent) resources
    RGResourceHandle ImportTexture(const std::string& name, rhi::TextureHandle handle,
                                    const RGTextureDesc& desc);
    RGResourceHandle ImportBuffer(const std::string& name, rhi::BufferHandle handle,
                                    const RGBufferDesc& desc);

    // Declare resource usage
    void Read(RGResourceHandle resource, rhi::ResourceState state = rhi::ResourceState::ShaderRead);
    void Write(RGResourceHandle resource, rhi::ResourceState state = rhi::ResourceState::ShaderWrite);
    void ReadWrite(RGResourceHandle resource);
    void WriteDepth(RGResourceHandle resource);
    void WriteColor(RGResourceHandle resource, u32 attachmentIndex = 0);

    // Set execution callback
    using ExecuteFunc = std::function<void(rhi::ICommandList*)>;
    void SetExecute(ExecuteFunc func);

    // Viewport/scissor for graphics passes
    void SetViewport(u32 width, u32 height);

private:
    RenderGraph& m_graph;
    u32 m_passIndex;
};

// ─── Internal Structures ─────────────────────────────────────────────────

struct RGResource {
    enum class Type : u8 { Texture, Buffer };

    Type             type;
    std::string      name;
    RGTextureDesc    texDesc;
    RGBufferDesc     bufDesc;
    bool             imported = false;
    bool             transient = true;

    // Physical resources (allocated during compile)
    rhi::TextureHandle physicalTexture;
    rhi::BufferHandle  physicalBuffer;

    // Lifetime tracking
    u32 firstPass = UINT32_MAX;
    u32 lastPass = 0;
    u32 refCount = 0;
};

struct RGPass {
    std::string    name;
    PassType       type;
    u32            index;

    struct ResourceAccess {
        RGResourceHandle handle;
        rhi::ResourceState state;
        bool writing;
    };

    std::vector<ResourceAccess> reads;
    std::vector<ResourceAccess> writes;
    std::vector<RGResourceHandle> creates;

    RGPassBuilder::ExecuteFunc executeFunc;

    u32 viewportWidth = 0;
    u32 viewportHeight = 0;

    // Compile state
    bool culled = false;
    u32  executionOrder = 0;

    // Async compute state
    bool asyncCompute = false;       // Runs on async compute queue
    bool needsSignal = false;        // Must signal semaphore after execution
    bool needsWait = false;          // Must wait on semaphore before execution
    u32  signalValue = 0;            // Timeline semaphore value to signal
    u32  waitValue = 0;              // Timeline semaphore value to wait on
};

// ─── Render Graph ────────────────────────────────────────────────────────

class RenderGraph {
public:
    explicit RenderGraph(rhi::IDevice* device);
    ~RenderGraph();

    // Build phase: add passes and declare resources
    RGPassBuilder& AddPass(const std::string& name, PassType type = PassType::Graphics);

    // Compile: topological sort, cull unused, allocate transient resources, insert barriers
    bool Compile();

    // Execute: run all passes in order on graphics queue
    void Execute(rhi::ICommandList* cmd);

    // Execute with separate async compute command list
    void Execute(rhi::ICommandList* graphicsCmd, rhi::ICommandList* asyncComputeCmd);

    // Reset for next frame
    void Reset();

    // Debug
    void DumpGraph() const;
    u32 GetPassCount() const { return static_cast<u32>(m_passes.size()); }
    u32 GetResourceCount() const { return static_cast<u32>(m_resources.size()); }
    u32 GetCulledPassCount() const;

    // Resource access (used by RGPassBuilder)
    RGResourceHandle AddResource(RGResource resource);
    RGResource& GetResource(RGResourceHandle handle) { return m_resources[handle.index]; }
    RGPass& GetPass(u32 index) { return m_passes[index]; }

private:
    void TopologicalSort();
    void CullUnused();
    void AllocateTransientResources();
    void ComputeBarriers();
    void InsertBarrier(rhi::ICommandList* cmd, RGResourceHandle handle,
                        rhi::ResourceState from, rhi::ResourceState to);

    rhi::IDevice* m_device;

    std::vector<RGPass>       m_passes;
    std::vector<RGResource>   m_resources;
    std::vector<std::unique_ptr<RGPassBuilder>> m_builders;

    // Compiled execution order
    std::vector<u32> m_executionOrder;
    std::vector<u32> m_asyncExecutionOrder; // Async compute passes
    bool m_compiled = false;

    // Resource state tracking for barrier insertion
    std::unordered_map<u32, rhi::ResourceState> m_resourceStates;

    // Cross-queue synchronization
    u32 m_nextSemaphoreValue = 1;
    void ClassifyAsyncPasses();
    void InsertCrossQueueSync();
};

} // namespace nge::renderer
