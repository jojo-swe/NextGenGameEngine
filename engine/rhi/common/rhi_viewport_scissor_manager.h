#pragma once

#include "engine/core/types.h"
#include <vector>
#include <string>
#include <mutex>

namespace nge::rhi {

// ─── GPU Viewport / Scissor State Manager ────────────────────────────────
// Manages viewport and scissor rect stacks for nested rendering contexts
// (e.g., shadow maps, reflections, UI overlays). Provides push/pop
// semantics, validation, and redundant-set detection.
//
// Use cases:
//   - Push/pop viewport for shadow map rendering
//   - Nested scissor rects for UI clipping
//   - Multi-viewport rendering (VR, split-screen)
//   - Validate viewport/scissor dimensions
//   - Detect redundant viewport/scissor sets

struct Viewport {
    float x, y, width, height;
    float minDepth, maxDepth;

    bool operator==(const Viewport& other) const {
        return x == other.x && y == other.y &&
               width == other.width && height == other.height &&
               minDepth == other.minDepth && maxDepth == other.maxDepth;
    }
};

struct ScissorRect {
    i32 x, y;
    u32 width, height;

    bool operator==(const ScissorRect& other) const {
        return x == other.x && y == other.y &&
               width == other.width && height == other.height;
    }
};

struct ViewportScissorConfig {
    u32  maxStackDepth = 16;
    u32  maxViewports = 16;     // For multi-viewport rendering
    bool validateDimensions = true;
    bool trackRedundantSets = true;
};

struct ViewportScissorStats {
    u32 viewportSets;
    u32 scissorSets;
    u32 viewportRedundant;
    u32 scissorRedundant;
    u32 pushCount;
    u32 popCount;
    u32 validationErrors;
    u32 currentStackDepth;
};

class ViewportScissorManager {
public:
    bool Init(const ViewportScissorConfig& config = {});
    void Shutdown();

    // Set the current viewport
    void SetViewport(const Viewport& vp, u32 index = 0);

    // Set the current scissor rect
    void SetScissor(const ScissorRect& rect, u32 index = 0);

    // Push current viewport+scissor onto stack
    bool Push();

    // Pop viewport+scissor from stack
    bool Pop();

    // Get current viewport
    Viewport GetViewport(u32 index = 0) const;

    // Get current scissor rect
    ScissorRect GetScissor(u32 index = 0) const;

    // Get stack depth
    u32 GetStackDepth() const;

    // Validate current viewport dimensions
    bool ValidateViewport(u32 index = 0) const;

    // Validate current scissor dimensions
    bool ValidateScissor(u32 index = 0) const;

    // Set viewport and scissor to match a render target
    void SetFullscreen(u32 width, u32 height, u32 index = 0);

    // Get how many viewports are active
    u32 GetActiveViewportCount() const;

    void ResetFrameCounters();
    void Reset();

    ViewportScissorStats GetStats() const;

private:
    struct ViewportScissorState {
        std::vector<Viewport> viewports;
        std::vector<ScissorRect> scissors;
    };

    ViewportScissorConfig m_config;

    ViewportScissorState m_current;
    std::vector<ViewportScissorState> m_stack;

    u32 m_activeViewportCount = 0;

    mutable u32 m_viewportSets = 0;
    mutable u32 m_scissorSets = 0;
    mutable u32 m_viewportRedundant = 0;
    mutable u32 m_scissorRedundant = 0;
    mutable u32 m_pushCount = 0;
    mutable u32 m_popCount = 0;
    mutable u32 m_validationErrors = 0;

    mutable std::mutex m_mutex;
};

} // namespace nge::rhi
