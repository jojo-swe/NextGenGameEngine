#include "engine/rhi/vulkan/vk_shader_object.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <algorithm>

namespace nge::rhi::vulkan {

bool ShaderObjectManager::Init(IDevice* device, const ShaderObjectConfig& config) {
    m_device = device;
    m_config = config;
    m_stats = {};
    m_nextHandle = 1;

    // TODO: Check VK_EXT_shader_object support
    // VkPhysicalDeviceShaderObjectFeaturesEXT features{};
    // features.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_OBJECT_FEATURES_EXT;
    // if (!features.shaderObject) { LOG_WARN("VK_EXT_shader_object not supported"); }

    if (config.enableBinaryCache) {
        LoadCacheFromDisk();
    }

    NGE_LOG_INFO("Shader object manager initialized: cache={}, maxObjects={}",
                 config.enableBinaryCache, config.maxCachedObjects);
    return true;
}

void ShaderObjectManager::Shutdown() {
    if (m_config.enableBinaryCache) {
        SaveCacheToDisk();
    }

    for (auto& [handle, obj] : m_objects) {
        if (obj.alive) {
            // TODO: vkDestroyShaderEXT(device, obj.handle, nullptr);
            obj.alive = false;
        }
    }
    m_objects.clear();
    m_hashToHandle.clear();
    m_nameToHandle.clear();
}

u64 ShaderObjectManager::CreateShaderObject(const ShaderObjectDesc& desc) {
    std::lock_guard lock(m_mutex);

    u64 spirvHash = HashSPIRV(desc.spirvCode);

    // Check if already compiled
    auto it = m_hashToHandle.find(spirvHash);
    if (it != m_hashToHandle.end()) {
        m_stats.cacheHits++;
        return it->second;
    }

    // TODO: Create VkShaderEXT
    // VkShaderCreateInfoEXT createInfo{};
    // createInfo.sType = VK_STRUCTURE_TYPE_SHADER_CREATE_INFO_EXT;
    // createInfo.stage = ToVkShaderStage(desc.stage);
    // createInfo.nextStage = 0; // depends on pipeline combination
    // createInfo.codeType = VK_SHADER_CODE_TYPE_SPIRV_EXT;
    // createInfo.codeSize = desc.spirvCode.size();
    // createInfo.pCode = desc.spirvCode.data();
    // createInfo.pName = desc.entryPoint.c_str();
    // createInfo.setLayoutCount = static_cast<u32>(desc.descriptorSetLayouts.size());
    // createInfo.pSetLayouts = reinterpret_cast<const VkDescriptorSetLayout*>(desc.descriptorSetLayouts.data());
    // VkShaderEXT shaderEXT;
    // vkCreateShadersEXT(device, 1, &createInfo, nullptr, &shaderEXT);

    u64 handle = m_nextHandle++;
    ShaderObject obj;
    obj.handle = handle;
    obj.stage = desc.stage;
    obj.debugName = desc.debugName;
    obj.spirvHash = spirvHash;
    obj.alive = true;

    m_objects[handle] = std::move(obj);
    m_hashToHandle[spirvHash] = handle;
    if (!desc.debugName.empty()) {
        m_nameToHandle[desc.debugName] = handle;
    }

    // Update stats
    m_stats.compilations++;
    m_stats.totalObjects++;
    switch (desc.stage) {
        case ShaderObjectStage::Vertex:   m_stats.vertexShaders++;   break;
        case ShaderObjectStage::Fragment: m_stats.fragmentShaders++; break;
        case ShaderObjectStage::Compute:  m_stats.computeShaders++;  break;
        case ShaderObjectStage::Mesh:     m_stats.meshShaders++;     break;
        default: break;
    }

    NGE_LOG_INFO("Shader object '{}' created: stage={}, hash={:#x}",
                 desc.debugName, static_cast<u32>(desc.stage), spirvHash);
    return handle;
}

u64 ShaderObjectManager::CreateFromBinary(const ShaderObjectBinary& binary, ShaderObjectStage stage) {
    std::lock_guard lock(m_mutex);

    auto it = m_hashToHandle.find(binary.hash);
    if (it != m_hashToHandle.end()) {
        m_stats.cacheHits++;
        return it->second;
    }

    // TODO: Create from binary
    // VkShaderCreateInfoEXT createInfo{};
    // createInfo.codeType = VK_SHADER_CODE_TYPE_BINARY_EXT;
    // createInfo.codeSize = binary.binaryData.size();
    // createInfo.pCode = binary.binaryData.data();

    u64 handle = m_nextHandle++;
    ShaderObject obj;
    obj.handle = handle;
    obj.stage = stage;
    obj.spirvHash = binary.hash;
    obj.binaryCache = binary.binaryData;
    obj.alive = true;

    m_objects[handle] = std::move(obj);
    m_hashToHandle[binary.hash] = handle;

    m_stats.totalObjects++;
    m_stats.totalBinaryBytes += binary.binaryData.size();

    return handle;
}

void ShaderObjectManager::Destroy(u64 shaderObject) {
    std::lock_guard lock(m_mutex);
    auto it = m_objects.find(shaderObject);
    if (it == m_objects.end()) return;

    // TODO: vkDestroyShaderEXT(device, it->second.handle, nullptr);
    m_hashToHandle.erase(it->second.spirvHash);
    if (!it->second.debugName.empty()) {
        m_nameToHandle.erase(it->second.debugName);
    }
    m_objects.erase(it);
    m_stats.totalObjects--;
}

void ShaderObjectManager::BindVertexFragment(ICommandList* cmd, u64 vertexShader, u64 fragmentShader) {
    std::lock_guard lock(m_mutex);
    // TODO: VkShaderStageFlagBits stages[] = { VK_SHADER_STAGE_VERTEX_BIT, VK_SHADER_STAGE_FRAGMENT_BIT };
    // VkShaderEXT shaders[] = { m_objects[vertexShader].handle, m_objects[fragmentShader].handle };
    // vkCmdBindShadersEXT(cmd, 2, stages, shaders);
    (void)cmd; (void)vertexShader; (void)fragmentShader;
}

void ShaderObjectManager::BindCompute(ICommandList* cmd, u64 computeShader) {
    std::lock_guard lock(m_mutex);
    // TODO: VkShaderStageFlagBits stages[] = { VK_SHADER_STAGE_COMPUTE_BIT };
    // VkShaderEXT shaders[] = { m_objects[computeShader].handle };
    // vkCmdBindShadersEXT(cmd, 1, stages, shaders);
    (void)cmd; (void)computeShader;
}

void ShaderObjectManager::BindMeshTask(ICommandList* cmd, u64 meshShader, u64 taskShader) {
    std::lock_guard lock(m_mutex);
    // TODO: Bind mesh + optional task shader
    // VkShaderStageFlagBits stages[2];
    // VkShaderEXT shaders[2];
    // u32 count = 1;
    // stages[0] = VK_SHADER_STAGE_MESH_BIT_EXT;
    // shaders[0] = m_objects[meshShader].handle;
    // if (taskShader) { stages[1] = VK_SHADER_STAGE_TASK_BIT_EXT; shaders[1] = ...; count = 2; }
    // vkCmdBindShadersEXT(cmd, count, stages, shaders);
    (void)cmd; (void)meshShader; (void)taskShader;
}

ShaderObjectBinary ShaderObjectManager::GetBinary(u64 shaderObject) const {
    std::lock_guard lock(m_mutex);
    ShaderObjectBinary binary{};
    auto it = m_objects.find(shaderObject);
    if (it == m_objects.end()) return binary;

    // TODO: vkGetShaderBinaryDataEXT(device, it->second.handle, &size, nullptr);
    // binary.binaryData.resize(size);
    // vkGetShaderBinaryDataEXT(device, it->second.handle, &size, binary.binaryData.data());
    binary.hash = it->second.spirvHash;
    binary.binaryData = it->second.binaryCache;
    return binary;
}

bool ShaderObjectManager::SaveCacheToDisk() const {
    std::lock_guard lock(m_mutex);

    for (const auto& [handle, obj] : m_objects) {
        if (!obj.alive || obj.binaryCache.empty()) continue;

        std::string path = m_config.cachePath + std::to_string(obj.spirvHash) + ".sobj";
        std::ofstream file(path, std::ios::binary);
        if (file.is_open()) {
            file.write(reinterpret_cast<const char*>(obj.binaryCache.data()), obj.binaryCache.size());
        }
    }

    NGE_LOG_INFO("Shader object cache saved: {} objects", m_objects.size());
    return true;
}

bool ShaderObjectManager::LoadCacheFromDisk() {
    // TODO: Scan cache directory and load binaries
    // for each .sobj file, parse hash from filename, load binary
    NGE_LOG_INFO("Shader object cache load attempted from: {}", m_config.cachePath);
    return true;
}

void ShaderObjectManager::Invalidate(u64 shaderObject) {
    std::lock_guard lock(m_mutex);
    auto it = m_objects.find(shaderObject);
    if (it == m_objects.end()) return;

    // Mark for recompilation — clear binary cache
    it->second.binaryCache.clear();
    NGE_LOG_INFO("Shader object '{}' invalidated for hot-reload", it->second.debugName);
}

u64 ShaderObjectManager::FindByName(const std::string& name) const {
    std::lock_guard lock(m_mutex);
    auto it = m_nameToHandle.find(name);
    return it != m_nameToHandle.end() ? it->second : 0;
}

ShaderObjectStats ShaderObjectManager::GetStats() const {
    std::lock_guard lock(m_mutex);
    return m_stats;
}

u64 ShaderObjectManager::HashSPIRV(const std::vector<u8>& spirv) const {
    u64 hash = 14695981039346656037ULL;
    for (u8 byte : spirv) {
        hash ^= static_cast<u64>(byte);
        hash *= 1099511628211ULL;
    }
    return hash;
}

} // namespace nge::rhi::vulkan
