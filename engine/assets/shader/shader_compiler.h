#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_shader_hot_reload.h"
#include <string>
#include <vector>
#include <unordered_map>

namespace nge::assets {

// ─── Shader Compilation ──────────────────────────────────────────────────
// Compiles HLSL shaders to SPIR-V using DXC (DirectX Shader Compiler).
// DXC supports HLSL → SPIR-V via the -spirv flag.
//
// Pipeline:
//   1. Source HLSL files in shaders/ directory
//   2. DXC compiles to SPIR-V bytecode
//   3. SPIR-V is cached on disk (shaders/cache/*.spv)
//   4. At runtime, load .spv files and create shader modules

struct ShaderCompileOptions {
    std::string sourcePath;          // Path to .hlsl file
    std::string entryPoint = "main"; // Entry point function name
    rhi::ShaderStage stage;          // Shader stage
    std::string profile;             // e.g. "vs_6_7", "ps_6_7", "cs_6_7", "ms_6_7", "lib_6_7"
    std::vector<std::string> defines;  // Preprocessor defines
    std::vector<std::string> includeDirs; // Include search paths
    bool enableDebugInfo = false;
    bool optimizationLevel3 = true;  // -O3
    bool enable16bit = false;        // -enable-16bit-types
    bool enableMeshShaders = false;
};

struct ShaderCompileResult {
    bool success = false;
    std::vector<byte> spirvBytecode;
    std::string errorMessage;
    std::string warningMessage;

    // Reflection data (extracted from SPIR-V)
    struct BindingInfo {
        u32 set = 0;
        u32 binding = 0;
        std::string name;
        std::string type; // "uniform_buffer", "storage_buffer", "sampled_image", etc.
    };
    std::vector<BindingInfo> bindings;

    struct PushConstantRange {
        u32 offset = 0;
        u32 size = 0;
    };
    PushConstantRange pushConstants;
};

class ShaderCompiler {
public:
    // Initialize the shader compiler (find DXC path, etc.)
    static bool Init(const std::string& dxcPath = "");
    static void Shutdown();

    // Compile a single shader
    static ShaderCompileResult Compile(const ShaderCompileOptions& options);

    // Compile and cache. Returns cached .spv path.
    static std::string CompileAndCache(const ShaderCompileOptions& options,
                                        const std::string& cachePath);

    // Load pre-compiled SPIR-V from disk
    static bool LoadSPIRV(const std::string& spvPath, std::vector<byte>& outBytecode);

    // Check if source is newer than cached .spv (for hot reload)
    static bool NeedsRecompile(const std::string& sourcePath, const std::string& spvPath);

    // Get the shader profile string for a given stage
    static std::string GetProfile(rhi::ShaderStage stage);

private:
    inline static std::string s_dxcPath;
    inline static bool s_initialized = false;
};

// ─── Shader Library ──────────────────────────────────────────────────────
// Manages loaded shaders and handles hot-reload.

class ShaderLibrary {
public:
    ShaderLibrary() = default;

    void Init(rhi::IDevice* device, const std::string& shaderDir, const std::string& cacheDir);
    void Shutdown();

    // Load or get cached shader handle
    rhi::ShaderHandle GetShader(const std::string& name, rhi::ShaderStage stage);

    // Check for modified shaders and recompile
    void HotReload();

private:
    struct ShaderEntry {
        std::string sourcePath;
        std::string cachePath;
        rhi::ShaderStage stage;
        rhi::ShaderHandle handle;
        u32 watchId = 0;
        u64 lastModifiedTime = 0;
    };

    rhi::IDevice* m_device = nullptr;
    std::string   m_shaderDir;
    std::string   m_cacheDir;
    std::unordered_map<std::string, ShaderEntry> m_shaders;
    rhi::ShaderHotReloadManager m_hotReload;
    u32 m_nextWatchId = 1;
};

} // namespace nge::assets
