#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>

namespace nge::rhi {

// ─── GPU Viewport/Scissor State Manager ──────────────────────────────────
// Manages dynamic viewport and scissor rect stacks for multi-viewport
// rendering scenarios: cascaded shadow maps, split-screen, VR stereo,
// cubemap face rendering, and UI overlay viewports.

struct Viewport {
    f32 x = 0, y = 0;
    f32 width = 0, height = 0;
    f32 minDepth = 0.0f;
    f32 maxDepth = 1.0f;
};

struct ScissorRect {
    i32 x = 0, y = 0;
    u32 width = 0, height = 0;
};

struct ViewportScissorPair {
    Viewport    viewport;
    ScissorRect scissor;
};

class ViewportStateManager {
public:
    // Set the primary viewport
    void SetViewport(f32 x, f32 y, f32 width, f32 height, f32 minDepth = 0.0f, f32 maxDepth = 1.0f);
    void SetViewport(const Viewport& vp);

    // Set the primary scissor rect
    void SetScissor(i32 x, i32 y, u32 width, u32 height);
    void SetScissor(const ScissorRect& rect);

    // Stack operations (push/pop for nested rendering)
    void Push(const Viewport& vp, const ScissorRect& scissor);
    void Push(const ViewportScissorPair& pair);
    void Pop();
    u32 GetStackDepth() const { return static_cast<u32>(m_stack.size()); }

    // Apply current viewport/scissor to command list
    void Apply(ICommandList* cmd) const;

    // Multi-viewport (for geometry shaders, VR)
    void SetMultiViewport(const std::vector<Viewport>& viewports, const std::vector<ScissorRect>& scissors);
    void ApplyMulti(ICommandList* cmd) const;

    // Get current state
    const Viewport& GetViewport() const { return m_current.viewport; }
    const ScissorRect& GetScissor() const { return m_current.scissor; }

    // Presets
    static ViewportScissorPair Fullscreen(u32 width, u32 height);
    static ViewportScissorPair ShadowCascade(u32 atlasSize, u32 cascadeIndex, u32 cascadeCount);
    static ViewportScissorPair SplitScreenLeft(u32 width, u32 height);
    static ViewportScissorPair SplitScreenRight(u32 width, u32 height);
    static ViewportScissorPair CubemapFace(u32 resolution, u32 faceIndex);
    static std::vector<ViewportScissorPair> StereoVR(u32 eyeWidth, u32 eyeHeight);

private:
    ViewportScissorPair m_current;
    std::vector<ViewportScissorPair> m_stack;

    // Multi-viewport state
    std::vector<Viewport> m_multiViewports;
    std::vector<ScissorRect> m_multiScissors;
};

} // namespace nge::rhi
