#pragma once

#include "engine/core/types.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

namespace nge::assets {

// ─── Shader Include Resolver ─────────────────────────────────────────────
// Resolves #include directives in HLSL shaders, producing a flattened
// source string with all includes inlined. Tracks dependencies for
// hot-reload invalidation.
//
// Features:
//   - Recursive include resolution with cycle detection
//   - Multiple include search paths
//   - Dependency graph for change propagation
//   - #pragma once support
//   - Source line mapping for error messages

struct ShaderSourceFile {
    std::string path;
    std::string source;
    u64         lastModified = 0;
};

struct ResolvedShader {
    std::string flattenedSource;
    std::vector<std::string> dependencies; // All included files (transitive)
    bool success = false;
    std::string errorMessage;
};

struct SourceLineMapping {
    u32         outputLine;
    std::string originalFile;
    u32         originalLine;
};

class ShaderIncludeResolver {
public:
    void Init();
    void Shutdown();

    // Add an include search path (searched in order)
    void AddIncludePath(const std::string& path);

    // Resolve all includes in a shader file
    ResolvedShader Resolve(const std::string& filePath);

    // Resolve from source string (with a virtual file path for error messages)
    ResolvedShader ResolveFromSource(const std::string& source, const std::string& virtualPath);

    // Check if any dependency of a shader has been modified
    bool HasDependencyChanged(const std::string& filePath) const;

    // Get all files that depend on the given include file (reverse lookup)
    std::vector<std::string> GetDependents(const std::string& includeFile) const;

    // Invalidate cached source for a file (on hot-reload)
    void Invalidate(const std::string& filePath);
    void InvalidateAll();

    // Get source line mapping for error message translation
    std::vector<SourceLineMapping> GetLineMapping(const std::string& filePath) const;

private:
    std::string ReadFile(const std::string& path) const;
    std::string FindInclude(const std::string& includePath, const std::string& parentDir) const;
    ResolvedShader ResolveRecursive(const std::string& source, const std::string& filePath,
                                      std::unordered_set<std::string>& visited,
                                      std::vector<std::string>& dependencies,
                                      std::vector<SourceLineMapping>& lineMap,
                                      u32& outputLine);

    std::vector<std::string> m_includePaths;

    // Cache: file path → source content
    mutable std::unordered_map<std::string, ShaderSourceFile> m_fileCache;

    // Dependency graph: file → files it includes
    std::unordered_map<std::string, std::vector<std::string>> m_dependencies;

    // Reverse dependency: include file → files that include it
    std::unordered_map<std::string, std::vector<std::string>> m_reverseDeps;

    // Line mappings per resolved shader
    std::unordered_map<std::string, std::vector<SourceLineMapping>> m_lineMappings;

    // Pragma once tracking
    std::unordered_set<std::string> m_pragmaOnceFiles;
};

} // namespace nge::assets
