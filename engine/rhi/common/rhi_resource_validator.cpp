#include "engine/rhi/common/rhi_resource_validator.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

void ResourceStateValidator::Init(const ResourceValidatorConfig& config) {
    m_config = config;
    m_stats = {};
    m_currentFrame = 0;
    m_resources.clear();
    m_destroyedHandles.clear();
    m_messages.clear();

    if (config.enabled) {
        NGE_LOG_INFO("Resource state validator enabled (breakOnError={})", config.breakOnError);
    }
}

void ResourceStateValidator::Shutdown() {
    // Report any resources that were never destroyed
    for (const auto& [handle, res] : m_resources) {
        if (res.state != ValidationResourceState::Destroyed) {
            AddMessage(ValidationSeverity::Warning,
                       "Resource leaked (never destroyed): " + res.name, res.name);
        }
    }
    m_resources.clear();
    m_destroyedHandles.clear();
}

void ResourceStateValidator::TrackCreate(u64 handle, const std::string& name) {
    if (!m_config.enabled) return;
    std::lock_guard lock(m_mutex);

    if (m_destroyedHandles.count(handle)) {
        // Handle reuse after destroy — remove from destroyed set
        m_destroyedHandles.erase(handle);
    }

    if (m_resources.count(handle)) {
        AddMessage(ValidationSeverity::Error,
                   "Double-create: resource handle already tracked: " + name, name);
        return;
    }

    TrackedResource res;
    res.handle = handle;
    res.name = name;
    res.state = ValidationResourceState::Uninitialized;
    res.createFrame = m_currentFrame;
    m_resources[handle] = std::move(res);
}

void ResourceStateValidator::TrackDestroy(u64 handle) {
    if (!m_config.enabled) return;
    std::lock_guard lock(m_mutex);

    if (m_destroyedHandles.count(handle)) {
        AddMessage(ValidationSeverity::Error,
                   "Double-free: resource already destroyed", GetResourceName(handle));
        return;
    }

    auto it = m_resources.find(handle);
    if (it == m_resources.end()) {
        AddMessage(ValidationSeverity::Error,
                   "Destroying untracked resource handle", "unknown");
        return;
    }

    if (it->second.state == ValidationResourceState::Writing || it->second.state == ValidationResourceState::Reading) {
        AddMessage(ValidationSeverity::Error,
                   "Destroying resource while still in use: " + it->second.name, it->second.name);
    }

    m_destroyedHandles.insert(handle);
    m_resources.erase(it);
}

bool ResourceStateValidator::ValidateRead(u64 handle, const std::string& passName) {
    if (!m_config.enabled) return true;
    std::lock_guard lock(m_mutex);
    m_stats.totalValidations++;

    // Use-after-destroy check
    if (m_destroyedHandles.count(handle)) {
        AddMessage(ValidationSeverity::Fatal,
                   "Use-after-destroy: reading destroyed resource in pass '" + passName + "'",
                   "destroyed");
        return false;
    }

    auto it = m_resources.find(handle);
    if (it == m_resources.end()) {
        AddMessage(ValidationSeverity::Error,
                   "Reading untracked resource in pass '" + passName + "'", "unknown");
        return false;
    }

    if (it->second.state == ValidationResourceState::Uninitialized) {
        AddMessage(ValidationSeverity::Error,
                   "Reading uninitialized resource '" + it->second.name + "' in pass '" + passName + "'",
                   it->second.name);
        return false;
    }

    if (it->second.state == ValidationResourceState::Writing) {
        AddMessage(ValidationSeverity::Error,
                   "Read-while-write hazard: '" + it->second.name + "' written by '" +
                   it->second.lastWriter + "', read in '" + passName + "' without barrier",
                   it->second.name);
        return false;
    }

    it->second.state = ValidationResourceState::Reading;
    return true;
}

bool ResourceStateValidator::ValidateWrite(u64 handle, const std::string& passName) {
    if (!m_config.enabled) return true;
    std::lock_guard lock(m_mutex);
    m_stats.totalValidations++;

    if (m_destroyedHandles.count(handle)) {
        AddMessage(ValidationSeverity::Fatal,
                   "Use-after-destroy: writing to destroyed resource in pass '" + passName + "'",
                   "destroyed");
        return false;
    }

    auto it = m_resources.find(handle);
    if (it == m_resources.end()) {
        AddMessage(ValidationSeverity::Error,
                   "Writing to untracked resource in pass '" + passName + "'", "unknown");
        return false;
    }

    if (it->second.state == ValidationResourceState::Reading) {
        AddMessage(ValidationSeverity::Error,
                   "Write-after-read hazard: '" + it->second.name + "' still being read, "
                   "write attempted in '" + passName + "' without barrier",
                   it->second.name);
        return false;
    }

    if (it->second.state == ValidationResourceState::Writing) {
        AddMessage(ValidationSeverity::Warning,
                   "Write-after-write: '" + it->second.name + "' overwritten by '" +
                   passName + "' (previously by '" + it->second.lastWriter + "')",
                   it->second.name);
    }

    it->second.state = ValidationResourceState::Writing;
    it->second.lastWriter = passName;
    return true;
}

bool ResourceStateValidator::ValidateTransition(u64 handle, ImageLayout from, ImageLayout to,
                                                  const std::string& passName) {
    if (!m_config.enabled) return true;
    std::lock_guard lock(m_mutex);
    m_stats.totalValidations++;

    if (m_destroyedHandles.count(handle)) {
        AddMessage(ValidationSeverity::Fatal,
                   "Transitioning destroyed resource in pass '" + passName + "'", "destroyed");
        return false;
    }

    auto it = m_resources.find(handle);
    if (it == m_resources.end()) {
        AddMessage(ValidationSeverity::Error,
                   "Transitioning untracked resource in pass '" + passName + "'", "unknown");
        return false;
    }

    if (from == to) {
        AddMessage(ValidationSeverity::Info,
                   "Redundant transition (" + std::string(ImageLayoutTracker::LayoutName(from)) +
                   " -> " + std::string(ImageLayoutTracker::LayoutName(to)) +
                   ") in pass '" + passName + "'",
                   it->second.name);
    }

    if (from == ImageLayout::Undefined && it->second.state != ValidationResourceState::Uninitialized) {
        AddMessage(ValidationSeverity::Warning,
                   "Transitioning from Undefined but resource was previously written (data loss): '" +
                   it->second.name + "' in pass '" + passName + "'",
                   it->second.name);
    }

    return true;
}

void ResourceStateValidator::MarkWritten(u64 handle) {
    if (!m_config.enabled) return;
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(handle);
    if (it != m_resources.end()) {
        it->second.state = ValidationResourceState::Writing;
    }
}

void ResourceStateValidator::MarkIdle(u64 handle) {
    if (!m_config.enabled) return;
    std::lock_guard lock(m_mutex);
    auto it = m_resources.find(handle);
    if (it != m_resources.end()) {
        it->second.state = ValidationResourceState::Idle;
    }
}

void ResourceStateValidator::BeginFrame(u64 frameNumber) {
    m_currentFrame = frameNumber;
}

void ResourceStateValidator::EndFrame() {
    // Could check for resources left in Writing/Reading state
}

void ResourceStateValidator::ClearMessages() {
    std::lock_guard lock(m_mutex);
    m_messages.clear();
}

ResourceValidatorStats ResourceStateValidator::GetStats() const {
    std::lock_guard lock(m_mutex);
    auto stats = m_stats;
    stats.trackedResources = static_cast<u32>(m_resources.size());
    return stats;
}

void ResourceStateValidator::AddMessage(ValidationSeverity severity, const std::string& msg,
                                          const std::string& resourceName) {
    if (m_messages.size() >= m_config.maxMessages) return;

    ValidationMessage vmsg;
    vmsg.severity = severity;
    vmsg.message = msg;
    vmsg.resourceName = resourceName;
    vmsg.frameNumber = m_currentFrame;
    m_messages.push_back(vmsg);

    switch (severity) {
        case ValidationSeverity::Info:
            m_stats.infos++;
            if (m_config.logWarnings) NGE_LOG_DEBUG("[Validator] {}", msg);
            break;
        case ValidationSeverity::Warning:
            m_stats.warnings++;
            NGE_LOG_WARN("[Validator] {}", msg);
            break;
        case ValidationSeverity::Error:
            m_stats.errors++;
            NGE_LOG_ERROR("[Validator] {}", msg);
            break;
        case ValidationSeverity::Fatal:
            m_stats.errors++;
            NGE_LOG_ERROR("[Validator FATAL] {}", msg);
            if (m_config.breakOnError) {
                // Platform-specific debugger break
                #ifdef _MSC_VER
                    __debugbreak();
                #elif defined(__GNUC__)
                    __builtin_trap();
                #endif
            }
            break;
    }
}

std::string ResourceStateValidator::GetResourceName(u64 handle) const {
    auto it = m_resources.find(handle);
    if (it != m_resources.end()) return it->second.name;
    return "handle_" + std::to_string(handle);
}

} // namespace nge::rhi
