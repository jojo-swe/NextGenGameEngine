#pragma once

#include "engine/core/types.h"
#include "engine/core/ecs/entity.h"
#include "engine/core/math/math_types.h"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <unordered_map>

namespace nge::scripting {

// ─── Script Handle ───────────────────────────────────────────────────────

using ScriptId = u32;
inline constexpr ScriptId INVALID_SCRIPT = UINT32_MAX;

// ─── Script Component ────────────────────────────────────────────────────
// Attached to entities that have scripted behavior.

struct ScriptComponent {
    ScriptId    scriptId = INVALID_SCRIPT;
    std::string scriptPath;
    bool        enabled = true;
};

// ─── Native Function Binding ─────────────────────────────────────────────
// Allows C++ functions to be called from scripts.

struct NativeBinding {
    std::string name;
    std::string signature; // For documentation
    std::function<void()> func; // Type-erased; actual binding via sol2
};

// ─── Script Engine Configuration ─────────────────────────────────────────

struct ScriptEngineConfig {
    std::string scriptsDirectory = "scripts/";
    bool        enableHotReload = true;
    f32         hotReloadCheckInterval = 1.0f; // seconds
    u32         maxScripts = 1024;
    bool        enableDebugger = false;
    u16         debuggerPort = 8172;
};

// ─── Script Engine ───────────────────────────────────────────────────────
// Wraps Lua + Sol2 for game scripting.
// Provides:
//   - Script loading and compilation
//   - Hot-reload on file change
//   - Entity lifecycle callbacks (OnCreate, OnUpdate, OnDestroy)
//   - Native C++ function bindings
//   - Coroutine support for async game logic

class ScriptEngine {
public:
    ScriptEngine() = default;
    ~ScriptEngine();

    bool Init(const ScriptEngineConfig& config = {});
    void Shutdown();

    // Script loading
    ScriptId LoadScript(const std::string& path);
    void UnloadScript(ScriptId id);
    bool ReloadScript(ScriptId id);

    // Hot-reload check (call periodically)
    void CheckHotReload();

    // Entity lifecycle — called by ECS systems
    void OnEntityCreate(ecs::Entity entity, ScriptId scriptId);
    void OnEntityUpdate(ecs::Entity entity, ScriptId scriptId, f32 deltaTime);
    void OnEntityDestroy(ecs::Entity entity, ScriptId scriptId);

    // Custom event dispatch
    void FireEvent(const std::string& eventName, ecs::Entity entity);

    // Native bindings — register C++ functions callable from Lua
    void RegisterFunction(const std::string& name, std::function<void()> func);
    void RegisterModule(const std::string& moduleName, std::function<void()> registrar);

    // Expose engine types to Lua
    void BindEngineTypes();

    // Execute arbitrary Lua code (for console/debug)
    bool Execute(const std::string& code, std::string& outError);

    // Global Lua state access (for advanced use)
    void* GetLuaState() { return m_luaState; }

    // Stats
    u32 GetLoadedScriptCount() const;
    bool IsScriptLoaded(ScriptId id) const;

private:
    void InitLuaState();
    void BindMathTypes();
    void BindInputSystem();
    void BindECSSystem();
    void BindPhysicsSystem();
    void BindAudioSystem();

    struct ScriptEntry {
        bool        loaded = false;
        std::string path;
        std::string source;
        u64         lastModifiedTime = 0;
        // In production: sol::environment per script for isolation
    };

    struct Impl;
    std::unique_ptr<Impl> m_impl;

    ScriptEngineConfig m_config;
    void* m_luaState = nullptr; // lua_State* (or sol::state*)
    std::vector<ScriptEntry> m_scripts;
    std::vector<u32> m_freeSlots;
    std::unordered_map<std::string, NativeBinding> m_nativeBindings;
    f32 m_hotReloadTimer = 0;
};

} // namespace nge::scripting
