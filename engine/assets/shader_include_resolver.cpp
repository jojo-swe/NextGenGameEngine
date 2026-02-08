#include "engine/assets/shader_include_resolver.h"
#include "engine/core/logging/log.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <regex>

namespace nge::assets {

void ShaderIncludeResolver::Init() {
    m_includePaths.clear();
    m_fileCache.clear();
    m_dependencies.clear();
    m_reverseDeps.clear();
    m_lineMappings.clear();
    m_pragmaOnceFiles.clear();
}

void ShaderIncludeResolver::Shutdown() {
    InvalidateAll();
}

void ShaderIncludeResolver::AddIncludePath(const std::string& path) {
    m_includePaths.push_back(path);
}

ResolvedShader ShaderIncludeResolver::Resolve(const std::string& filePath) {
    std::string source = ReadFile(filePath);
    if (source.empty()) {
        ResolvedShader result;
        result.success = false;
        result.errorMessage = "Failed to read file: " + filePath;
        return result;
    }
    return ResolveFromSource(source, filePath);
}

ResolvedShader ShaderIncludeResolver::ResolveFromSource(const std::string& source,
                                                          const std::string& virtualPath) {
    std::unordered_set<std::string> visited;
    std::vector<std::string> dependencies;
    std::vector<SourceLineMapping> lineMap;
    u32 outputLine = 1;

    auto result = ResolveRecursive(source, virtualPath, visited, dependencies, lineMap, outputLine);

    if (result.success) {
        m_dependencies[virtualPath] = dependencies;

        // Build reverse dependency map
        for (const auto& dep : dependencies) {
            m_reverseDeps[dep].push_back(virtualPath);
        }

        m_lineMappings[virtualPath] = lineMap;
    }

    result.dependencies = std::move(dependencies);
    return result;
}

ResolvedShader ShaderIncludeResolver::ResolveRecursive(
    const std::string& source, const std::string& filePath,
    std::unordered_set<std::string>& visited,
    std::vector<std::string>& dependencies,
    std::vector<SourceLineMapping>& lineMap,
    u32& outputLine) {

    ResolvedShader result;
    result.success = true;

    // Cycle detection
    if (visited.count(filePath)) {
        // #pragma once — skip silently
        if (m_pragmaOnceFiles.count(filePath)) {
            return result;
        }
        result.success = false;
        result.errorMessage = "Circular include detected: " + filePath;
        return result;
    }
    visited.insert(filePath);

    std::string parentDir;
    auto lastSlash = filePath.find_last_of("/\\");
    if (lastSlash != std::string::npos) {
        parentDir = filePath.substr(0, lastSlash);
    }

    std::istringstream stream(source);
    std::string line;
    u32 sourceLine = 0;
    std::ostringstream output;

    // Regex for #include "path" and #include <path>
    std::regex includeRegex(R"(^\s*#\s*include\s+[\"<]([^\">\s]+)[\">])");
    std::regex pragmaOnceRegex(R"(^\s*#\s*pragma\s+once\s*$)");

    while (std::getline(stream, line)) {
        sourceLine++;
        std::smatch match;

        // Check for #pragma once
        if (std::regex_match(line, pragmaOnceRegex)) {
            m_pragmaOnceFiles.insert(filePath);
            continue;
        }

        // Check for #include
        if (std::regex_search(line, match, includeRegex)) {
            std::string includePath = match[1].str();
            std::string resolvedPath = FindInclude(includePath, parentDir);

            if (resolvedPath.empty()) {
                result.success = false;
                result.errorMessage = filePath + "(" + std::to_string(sourceLine) +
                                      "): Cannot find include '" + includePath + "'";
                return result;
            }

            // Track dependency
            dependencies.push_back(resolvedPath);

            // Read and recursively resolve the include
            std::string includeSource = ReadFile(resolvedPath);
            if (includeSource.empty()) {
                result.success = false;
                result.errorMessage = "Failed to read include: " + resolvedPath;
                return result;
            }

            auto subResult = ResolveRecursive(includeSource, resolvedPath, visited,
                                                dependencies, lineMap, outputLine);
            if (!subResult.success) {
                result.success = false;
                result.errorMessage = subResult.errorMessage;
                return result;
            }

            output << subResult.flattenedSource;
        } else {
            // Regular line — emit with line mapping
            SourceLineMapping mapping;
            mapping.outputLine = outputLine;
            mapping.originalFile = filePath;
            mapping.originalLine = sourceLine;
            lineMap.push_back(mapping);

            output << line << "\n";
            outputLine++;
        }
    }

    visited.erase(filePath);
    result.flattenedSource = output.str();
    return result;
}

bool ShaderIncludeResolver::HasDependencyChanged(const std::string& filePath) const {
    auto it = m_dependencies.find(filePath);
    if (it == m_dependencies.end()) return true; // Unknown file, assume changed

    for (const auto& dep : it->second) {
        auto cacheIt = m_fileCache.find(dep);
        if (cacheIt == m_fileCache.end()) return true;

        // Check modification time
        try {
            auto modTime = std::filesystem::last_write_time(dep).time_since_epoch().count();
            if (static_cast<u64>(modTime) != cacheIt->second.lastModified) return true;
        } catch (...) {
            return true;
        }
    }
    return false;
}

std::vector<std::string> ShaderIncludeResolver::GetDependents(const std::string& includeFile) const {
    auto it = m_reverseDeps.find(includeFile);
    if (it != m_reverseDeps.end()) return it->second;
    return {};
}

void ShaderIncludeResolver::Invalidate(const std::string& filePath) {
    m_fileCache.erase(filePath);
    m_dependencies.erase(filePath);
    m_lineMappings.erase(filePath);
    m_pragmaOnceFiles.erase(filePath);
}

void ShaderIncludeResolver::InvalidateAll() {
    m_fileCache.clear();
    m_dependencies.clear();
    m_reverseDeps.clear();
    m_lineMappings.clear();
    m_pragmaOnceFiles.clear();
}

std::vector<SourceLineMapping> ShaderIncludeResolver::GetLineMapping(const std::string& filePath) const {
    auto it = m_lineMappings.find(filePath);
    if (it != m_lineMappings.end()) return it->second;
    return {};
}

std::string ShaderIncludeResolver::ReadFile(const std::string& path) const {
    // Check cache first
    auto it = m_fileCache.find(path);
    if (it != m_fileCache.end()) return it->second.source;

    std::ifstream file(path);
    if (!file.is_open()) return "";

    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();

    // Cache the file
    ShaderSourceFile entry;
    entry.path = path;
    entry.source = content;
    try {
        entry.lastModified = static_cast<u64>(
            std::filesystem::last_write_time(path).time_since_epoch().count());
    } catch (...) {
        entry.lastModified = 0;
    }
    m_fileCache[path] = std::move(entry);

    return content;
}

std::string ShaderIncludeResolver::FindInclude(const std::string& includePath,
                                                  const std::string& parentDir) const {
    namespace fs = std::filesystem;

    // Try relative to parent directory first
    if (!parentDir.empty()) {
        fs::path candidate = fs::path(parentDir) / includePath;
        if (fs::exists(candidate)) return candidate.string();
    }

    // Try each include search path
    for (const auto& searchPath : m_includePaths) {
        fs::path candidate = fs::path(searchPath) / includePath;
        if (fs::exists(candidate)) return candidate.string();
    }

    // Try as absolute path
    if (fs::exists(includePath)) return includePath;

    return "";
}

} // namespace nge::assets
