#include "engine/scripting/script_engine.h"
#include "engine/core/logging/log.h"
#include "engine/core/assert.h"
#include <fstream>
#include <filesystem>

// Sol2/Lua integration — when available via vcpkg:
// #define SOL_ALL_SAFETIES_ON 1
// #include <sol/sol.hpp>

namespace nge::scripting {

struct ScriptEngine::Impl {
    // In production: sol::state lua;
    // For now, stub implementation
};

ScriptEngine::~ScriptEngine() {
    Shutdown();
}

bool ScriptEngine::Init(const ScriptEngineConfig& config) {
    m_config = config;
    m_impl = std::make_unique<Impl>();

    InitLuaState();
    BindEngineTypes();

    NGE_LOG_INFO("Script engine initialized (stub): dir='{}', hotReload={}",
                 config.scriptsDirectory, config.enableHotReload);
    return true;
}

void ScriptEngine::Shutdown() {
    m_scripts.clear();
    m_freeSlots.clear();
    m_nativeBindings.clear();
    m_impl.reset();
    m_luaState = nullptr;
}

void ScriptEngine::InitLuaState() {
    // sol::state& lua = m_impl->lua;
    // lua.open_libraries(sol::lib::base, sol::lib::math, sol::lib::string,
    //                    sol::lib::table, sol::lib::coroutine, sol::lib::os);
    //
    // // Set script search path
    // lua["package"]["path"] = m_config.scriptsDirectory + "?.lua;" +
    //                          m_config.scriptsDirectory + "?/init.lua";
    //
    // m_luaState = lua.lua_state();
    NGE_LOG_DEBUG("Lua state initialized (stub)");
}

ScriptId ScriptEngine::LoadScript(const std::string& path) {
    std::string fullPath = m_config.scriptsDirectory + path;

    // Read file
    std::ifstream file(fullPath);
    if (!file.is_open()) {
        NGE_LOG_ERROR("Failed to load script: {}", fullPath);
        return INVALID_SCRIPT;
    }

    std::string source((std::istreambuf_iterator<char>(file)),
                        std::istreambuf_iterator<char>());

    // Allocate slot
    ScriptId id;
    if (!m_freeSlots.empty()) {
        id = m_freeSlots.back();
        m_freeSlots.pop_back();
    } else {
        id = static_cast<ScriptId>(m_scripts.size());
        m_scripts.emplace_back();
    }

    auto& entry = m_scripts[id];
    entry.loaded = true;
    entry.path = path;
    entry.source = std::move(source);

    // Get last modified time for hot-reload
    std::error_code ec;
    auto ftime = std::filesystem::last_write_time(fullPath, ec);
    if (!ec) {
        entry.lastModifiedTime = static_cast<u64>(
            ftime.time_since_epoch().count());
    }

    // Compile in Lua
    // sol::protected_function_result result = m_impl->lua.script(entry.source, sol::script_pass_on_error);
    // if (!result.valid()) {
    //     sol::error err = result;
    //     NGE_LOG_ERROR("Script compile error in '{}': {}", path, err.what());
    //     entry.loaded = false;
    //     m_freeSlots.push_back(id);
    //     return INVALID_SCRIPT;
    // }

    NGE_LOG_INFO("Loaded script {}: '{}'", id, path);
    return id;
}

void ScriptEngine::UnloadScript(ScriptId id) {
    if (id >= m_scripts.size() || !m_scripts[id].loaded) return;
    m_scripts[id].loaded = false;
    m_scripts[id].source.clear();
    m_freeSlots.push_back(id);
    NGE_LOG_DEBUG("Unloaded script {}", id);
}

bool ScriptEngine::ReloadScript(ScriptId id) {
    if (id >= m_scripts.size() || !m_scripts[id].loaded) return false;

    std::string path = m_scripts[id].path;
    UnloadScript(id);

    // Re-use same ID by loading into same slot
    ScriptId newId = LoadScript(path);
    return newId != INVALID_SCRIPT;
}

void ScriptEngine::CheckHotReload() {
    if (!m_config.enableHotReload) return;

    for (u32 i = 0; i < static_cast<u32>(m_scripts.size()); ++i) {
        auto& entry = m_scripts[i];
        if (!entry.loaded) continue;

        std::string fullPath = m_config.scriptsDirectory + entry.path;
        std::error_code ec;
        auto ftime = std::filesystem::last_write_time(fullPath, ec);
        if (ec) continue;

        u64 currentTime = static_cast<u64>(ftime.time_since_epoch().count());
        if (currentTime != entry.lastModifiedTime) {
            NGE_LOG_INFO("Hot-reloading script: {}", entry.path);
            ReloadScript(i);
        }
    }
}

void ScriptEngine::OnEntityCreate(ecs::Entity entity, ScriptId scriptId) {
    if (scriptId >= m_scripts.size() || !m_scripts[scriptId].loaded) return;

    // sol::protected_function onCreate = m_impl->lua["OnCreate"];
    // if (onCreate.valid()) {
    //     sol::protected_function_result result = onCreate(entity.id);
    //     if (!result.valid()) {
    //         sol::error err = result;
    //         NGE_LOG_ERROR("Script OnCreate error: {}", err.what());
    //     }
    // }
    (void)entity;
}

void ScriptEngine::OnEntityUpdate(ecs::Entity entity, ScriptId scriptId, f32 deltaTime) {
    if (scriptId >= m_scripts.size() || !m_scripts[scriptId].loaded) return;

    // sol::protected_function onUpdate = m_impl->lua["OnUpdate"];
    // if (onUpdate.valid()) {
    //     sol::protected_function_result result = onUpdate(entity.id, deltaTime);
    //     if (!result.valid()) {
    //         sol::error err = result;
    //         NGE_LOG_ERROR("Script OnUpdate error: {}", err.what());
    //     }
    // }
    (void)entity;
    (void)deltaTime;
}

void ScriptEngine::OnEntityDestroy(ecs::Entity entity, ScriptId scriptId) {
    if (scriptId >= m_scripts.size() || !m_scripts[scriptId].loaded) return;
    (void)entity;
}

void ScriptEngine::FireEvent(const std::string& eventName, ecs::Entity entity) {
    // sol::protected_function handler = m_impl->lua[eventName];
    // if (handler.valid()) handler(entity.id);
    (void)eventName;
    (void)entity;
}

void ScriptEngine::RegisterFunction(const std::string& name, std::function<void()> func) {
    m_nativeBindings[name] = {name, "", std::move(func)};
    // m_impl->lua.set_function(name, func);
}

void ScriptEngine::RegisterModule(const std::string& moduleName, std::function<void()> registrar) {
    // sol::table module = m_impl->lua.create_named_table(moduleName);
    // registrar();
    (void)moduleName;
    (void)registrar;
}

void ScriptEngine::BindEngineTypes() {
    BindMathTypes();
    BindInputSystem();
    BindECSSystem();
    BindPhysicsSystem();
    BindAudioSystem();
}

void ScriptEngine::BindMathTypes() {
    // sol::state& lua = m_impl->lua;
    //
    // lua.new_usertype<math::Vec3>("Vec3",
    //     sol::constructors<math::Vec3(), math::Vec3(f32, f32, f32)>(),
    //     "x", &math::Vec3::x, "y", &math::Vec3::y, "z", &math::Vec3::z,
    //     "Length", &math::Vec3::Length,
    //     "Normalized", &math::Vec3::Normalized,
    //     "Dot", &math::Vec3::Dot,
    //     "Cross", &math::Vec3::Cross,
    //     sol::meta_function::addition, sol::resolve<math::Vec3(const math::Vec3&) const>(&math::Vec3::operator+),
    //     sol::meta_function::subtraction, sol::resolve<math::Vec3(const math::Vec3&) const>(&math::Vec3::operator-)
    // );
    //
    // lua.new_usertype<math::Vec4>("Vec4", ...);
    // lua.new_usertype<math::Mat4>("Mat4", ...);
}

void ScriptEngine::BindInputSystem() {
    // lua["Input"] = lua.create_table_with(
    //     "IsKeyDown", &platform::Input::IsKeyDown,
    //     "IsKeyPressed", &platform::Input::IsKeyPressed,
    //     "GetMousePosition", &platform::Input::GetMousePosition,
    //     "GetMouseDelta", &platform::Input::GetMouseDelta
    // );
}

void ScriptEngine::BindECSSystem() {
    // lua.new_usertype<ecs::Entity>("Entity",
    //     "id", &ecs::Entity::id,
    //     "IsValid", &ecs::Entity::IsValid,
    //     "Index", &ecs::Entity::Index
    // );
}

void ScriptEngine::BindPhysicsSystem() {
    // Expose physics world raycasting, force application, etc.
}

void ScriptEngine::BindAudioSystem() {
    // Expose audio play/stop/volume control
}

bool ScriptEngine::Execute(const std::string& code, std::string& outError) {
    // sol::protected_function_result result = m_impl->lua.script(code, sol::script_pass_on_error);
    // if (!result.valid()) {
    //     sol::error err = result;
    //     outError = err.what();
    //     return false;
    // }
    (void)code;
    outError = "Script engine stub — Lua not yet integrated";
    return false;
}

u32 ScriptEngine::GetLoadedScriptCount() const {
    u32 count = 0;
    for (const auto& s : m_scripts) {
        if (s.loaded) count++;
    }
    return count;
}

bool ScriptEngine::IsScriptLoaded(ScriptId id) const {
    return id < m_scripts.size() && m_scripts[id].loaded;
}

} // namespace nge::scripting
