#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include "engine/rhi/common/rhi_image_layout_tracker.h"
#include <vector>
#include <string>
#include <unordered_set>
#include <mutex>

namespace nge::rhi {

// ─── GPU Resource State Validator ────────────────────────────────────────
// Debug-mode validation layer that checks resource usage correctness at
// runtime. Catches common errors:
//   - Reading from uninitialized resources
//   - Writing to resources still in use by another pass
//   - Missing barriers before layout transitions
//   - Use-after-destroy
//   - Double-free
//   - Binding a resource that hasn't been transitioned to the right layout
//
// Disabled in release builds (zero overhead).

#ifdef NDEBUG
    #define NGE_RESOURCE_VALIDATE(validator, ...) ((void)0)
#else
    #define NGE_RESOURCE_VALIDATE(validator, ...) (validator).__VA_ARGS__
#endif

enum class ValidationResourceState : u8 {
    Uninitialized,
    Idle,
    Reading,
    Writing,
    Destroyed,
};

enum class ValidationSeverity : u8 {
    Info,
    Warning,
    Error,
    Fatal,
};

struct ValidationMessage {
    ValidationSeverity severity;
    std::string        message;
    std::string        resourceName;
    u64                frameNumber;
};

struct ResourceValidatorConfig {
    bool enabled = true;
    bool breakOnError = false;       // Trigger debugger break on error
    bool logWarnings = true;
    u32  maxMessages = 1000;
};

struct ResourceValidatorStats {
    u32 trackedResources;
    u32 errors;
    u32 warnings;
    u32 infos;
    u32 totalValidations;
};

class ResourceStateValidator {
public:
    void Init(const ResourceValidatorConfig& config = {});
    void Shutdown();

    // Track a newly created resource
    void TrackCreate(u64 handle, const std::string& name);

    // Mark resource as destroyed
    void TrackDestroy(u64 handle);

    // Validate a read operation
    bool ValidateRead(u64 handle, const std::string& passName);

    // Validate a write operation
    bool ValidateWrite(u64 handle, const std::string& passName);

    // Validate a layout transition
    bool ValidateTransition(u64 handle, ImageLayout from, ImageLayout to, const std::string& passName);

    // Mark resource as written (after validated write)
    void MarkWritten(u64 handle);

    // Mark resource as idle (after barrier)
    void MarkIdle(u64 handle);

    // Begin/end frame
    void BeginFrame(u64 frameNumber);
    void EndFrame();

    // Get validation messages
    const std::vector<ValidationMessage>& GetMessages() const { return m_messages; }
    void ClearMessages();

    // Check if any errors occurred
    bool HasErrors() const { return m_stats.errors > 0; }

    ResourceValidatorStats GetStats() const;

private:
    void AddMessage(ValidationSeverity severity, const std::string& msg,
                    const std::string& resourceName);
    std::string GetResourceName(u64 handle) const;

    struct TrackedResource {
        u64            handle;
        std::string    name;
        ValidationResourceState  state;
        std::string    lastWriter;
        u64            createFrame;
    };

    ResourceValidatorConfig m_config;
    std::unordered_map<u64, TrackedResource> m_resources;
    std::unordered_set<u64> m_destroyedHandles; // Detect use-after-destroy
    std::vector<ValidationMessage> m_messages;
    ResourceValidatorStats m_stats{};
    u64 m_currentFrame = 0;
    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
