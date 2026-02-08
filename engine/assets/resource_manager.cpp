#include "engine/assets/resource_manager.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <algorithm>
#include <fstream>
#include <filesystem>

namespace nge::assets {

bool ResourceManager::Init(rhi::IDevice* device, u32 streamingBudgetMB) {
    m_device = device;
    m_streamingBudget = static_cast<u64>(streamingBudgetMB) * 1024 * 1024;
    m_totalMemoryUsage = 0;
    m_gpuMemoryUsage = 0;

    NGE_LOG_INFO("Resource manager initialized: {} MB streaming budget", streamingBudgetMB);
    return true;
}

void ResourceManager::Shutdown() {
    std::lock_guard lock(m_mutex);

    for (auto& [id, entry] : m_resources) {
        if (entry.data) {
            // TODO: Type-specific cleanup
            entry.data = nullptr;
        }
        if (entry.gpuBuffer.IsValid() && m_device) {
            m_device->DestroyBuffer(entry.gpuBuffer);
        }
        if (entry.gpuTexture.IsValid() && m_device) {
            m_device->DestroyTexture(entry.gpuTexture);
        }
    }

    m_resources.clear();
    m_pathToId.clear();
    m_loadQueue.clear();
    m_gpuUploadQueue.clear();
    m_device = nullptr;
}

ResourceId ResourceManager::GenerateId(const std::string& path) const {
    // FNV-1a hash of the path
    u64 hash = 14695981039346656037ULL;
    for (char c : path) {
        hash ^= static_cast<u64>(c);
        hash *= 1099511628211ULL;
    }
    return hash == INVALID_RESOURCE ? 1 : hash;
}

ResourceId ResourceManager::LoadSync(ResourceType type, const std::string& path) {
    // Check if already loaded
    ResourceId existing = FindByPath(path);
    if (existing != INVALID_RESOURCE) {
        AddRef(existing);
        return existing;
    }

    ResourceId id = GenerateId(path);

    ResourceEntry entry;
    entry.id = id;
    entry.type = type;
    entry.path = path;
    entry.name = std::filesystem::path(path).stem().string();
    entry.state = ResourceState::Loading;
    entry.refCount = 1;

    bool loaded = false;
    switch (type) {
        case ResourceType::Mesh:     loaded = LoadMesh(entry); break;
        case ResourceType::Texture:  loaded = LoadTexture(entry); break;
        case ResourceType::Shader:   loaded = LoadShader(entry); break;
        case ResourceType::Sound:    loaded = LoadSound(entry); break;
        default:
            NGE_LOG_WARN("ResourceManager: unsupported type for sync load");
            break;
    }

    if (loaded) {
        entry.state = ResourceState::Loaded;

        // GPU upload if applicable
        bool uploaded = false;
        switch (type) {
            case ResourceType::Mesh:    uploaded = UploadMeshToGPU(entry); break;
            case ResourceType::Texture: uploaded = UploadTextureToGPU(entry); break;
            default: uploaded = true; break;
        }

        entry.state = uploaded ? ResourceState::Ready : ResourceState::Loaded;
    } else {
        entry.state = ResourceState::Failed;
    }

    {
        std::lock_guard lock(m_mutex);
        m_resources[id] = std::move(entry);
        m_pathToId[path] = id;
    }

    NGE_LOG_INFO("Loaded resource '{}' ({}): {}", path,
                 static_cast<u32>(type), loaded ? "OK" : "FAILED");
    return loaded ? id : INVALID_RESOURCE;
}

ResourceId ResourceManager::LoadAsync(ResourceType type, const std::string& path,
                                        std::function<void(ResourceId, bool)> onComplete) {
    ResourceId existing = FindByPath(path);
    if (existing != INVALID_RESOURCE) {
        AddRef(existing);
        if (onComplete) onComplete(existing, true);
        return existing;
    }

    ResourceId id = GenerateId(path);

    {
        std::lock_guard lock(m_mutex);
        ResourceEntry entry;
        entry.id = id;
        entry.type = type;
        entry.path = path;
        entry.name = std::filesystem::path(path).stem().string();
        entry.state = ResourceState::Loading;
        entry.refCount = 1;
        m_resources[id] = std::move(entry);
        m_pathToId[path] = id;
    }

    ResourceLoadRequest req;
    req.id = id;
    req.type = type;
    req.path = path;
    req.onComplete = std::move(onComplete);

    {
        std::lock_guard lock(m_mutex);
        m_loadQueue.push_back(std::move(req));
    }

    return id;
}

void ResourceManager::Unload(ResourceId id) {
    Release(id);
}

void ResourceManager::AddRef(ResourceId id) {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(id);
    if (it != m_resources.end()) {
        it->second.refCount++;
    }
}

void ResourceManager::Release(ResourceId id) {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(id);
    if (it == m_resources.end()) return;

    if (it->second.refCount > 0) it->second.refCount--;

    // Don't immediately free — let LRU eviction handle it
}

ResourceState ResourceManager::GetState(ResourceId id) const {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(id);
    return (it != m_resources.end()) ? it->second.state : ResourceState::Unloaded;
}

const ResourceEntry* ResourceManager::GetEntry(ResourceId id) const {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(id);
    return (it != m_resources.end()) ? &it->second : nullptr;
}

ResourceEntry* ResourceManager::GetEntryMutable(ResourceId id) {
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(id);
    return (it != m_resources.end()) ? &it->second : nullptr;
}

bool ResourceManager::IsReady(ResourceId id) const {
    return GetState(id) == ResourceState::Ready;
}

rhi::BufferHandle ResourceManager::GetBuffer(ResourceId id) const {
    auto* entry = GetEntry(id);
    return entry ? entry->gpuBuffer : rhi::BufferHandle{};
}

rhi::TextureHandle ResourceManager::GetTexture(ResourceId id) const {
    auto* entry = GetEntry(id);
    return entry ? entry->gpuTexture : rhi::TextureHandle{};
}

ResourceId ResourceManager::FindByPath(const std::string& path) const {
    std::lock_guard lock(m_mutex);
    auto it = m_pathToId.find(path);
    return (it != m_pathToId.end()) ? it->second : INVALID_RESOURCE;
}

void ResourceManager::Update(u64 frameIndex) {
    m_currentFrame = frameIndex;

    ProcessLoadQueue();
    ProcessGPUUploads();

    // Update last-used frame for ready resources
    {
        std::lock_guard lock(m_mutex);
        for (auto& [id, entry] : m_resources) {
            if (entry.refCount > 0 && entry.state == ResourceState::Ready) {
                entry.lastUsedFrame = frameIndex;
            }
        }
    }

    // Evict if over budget
    if (m_totalMemoryUsage > m_streamingBudget) {
        EvictLRU();
    }
}

void ResourceManager::ProcessLoadQueue() {
    std::vector<ResourceLoadRequest> pending;
    {
        std::lock_guard lock(m_mutex);
        std::swap(pending, m_loadQueue);
    }

    // Sort by priority (higher first)
    std::sort(pending.begin(), pending.end(),
        [](const ResourceLoadRequest& a, const ResourceLoadRequest& b) {
            return a.priority > b.priority;
        });

    for (auto& req : pending) {
        auto* entry = GetEntryMutable(req.id);
        if (!entry) continue;

        bool loaded = false;
        switch (req.type) {
            case ResourceType::Mesh:    loaded = LoadMesh(*entry); break;
            case ResourceType::Texture: loaded = LoadTexture(*entry); break;
            case ResourceType::Shader:  loaded = LoadShader(*entry); break;
            case ResourceType::Sound:   loaded = LoadSound(*entry); break;
            default: break;
        }

        if (loaded) {
            entry->state = ResourceState::Loaded;
            std::lock_guard lock(m_mutex);
            m_gpuUploadQueue.push_back(req.id);
        } else {
            entry->state = ResourceState::Failed;
        }

        if (req.onComplete) req.onComplete(req.id, loaded);
    }
}

void ResourceManager::ProcessGPUUploads() {
    std::vector<ResourceId> uploads;
    {
        std::lock_guard lock(m_mutex);
        std::swap(uploads, m_gpuUploadQueue);
    }

    for (ResourceId id : uploads) {
        auto* entry = GetEntryMutable(id);
        if (!entry || entry->state != ResourceState::Loaded) continue;

        entry->state = ResourceState::Uploading;
        bool ok = false;
        switch (entry->type) {
            case ResourceType::Mesh:    ok = UploadMeshToGPU(*entry); break;
            case ResourceType::Texture: ok = UploadTextureToGPU(*entry); break;
            default: ok = true; break;
        }
        entry->state = ok ? ResourceState::Ready : ResourceState::Failed;
    }
}

void ResourceManager::EvictLRU() {
    std::lock_guard lock(m_mutex);

    // Collect eviction candidates: unreferenced resources sorted by last-used frame
    struct Candidate {
        ResourceId id;
        u64 lastUsed;
        u64 sizeBytes;
    };
    std::vector<Candidate> candidates;

    for (auto& [id, entry] : m_resources) {
        if (entry.refCount == 0 && entry.state == ResourceState::Ready) {
            candidates.push_back({id, entry.lastUsedFrame, entry.sizeBytes});
        }
    }

    std::sort(candidates.begin(), candidates.end(),
        [](const Candidate& a, const Candidate& b) { return a.lastUsed < b.lastUsed; });

    // Evict until under budget
    for (const auto& c : candidates) {
        if (m_totalMemoryUsage <= m_streamingBudget) break;

        auto it = m_resources.find(c.id);
        if (it == m_resources.end()) continue;

        auto& entry = it->second;
        if (entry.gpuBuffer.IsValid() && m_device) {
            m_device->DestroyBuffer(entry.gpuBuffer);
            entry.gpuBuffer = {};
        }
        if (entry.gpuTexture.IsValid() && m_device) {
            m_device->DestroyTexture(entry.gpuTexture);
            entry.gpuTexture = {};
        }
        if (entry.data) {
            entry.data = nullptr;
        }

        m_totalMemoryUsage -= entry.sizeBytes;
        m_gpuMemoryUsage -= entry.sizeBytes;
        entry.state = ResourceState::Unloaded;
        entry.sizeBytes = 0;

        NGE_LOG_DEBUG("Evicted resource '{}' (LRU frame {})", entry.path, c.lastUsed);
    }
}

void ResourceManager::CheckHotReload() {
    std::lock_guard lock(m_mutex);

    for (auto& [id, entry] : m_resources) {
        if (entry.state != ResourceState::Ready) continue;

        // Check file modification time
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(entry.path, ec);
        if (ec) continue;

        // TODO: Compare with stored modification time and reload if changed
    }
}

// ─── Type-Specific Loaders ───────────────────────────────────────────────

bool ResourceManager::LoadMesh(ResourceEntry& entry) {
    // TODO: Use MeshLoader to load the mesh
    NGE_LOG_DEBUG("Loading mesh: {}", entry.path);
    entry.sizeBytes = 0; // Will be set by actual loader
    m_totalMemoryUsage += entry.sizeBytes;
    return true; // Stub
}

bool ResourceManager::LoadTexture(ResourceEntry& entry) {
    // TODO: Use TextureLoader to load the texture
    NGE_LOG_DEBUG("Loading texture: {}", entry.path);
    entry.sizeBytes = 0;
    m_totalMemoryUsage += entry.sizeBytes;
    return true;
}

bool ResourceManager::LoadShader(ResourceEntry& entry) {
    // TODO: Use ShaderCompiler to compile/load the shader
    NGE_LOG_DEBUG("Loading shader: {}", entry.path);
    return true;
}

bool ResourceManager::LoadSound(ResourceEntry& entry) {
    // TODO: Use AudioSystem to load sound data
    NGE_LOG_DEBUG("Loading sound: {}", entry.path);
    return true;
}

bool ResourceManager::UploadMeshToGPU(ResourceEntry& entry) {
    if (!m_device) return false;
    // TODO: Create vertex/index buffers, upload via staging
    NGE_LOG_DEBUG("Uploading mesh to GPU: {}", entry.path);
    return true;
}

bool ResourceManager::UploadTextureToGPU(ResourceEntry& entry) {
    if (!m_device) return false;
    // TODO: Create texture, upload via staging buffer
    NGE_LOG_DEBUG("Uploading texture to GPU: {}", entry.path);
    return true;
}

u32 ResourceManager::GetLoadedResourceCount() const {
    std::lock_guard lock(m_mutex);
    u32 count = 0;
    for (const auto& [id, entry] : m_resources) {
        if (entry.state == ResourceState::Ready || entry.state == ResourceState::Loaded) count++;
    }
    return count;
}

u32 ResourceManager::GetPendingLoadCount() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_loadQueue.size() + m_gpuUploadQueue.size());
}

} // namespace nge::assets
