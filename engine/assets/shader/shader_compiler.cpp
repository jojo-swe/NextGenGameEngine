#include "engine/assets/shader/shader_compiler.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <array>
#include <sstream>

namespace fs = std::filesystem;

namespace nge::assets {

bool ShaderCompiler::Init(const std::string& dxcPath) {
    if (s_initialized) return true;

    if (!dxcPath.empty()) {
        s_dxcPath = dxcPath;
    } else {
        // Try to find DXC in common locations
        std::vector<std::string> candidates = {
            "dxc",
            "dxc.exe",
        };

        // Check Vulkan SDK
        const char* vulkanSdk = std::getenv("VULKAN_SDK");
        if (vulkanSdk) {
            candidates.push_back(std::string(vulkanSdk) + "/Bin/dxc.exe");
            candidates.push_back(std::string(vulkanSdk) + "/bin/dxc");
        }

        for (const auto& candidate : candidates) {
            if (fs::exists(candidate)) {
                s_dxcPath = candidate;
                break;
            }
        }

        // Fallback: assume dxc is in PATH
        if (s_dxcPath.empty()) {
            s_dxcPath = "dxc";
        }
    }

    s_initialized = true;
    NGE_LOG_INFO("Shader compiler initialized: DXC at '{}'", s_dxcPath);
    return true;
}

void ShaderCompiler::Shutdown() {
    s_initialized = false;
}

std::string ShaderCompiler::GetProfile(rhi::ShaderStage stage) {
    switch (stage) {
        case rhi::ShaderStage::Vertex:        return "vs_6_7";
        case rhi::ShaderStage::Fragment:       return "ps_6_7";
        case rhi::ShaderStage::Compute:        return "cs_6_7";
        case rhi::ShaderStage::Geometry:       return "gs_6_7";
        case rhi::ShaderStage::TessControl:    return "hs_6_7";
        case rhi::ShaderStage::TessEval:       return "ds_6_7";
        case rhi::ShaderStage::Mesh:           return "ms_6_7";
        case rhi::ShaderStage::Amplification:  return "as_6_7";
        case rhi::ShaderStage::RayGeneration:
        case rhi::ShaderStage::RayMiss:
        case rhi::ShaderStage::RayClosestHit:
        case rhi::ShaderStage::RayAnyHit:
        case rhi::ShaderStage::RayIntersection:
        case rhi::ShaderStage::Callable:       return "lib_6_7";
        default: return "vs_6_7";
    }
}

ShaderCompileResult ShaderCompiler::Compile(const ShaderCompileOptions& options) {
    ShaderCompileResult result;

    if (!s_initialized) {
        result.errorMessage = "Shader compiler not initialized";
        return result;
    }

    if (!fs::exists(options.sourcePath)) {
        result.errorMessage = "Source file not found: " + options.sourcePath;
        return result;
    }

    // Build DXC command line
    // dxc -spirv -T <profile> -E <entry> [-D<define>]... [-I<include>]... -Fo <output> <input>
    std::string profile = options.profile.empty() ? GetProfile(options.stage) : options.profile;

    std::string tempOutput = options.sourcePath + ".spv";

    std::ostringstream cmd;
    cmd << "\"" << s_dxcPath << "\"";
    cmd << " -spirv";
    cmd << " -fspv-target-env=vulkan1.3";
    cmd << " -T " << profile;
    cmd << " -E " << options.entryPoint;

    if (options.optimizationLevel3) {
        cmd << " -O3";
    }

    if (options.enableDebugInfo) {
        cmd << " -Zi";
    }

    if (options.enable16bit) {
        cmd << " -enable-16bit-types";
    }

    for (const auto& define : options.defines) {
        cmd << " -D" << define;
    }

    for (const auto& inc : options.includeDirs) {
        cmd << " -I \"" << inc << "\"";
    }

    cmd << " -Fo \"" << tempOutput << "\"";
    cmd << " \"" << options.sourcePath << "\"";
    cmd << " 2>&1";

    std::string cmdStr = cmd.str();
    NGE_LOG_DEBUG("Compiling shader: {}", cmdStr);

    // Execute DXC
    std::array<char, 4096> buffer;
    std::string output;

#if defined(NGE_PLATFORM_WINDOWS)
    FILE* pipe = _popen(cmdStr.c_str(), "r");
#else
    FILE* pipe = popen(cmdStr.c_str(), "r");
#endif

    if (!pipe) {
        result.errorMessage = "Failed to execute DXC";
        return result;
    }

    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

#if defined(NGE_PLATFORM_WINDOWS)
    int exitCode = _pclose(pipe);
#else
    int exitCode = pclose(pipe);
#endif

    if (exitCode != 0) {
        result.errorMessage = output;
        NGE_LOG_ERROR("Shader compilation failed: {}\n{}", options.sourcePath, output);
        return result;
    }

    if (!output.empty()) {
        result.warningMessage = output;
    }

    // Read compiled SPIR-V
    if (!LoadSPIRV(tempOutput, result.spirvBytecode)) {
        result.errorMessage = "Failed to read compiled SPIR-V";
        return result;
    }

    // Clean up temp file if it was a temp
    // (we keep it if it matches the desired cache path)

    result.success = true;
    NGE_LOG_INFO("Compiled shader: {} ({} bytes SPIR-V)", options.sourcePath, result.spirvBytecode.size());
    return result;
}

std::string ShaderCompiler::CompileAndCache(const ShaderCompileOptions& options,
                                             const std::string& cachePath)
{
    // Derive output path
    std::string filename = fs::path(options.sourcePath).stem().string();
    std::string stageSuffix;
    switch (options.stage) {
        case rhi::ShaderStage::Vertex:       stageSuffix = ".vert"; break;
        case rhi::ShaderStage::Fragment:      stageSuffix = ".frag"; break;
        case rhi::ShaderStage::Compute:       stageSuffix = ".comp"; break;
        case rhi::ShaderStage::Mesh:          stageSuffix = ".mesh"; break;
        case rhi::ShaderStage::Amplification: stageSuffix = ".task"; break;
        default:                              stageSuffix = ".shader"; break;
    }

    std::string spvPath = cachePath + "/" + filename + stageSuffix + ".spv";

    // Check if recompilation is needed
    if (!NeedsRecompile(options.sourcePath, spvPath)) {
        NGE_LOG_DEBUG("Shader cache hit: {}", spvPath);
        return spvPath;
    }

    // Ensure cache directory exists
    fs::create_directories(cachePath);

    // Compile
    auto result = Compile(options);
    if (!result.success) return "";

    // Write to cache
    std::ofstream out(spvPath, std::ios::binary);
    if (!out.is_open()) {
        NGE_LOG_ERROR("Failed to write shader cache: {}", spvPath);
        return "";
    }
    out.write(reinterpret_cast<const char*>(result.spirvBytecode.data()),
              static_cast<std::streamsize>(result.spirvBytecode.size()));
    out.close();

    return spvPath;
}

bool ShaderCompiler::LoadSPIRV(const std::string& spvPath, std::vector<byte>& outBytecode) {
    std::ifstream file(spvPath, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return false;

    auto size = file.tellg();
    file.seekg(0, std::ios::beg);

    outBytecode.resize(static_cast<usize>(size));
    file.read(reinterpret_cast<char*>(outBytecode.data()), size);
    return file.good();
}

bool ShaderCompiler::NeedsRecompile(const std::string& sourcePath, const std::string& spvPath) {
    if (!fs::exists(spvPath)) return true;
    if (!fs::exists(sourcePath)) return false;

    auto srcTime = fs::last_write_time(sourcePath);
    auto spvTime = fs::last_write_time(spvPath);
    return srcTime > spvTime;
}

// ─── ShaderLibrary ───────────────────────────────────────────────────────

void ShaderLibrary::Init(rhi::IDevice* device, const std::string& shaderDir, const std::string& cacheDir) {
    m_device    = device;
    m_shaderDir = shaderDir;
    m_cacheDir  = cacheDir;

    ShaderCompiler::Init();
    fs::create_directories(cacheDir);

    NGE_LOG_INFO("Shader library initialized: src='{}', cache='{}'", shaderDir, cacheDir);
}

void ShaderLibrary::Shutdown() {
    for (auto& [name, entry] : m_shaders) {
        if (entry.handle.IsValid()) {
            m_device->DestroyShader(entry.handle);
        }
    }
    m_shaders.clear();
    ShaderCompiler::Shutdown();
}

rhi::ShaderHandle ShaderLibrary::GetShader(const std::string& name, rhi::ShaderStage stage) {
    std::string key = name + "_" + std::to_string(static_cast<u32>(stage));

    auto it = m_shaders.find(key);
    if (it != m_shaders.end() && it->second.handle.IsValid()) {
        return it->second.handle;
    }

    // Find source file
    std::string sourcePath = m_shaderDir + "/" + name;
    if (!fs::exists(sourcePath)) {
        NGE_LOG_ERROR("Shader source not found: {}", sourcePath);
        return rhi::ShaderHandle{};
    }

    // Compile and cache
    ShaderCompileOptions options;
    options.sourcePath = sourcePath;
    options.stage = stage;
    options.includeDirs.push_back(m_shaderDir);
    options.includeDirs.push_back(m_shaderDir + "/common");

    std::string spvPath = ShaderCompiler::CompileAndCache(options, m_cacheDir);
    if (spvPath.empty()) return rhi::ShaderHandle{};

    // Load SPIR-V and create shader module
    std::vector<byte> bytecode;
    if (!ShaderCompiler::LoadSPIRV(spvPath, bytecode)) {
        NGE_LOG_ERROR("Failed to load SPIR-V: {}", spvPath);
        return rhi::ShaderHandle{};
    }

    rhi::ShaderDesc desc;
    desc.bytecode     = bytecode.data();
    desc.bytecodeSize = bytecode.size();
    desc.stage        = stage;
    desc.debugName    = name.c_str();

    rhi::ShaderHandle handle = m_device->CreateShader(desc);

    ShaderEntry entry;
    entry.sourcePath = sourcePath;
    entry.cachePath  = spvPath;
    entry.stage      = stage;
    entry.handle     = handle;
    m_shaders[key]   = entry;

    return handle;
}

void ShaderLibrary::HotReload() {
    for (auto& [key, entry] : m_shaders) {
        if (!ShaderCompiler::NeedsRecompile(entry.sourcePath, entry.cachePath)) continue;

        NGE_LOG_INFO("Hot-reloading shader: {}", entry.sourcePath);

        ShaderCompileOptions options;
        options.sourcePath = entry.sourcePath;
        options.stage = entry.stage;
        options.includeDirs.push_back(m_shaderDir);
        options.includeDirs.push_back(m_shaderDir + "/common");

        std::string spvPath = ShaderCompiler::CompileAndCache(options, m_cacheDir);
        if (spvPath.empty()) continue;

        std::vector<byte> bytecode;
        if (!ShaderCompiler::LoadSPIRV(spvPath, bytecode)) continue;

        // Destroy old shader
        if (entry.handle.IsValid()) {
            m_device->DestroyShader(entry.handle);
        }

        rhi::ShaderDesc desc;
        desc.bytecode     = bytecode.data();
        desc.bytecodeSize = bytecode.size();
        desc.stage        = entry.stage;
        desc.debugName    = entry.sourcePath.c_str();

        entry.handle    = m_device->CreateShader(desc);
        entry.cachePath = spvPath;
    }
}

} // namespace nge::assets
