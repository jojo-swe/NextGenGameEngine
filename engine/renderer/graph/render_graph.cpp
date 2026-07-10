#include "engine/renderer/graph/render_graph.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <algorithm>
#include <queue>
#include <unordered_set>

namespace nge::renderer {

// ─── RGPassBuilder ───────────────────────────────────────────────────────

RGResourceHandle RGPassBuilder::CreateTexture(const std::string& name, const RGTextureDesc& desc) {
    RGResource res;
    res.type = RGResource::Type::Texture;
    res.name = name;
    res.texDesc = desc;
    res.transient = true;
    res.imported = false;
    auto handle = m_graph.AddResource(std::move(res));
    m_graph.GetPass(m_passIndex).creates.push_back(handle);
    return handle;
}

RGResourceHandle RGPassBuilder::CreateBuffer(const std::string& name, const RGBufferDesc& desc) {
    RGResource res;
    res.type = RGResource::Type::Buffer;
    res.name = name;
    res.bufDesc = desc;
    res.transient = true;
    res.imported = false;
    auto handle = m_graph.AddResource(std::move(res));
    m_graph.GetPass(m_passIndex).creates.push_back(handle);
    return handle;
}

RGResourceHandle RGPassBuilder::ImportTexture(const std::string& name, rhi::TextureHandle texHandle,
                                                const RGTextureDesc& desc) {
    RGResource res;
    res.type = RGResource::Type::Texture;
    res.name = name;
    res.texDesc = desc;
    res.transient = false;
    res.imported = true;
    res.physicalTexture = texHandle;
    return m_graph.AddResource(std::move(res));
}

RGResourceHandle RGPassBuilder::ImportBuffer(const std::string& name, rhi::BufferHandle bufHandle,
                                               const RGBufferDesc& desc) {
    RGResource res;
    res.type = RGResource::Type::Buffer;
    res.name = name;
    res.bufDesc = desc;
    res.transient = false;
    res.imported = true;
    res.physicalBuffer = bufHandle;
    return m_graph.AddResource(std::move(res));
}

void RGPassBuilder::Read(RGResourceHandle resource, rhi::ResourceState state) {
    auto& pass = m_graph.GetPass(m_passIndex);
    pass.reads.push_back({resource, state, false});
    m_graph.GetResource(resource).refCount++;
    m_graph.GetResource(resource).lastPass = math::Max(m_graph.GetResource(resource).lastPass, m_passIndex);
    if (m_graph.GetResource(resource).firstPass == UINT32_MAX) {
        m_graph.GetResource(resource).firstPass = m_passIndex;
    }
}

void RGPassBuilder::Write(RGResourceHandle resource, rhi::ResourceState state) {
    auto& pass = m_graph.GetPass(m_passIndex);
    pass.writes.push_back({resource, state, true});
    m_graph.GetResource(resource).refCount++;
    m_graph.GetResource(resource).lastPass = math::Max(m_graph.GetResource(resource).lastPass, m_passIndex);
    if (m_graph.GetResource(resource).firstPass == UINT32_MAX) {
        m_graph.GetResource(resource).firstPass = m_passIndex;
    }
}

void RGPassBuilder::ReadWrite(RGResourceHandle resource) {
    Read(resource, rhi::ResourceState::ShaderRead);
    Write(resource, rhi::ResourceState::ShaderWrite);
}

void RGPassBuilder::WriteDepth(RGResourceHandle resource) {
    Write(resource, rhi::ResourceState::DepthWrite);
}

void RGPassBuilder::WriteColor(RGResourceHandle resource, u32 /*attachmentIndex*/) {
    Write(resource, rhi::ResourceState::RenderTarget);
}

void RGPassBuilder::SetExecute(ExecuteFunc func) {
    m_graph.GetPass(m_passIndex).executeFunc = std::move(func);
}

void RGPassBuilder::SetViewport(u32 width, u32 height) {
    auto& pass = m_graph.GetPass(m_passIndex);
    pass.viewportWidth = width;
    pass.viewportHeight = height;
}

// ─── RenderGraph ─────────────────────────────────────────────────────────

RenderGraph::RenderGraph(rhi::IDevice* device) : m_device(device) {}

RenderGraph::~RenderGraph() {
    Reset();
}

RGPassBuilder& RenderGraph::AddPass(const std::string& name, PassType type) {
    u32 passIndex = static_cast<u32>(m_passes.size());

    RGPass pass;
    pass.name = name;
    pass.type = type;
    pass.index = passIndex;
    m_passes.push_back(std::move(pass));

    auto builder = std::make_unique<RGPassBuilder>(*this, passIndex);
    auto* ptr = builder.get();
    m_builders.push_back(std::move(builder));
    return *ptr;
}

RGResourceHandle RenderGraph::AddResource(RGResource resource) {
    RGResourceHandle handle;
    handle.index = static_cast<u32>(m_resources.size());
    m_resources.push_back(std::move(resource));
    return handle;
}

bool RenderGraph::Compile() {
    if (m_passes.empty()) return true;

    TopologicalSort();
    CullUnused();
    AllocateTransientResources();
    ClassifyAsyncPasses();
    InsertCrossQueueSync();

    m_compiled = true;

    // Only read by NGE_LOG_DEBUG, which is compiled out above the debug log level.
    [[maybe_unused]] u32 culled = GetCulledPassCount();
    [[maybe_unused]] u32 asyncCount = static_cast<u32>(m_asyncExecutionOrder.size());
    NGE_LOG_DEBUG("Render graph compiled: {} passes ({} active, {} culled, {} async), {} resources",
                  m_passes.size(), m_passes.size() - culled, culled, asyncCount, m_resources.size());
    return true;
}

void RenderGraph::TopologicalSort() {
    u32 passCount = static_cast<u32>(m_passes.size());

    // Build adjacency: pass A → pass B if B reads a resource that A writes
    std::vector<std::vector<u32>> adjacency(passCount);
    std::vector<u32> inDegree(passCount, 0);

    for (u32 b = 0; b < passCount; ++b) {
        for (const auto& read : m_passes[b].reads) {
            // Find which pass writes this resource
            for (u32 a = 0; a < passCount; ++a) {
                if (a == b) continue;
                for (const auto& write : m_passes[a].writes) {
                    if (write.handle == read.handle) {
                        adjacency[a].push_back(b);
                        inDegree[b]++;
                    }
                }
            }
        }
    }

    // Kahn's algorithm
    std::queue<u32> queue;
    for (u32 i = 0; i < passCount; ++i) {
        if (inDegree[i] == 0) queue.push(i);
    }

    m_executionOrder.clear();
    m_executionOrder.reserve(passCount);

    while (!queue.empty()) {
        u32 node = queue.front();
        queue.pop();
        m_executionOrder.push_back(node);

        for (u32 neighbor : adjacency[node]) {
            if (--inDegree[neighbor] == 0) {
                queue.push(neighbor);
            }
        }
    }

    // Assign execution order to passes
    for (u32 i = 0; i < static_cast<u32>(m_executionOrder.size()); ++i) {
        m_passes[m_executionOrder[i]].executionOrder = i;
    }

    // If not all passes were reached, there's a cycle
    if (m_executionOrder.size() != passCount) {
        NGE_LOG_ERROR("Render graph has a dependency cycle!");
    }
}

void RenderGraph::CullUnused() {
    // Mark passes as culled if they don't contribute to any output
    // Start from passes that write to imported (external) resources and trace backwards

    std::unordered_set<u32> needed;

    // All passes that write to imported resources are needed
    for (u32 i = 0; i < static_cast<u32>(m_passes.size()); ++i) {
        for (const auto& write : m_passes[i].writes) {
            if (!m_resources[write.handle.index].transient) {
                needed.insert(i);
            }
        }
    }

    // Also keep the last pass (usually presents to screen)
    if (!m_passes.empty()) {
        needed.insert(static_cast<u32>(m_passes.size()) - 1);
    }

    // Trace backwards: if pass B is needed and reads from pass A's output, A is needed too
    bool changed = true;
    while (changed) {
        changed = false;
        for (u32 b : needed) {
            for (const auto& read : m_passes[b].reads) {
                for (u32 a = 0; a < static_cast<u32>(m_passes.size()); ++a) {
                    if (needed.count(a)) continue;
                    for (const auto& write : m_passes[a].writes) {
                        if (write.handle == read.handle) {
                            needed.insert(a);
                            changed = true;
                        }
                    }
                }
            }
        }
    }

    for (u32 i = 0; i < static_cast<u32>(m_passes.size()); ++i) {
        m_passes[i].culled = (needed.count(i) == 0);
        if (m_passes[i].culled) {
            NGE_LOG_DEBUG("Render graph: culled pass '{}'", m_passes[i].name);
        }
    }
}

void RenderGraph::AllocateTransientResources() {
    for (auto& res : m_resources) {
        if (!res.transient || res.imported) continue;
        if (res.refCount == 0) continue; // Unused resource

        if (res.type == RGResource::Type::Texture) {
            rhi::TextureDesc desc;
            desc.width = res.texDesc.width;
            desc.height = res.texDesc.height;
            desc.depth = res.texDesc.depth;
            desc.mipLevels = res.texDesc.mipLevels;
            desc.format = res.texDesc.format;
            desc.usage = res.texDesc.usage;
            desc.debugName = res.name.c_str();
            res.physicalTexture = m_device->CreateTexture(desc);
        } else {
            rhi::BufferDesc desc;
            desc.size = res.bufDesc.size;
            desc.usage = res.bufDesc.usage;
            desc.memoryUsage = rhi::MemoryUsage::GPU_Only;
            desc.debugName = res.name.c_str();
            res.physicalBuffer = m_device->CreateBuffer(desc);
        }
    }
}

void RenderGraph::ComputeBarriers() {
    // Initialize all resource states to Undefined
    m_resourceStates.clear();
    for (u32 i = 0; i < static_cast<u32>(m_resources.size()); ++i) {
        m_resourceStates[i] = rhi::ResourceState::Undefined;
    }
}

void RenderGraph::InsertBarrier(rhi::ICommandList* cmd, RGResourceHandle handle,
                                  rhi::ResourceState from, rhi::ResourceState to) {
    if (from == to) return;

    auto& res = m_resources[handle.index];
    if (res.type == RGResource::Type::Texture) {
        cmd->TextureBarrier(res.physicalTexture, from, to);
    } else {
        cmd->BufferBarrier(res.physicalBuffer, from, to);
    }
}

void RenderGraph::Execute(rhi::ICommandList* cmd) {
    if (!m_compiled) {
        NGE_LOG_ERROR("Render graph not compiled before execution!");
        return;
    }

    ComputeBarriers();

    for (u32 passIdx : m_executionOrder) {
        auto& pass = m_passes[passIdx];
        if (pass.culled) continue;
        if (!pass.executeFunc) continue;

        cmd->BeginDebugLabel(pass.name.c_str(), 0.2f, 0.6f, 0.9f);

        // Insert barriers for reads
        for (const auto& access : pass.reads) {
            rhi::ResourceState currentState = m_resourceStates[access.handle.index];
            if (currentState != access.state) {
                InsertBarrier(cmd, access.handle, currentState, access.state);
                m_resourceStates[access.handle.index] = access.state;
            }
        }

        // Insert barriers for writes
        for (const auto& access : pass.writes) {
            rhi::ResourceState currentState = m_resourceStates[access.handle.index];
            if (currentState != access.state) {
                InsertBarrier(cmd, access.handle, currentState, access.state);
                m_resourceStates[access.handle.index] = access.state;
            }
        }

        // Execute the pass
        pass.executeFunc(cmd);

        cmd->EndDebugLabel();
    }
}

void RenderGraph::Execute(rhi::ICommandList* graphicsCmd, rhi::ICommandList* asyncComputeCmd) {
    if (!m_compiled) {
        NGE_LOG_ERROR("Render graph not compiled before execution!");
        return;
    }

    ComputeBarriers();

    // Execute graphics queue passes
    for (u32 passIdx : m_executionOrder) {
        auto& pass = m_passes[passIdx];
        if (pass.culled || pass.asyncCompute) continue;
        if (!pass.executeFunc) continue;

        // Wait for async compute if needed
        if (pass.needsWait && asyncComputeCmd) {
            graphicsCmd->WaitSemaphore(pass.waitValue);
        }

        graphicsCmd->BeginDebugLabel(pass.name.c_str(), 0.2f, 0.6f, 0.9f);

        for (const auto& access : pass.reads) {
            rhi::ResourceState currentState = m_resourceStates[access.handle.index];
            if (currentState != access.state) {
                InsertBarrier(graphicsCmd, access.handle, currentState, access.state);
                m_resourceStates[access.handle.index] = access.state;
            }
        }
        for (const auto& access : pass.writes) {
            rhi::ResourceState currentState = m_resourceStates[access.handle.index];
            if (currentState != access.state) {
                InsertBarrier(graphicsCmd, access.handle, currentState, access.state);
                m_resourceStates[access.handle.index] = access.state;
            }
        }

        pass.executeFunc(graphicsCmd);
        graphicsCmd->EndDebugLabel();
    }

    // Execute async compute passes
    if (asyncComputeCmd) {
        for (u32 passIdx : m_asyncExecutionOrder) {
            auto& pass = m_passes[passIdx];
            if (pass.culled) continue;
            if (!pass.executeFunc) continue;

            if (pass.needsWait) {
                asyncComputeCmd->WaitSemaphore(pass.waitValue);
            }

            asyncComputeCmd->BeginDebugLabel(pass.name.c_str(), 0.9f, 0.4f, 0.2f);

            for (const auto& access : pass.reads) {
                rhi::ResourceState currentState = m_resourceStates[access.handle.index];
                if (currentState != access.state) {
                    InsertBarrier(asyncComputeCmd, access.handle, currentState, access.state);
                    m_resourceStates[access.handle.index] = access.state;
                }
            }
            for (const auto& access : pass.writes) {
                rhi::ResourceState currentState = m_resourceStates[access.handle.index];
                if (currentState != access.state) {
                    InsertBarrier(asyncComputeCmd, access.handle, currentState, access.state);
                    m_resourceStates[access.handle.index] = access.state;
                }
            }

            pass.executeFunc(asyncComputeCmd);
            asyncComputeCmd->EndDebugLabel();

            if (pass.needsSignal) {
                asyncComputeCmd->SignalSemaphore(pass.signalValue);
            }
        }
    }
}

void RenderGraph::ClassifyAsyncPasses() {
    m_asyncExecutionOrder.clear();

    for (u32 passIdx : m_executionOrder) {
        auto& pass = m_passes[passIdx];
        if (pass.culled) continue;

        if (pass.type == PassType::AsyncCompute) {
            pass.asyncCompute = true;
            m_asyncExecutionOrder.push_back(passIdx);
        }
    }

    // Remove async passes from the main execution order
    m_executionOrder.erase(
        std::remove_if(m_executionOrder.begin(), m_executionOrder.end(),
                        [this](u32 idx) { return m_passes[idx].asyncCompute; }),
        m_executionOrder.end());
}

void RenderGraph::InsertCrossQueueSync() {
    m_nextSemaphoreValue = 1;

    // For each async compute pass, check if any graphics pass reads its output
    for (u32 asyncIdx : m_asyncExecutionOrder) {
        auto& asyncPass = m_passes[asyncIdx];

        for (const auto& write : asyncPass.writes) {
            // Find graphics passes that read this resource
            for (u32 gfxIdx : m_executionOrder) {
                auto& gfxPass = m_passes[gfxIdx];
                if (gfxPass.culled) continue;

                for (const auto& read : gfxPass.reads) {
                    if (read.handle == write.handle) {
                        // Async must signal, graphics must wait
                        asyncPass.needsSignal = true;
                        asyncPass.signalValue = m_nextSemaphoreValue;
                        gfxPass.needsWait = true;
                        gfxPass.waitValue = m_nextSemaphoreValue;
                        m_nextSemaphoreValue++;
                    }
                }
            }
        }

        // Also check if async pass reads from graphics pass output
        for (const auto& read : asyncPass.reads) {
            for (u32 gfxIdx : m_executionOrder) {
                auto& gfxPass = m_passes[gfxIdx];
                if (gfxPass.culled) continue;

                for (const auto& write : gfxPass.writes) {
                    if (write.handle == read.handle) {
                        gfxPass.needsSignal = true;
                        gfxPass.signalValue = m_nextSemaphoreValue;
                        asyncPass.needsWait = true;
                        asyncPass.waitValue = m_nextSemaphoreValue;
                        m_nextSemaphoreValue++;
                    }
                }
            }
        }
    }
}

void RenderGraph::Reset() {
    // Free transient resources
    for (auto& res : m_resources) {
        if (!res.transient) continue;
        if (res.type == RGResource::Type::Texture && res.physicalTexture.IsValid()) {
            m_device->DestroyTexture(res.physicalTexture);
        }
        if (res.type == RGResource::Type::Buffer && res.physicalBuffer.IsValid()) {
            m_device->DestroyBuffer(res.physicalBuffer);
        }
    }

    m_passes.clear();
    m_resources.clear();
    m_builders.clear();
    m_executionOrder.clear();
    m_asyncExecutionOrder.clear();
    m_resourceStates.clear();
    m_compiled = false;
    m_nextSemaphoreValue = 1;
}

void RenderGraph::DumpGraph() const {
    NGE_LOG_INFO("=== Render Graph ===");
    NGE_LOG_INFO("Passes: {}", m_passes.size());
    for (const auto& pass : m_passes) {
        NGE_LOG_INFO("  [{}] '{}' ({}{})",
                     pass.executionOrder, pass.name,
                     pass.type == PassType::Graphics ? "Graphics" :
                     pass.type == PassType::Compute ? "Compute" :
                     pass.type == PassType::Transfer ? "Transfer" : "AsyncCompute",
                     pass.culled ? ", CULLED" : "");
        for (const auto& r : pass.reads) {
            NGE_LOG_INFO("    reads: '{}'", m_resources[r.handle.index].name);
        }
        for (const auto& w : pass.writes) {
            NGE_LOG_INFO("    writes: '{}'", m_resources[w.handle.index].name);
        }
    }
    NGE_LOG_INFO("Resources: {}", m_resources.size());
    for (const auto& res : m_resources) {
        NGE_LOG_INFO("  '{}' ({}{}refs={})",
                     res.name,
                     res.type == RGResource::Type::Texture ? "Texture" : "Buffer",
                     res.transient ? ", transient, " : ", imported, ",
                     res.refCount);
    }
}

u32 RenderGraph::GetCulledPassCount() const {
    u32 count = 0;
    for (const auto& pass : m_passes) {
        if (pass.culled) count++;
    }
    return count;
}

} // namespace nge::renderer
