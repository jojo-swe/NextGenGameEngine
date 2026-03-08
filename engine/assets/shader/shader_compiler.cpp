#include "engine/assets/shader/shader_compiler.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <fstream>
#include <filesystem>
#include <cstdlib>
#include <array>
#include <sstream>

#if defined(NGE_PLATFORM_WINDOWS)
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace {

nge::u64 HashBytecode(const std::vector<nge::byte>& data) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(data.data());
    nge::u64 hash = 1469598103934665603ull;
    for (nge::usize i = 0; i < data.size(); ++i) {
        hash ^= static_cast<nge::u64>(bytes[i]);
        hash *= 1099511628211ull;
    }
    return hash;
}

std::vector<std::string> CollectDirectIncludePaths(
    const std::string& sourcePath,
    const std::vector<std::string>& includeDirs)
{
    std::ifstream file(sourcePath);
    if (!file.is_open()) {
        return {};
    }

    const fs::path sourceDir = fs::path(sourcePath).parent_path();
    std::vector<std::string> includePaths;
    std::string line;
    while (std::getline(file, line)) {
        const auto includePos = line.find("#include");
        if (includePos == std::string::npos) {
            continue;
        }

        const auto firstQuote = line.find('"', includePos);
        if (firstQuote == std::string::npos) {
            continue;
        }

        const auto secondQuote = line.find('"', firstQuote + 1);
        if (secondQuote == std::string::npos) {
            continue;
        }

        const std::string includeName = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);

        fs::path resolvedPath = sourceDir / includeName;
        if (!fs::exists(resolvedPath)) {
            for (const auto& includeDir : includeDirs) {
                const fs::path candidate = fs::path(includeDir) / includeName;
                if (fs::exists(candidate)) {
                    resolvedPath = candidate;
                    break;
                }
            }
        }

        if (!fs::exists(resolvedPath)) {
            continue;
        }

        const std::string resolved = resolvedPath.lexically_normal().string();
        if (std::find(includePaths.begin(), includePaths.end(), resolved) == includePaths.end()) {
            includePaths.push_back(resolved);
        }
    }

    return includePaths;
}

}

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

    std::string cmdStr = cmd.str();
    NGE_LOG_DEBUG("Compiling shader: {}", cmdStr);

    std::string output;

#if defined(NGE_PLATFORM_WINDOWS)
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    sa.bInheritHandle = TRUE;

    HANDLE readPipe = nullptr;
    HANDLE writePipe = nullptr;
    if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
        result.errorMessage = "Failed to create DXC output pipe";
        return result;
    }

    SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOA startupInfo{};
    startupInfo.cb = sizeof(startupInfo);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startupInfo.hStdOutput = writePipe;
    startupInfo.hStdError = writePipe;

    PROCESS_INFORMATION processInfo{};
    std::vector<char> mutableCmd(cmdStr.begin(), cmdStr.end());
    mutableCmd.push_back('\0');

    const BOOL created = CreateProcessA(
        nullptr,
        mutableCmd.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    CloseHandle(writePipe);

    if (!created) {
        CloseHandle(readPipe);
        result.errorMessage = "Failed to execute DXC";
        return result;
    }

    std::array<char, 4096> buffer{};
    DWORD bytesRead = 0;
    while (ReadFile(readPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr) && bytesRead > 0) {
        output.append(buffer.data(), bytesRead);
    }

    CloseHandle(readPipe);

    WaitForSingleObject(processInfo.hProcess, INFINITE);
    DWORD exitCode = 0;
    GetExitCodeProcess(processInfo.hProcess, &exitCode);
    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
#else
    cmd << " 2>&1";
    cmdStr = cmd.str();
    FILE* pipe = popen(cmdStr.c_str(), "r");
    if (!pipe) {
        result.errorMessage = "Failed to execute DXC";
        return result;
    }

    std::array<char, 4096> buffer{};
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
        output += buffer.data();
    }

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
    m_nextWatchId = 1;

    ShaderCompiler::Init();
    fs::create_directories(cacheDir);
    m_hotReload.Init();

    NGE_LOG_INFO("Shader library initialized: src='{}', cache='{}'", shaderDir, cacheDir);
}

void ShaderLibrary::Shutdown() {
    for (auto& [name, entry] : m_shaders) {
        if (entry.handle.IsValid()) {
            m_device->DestroyShader(entry.handle);
        }
    }
    m_shaders.clear();
    m_hotReload.Shutdown();
    m_nextWatchId = 1;
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
    const auto includePaths = CollectDirectIncludePaths(sourcePath, options.includeDirs);

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
    entry.watchId    = m_nextWatchId++;
    m_hotReload.WatchShader(entry.watchId, entry.sourcePath, entry.stage, includePaths);
    m_shaders[key]   = entry;

    return handle;
}

void ShaderLibrary::HotReload() {
    if (m_hotReload.PollChanges() == 0) {
        return;
    }

    const auto pending = m_hotReload.GetPendingReloads();
    for (u32 watchId : pending) {
        ShaderEntry* entry = nullptr;
        for (auto& [key, candidate] : m_shaders) {
            if (candidate.watchId == watchId) {
                entry = &candidate;
                break;
            }
        }
        if (!entry) {
            continue;
        }

        NGE_LOG_INFO("Hot-reloading shader: {}", entry->sourcePath);

        ShaderCompileOptions options;
        options.sourcePath = entry->sourcePath;
        options.stage = entry->stage;
        options.includeDirs.push_back(m_shaderDir);
        options.includeDirs.push_back(m_shaderDir + "/common");
        const auto includePaths = CollectDirectIncludePaths(entry->sourcePath, options.includeDirs);

        std::string spvPath = ShaderCompiler::CompileAndCache(options, m_cacheDir);
        if (spvPath.empty()) {
            m_hotReload.MarkFailed(watchId);
            continue;
        }

        std::vector<byte> bytecode;
        if (!ShaderCompiler::LoadSPIRV(spvPath, bytecode)) {
            m_hotReload.MarkFailed(watchId);
            continue;
        }

        if (entry->handle.IsValid()) {
            m_device->DestroyShader(entry->handle);
        }

        rhi::ShaderDesc desc;
        desc.bytecode     = bytecode.data();
        desc.bytecodeSize = bytecode.size();
        desc.stage        = entry->stage;
        desc.debugName    = entry->sourcePath.c_str();

        entry->handle    = m_device->CreateShader(desc);
        entry->cachePath = spvPath;
        m_hotReload.WatchShader(entry->watchId, entry->sourcePath, entry->stage, includePaths);
        m_hotReload.MarkReloaded(watchId, HashBytecode(bytecode));
    }
}

} // namespace nge::assets
