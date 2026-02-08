#include "engine/rhi/common/rhi_viewport_state.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

void ViewportStateManager::SetViewport(f32 x, f32 y, f32 width, f32 height, f32 minDepth, f32 maxDepth) {
    m_current.viewport = {x, y, width, height, minDepth, maxDepth};
}

void ViewportStateManager::SetViewport(const Viewport& vp) {
    m_current.viewport = vp;
}

void ViewportStateManager::SetScissor(i32 x, i32 y, u32 width, u32 height) {
    m_current.scissor = {x, y, width, height};
}

void ViewportStateManager::SetScissor(const ScissorRect& rect) {
    m_current.scissor = rect;
}

void ViewportStateManager::Push(const Viewport& vp, const ScissorRect& scissor) {
    m_stack.push_back(m_current);
    m_current.viewport = vp;
    m_current.scissor = scissor;
}

void ViewportStateManager::Push(const ViewportScissorPair& pair) {
    m_stack.push_back(m_current);
    m_current = pair;
}

void ViewportStateManager::Pop() {
    if (!m_stack.empty()) {
        m_current = m_stack.back();
        m_stack.pop_back();
    }
}

void ViewportStateManager::Apply(ICommandList* cmd) const {
    cmd->SetViewport(m_current.viewport.x, m_current.viewport.y,
                     m_current.viewport.width, m_current.viewport.height,
                     m_current.viewport.minDepth, m_current.viewport.maxDepth);
    cmd->SetScissor(m_current.scissor.x, m_current.scissor.y,
                    m_current.scissor.width, m_current.scissor.height);
}

void ViewportStateManager::SetMultiViewport(const std::vector<Viewport>& viewports,
                                              const std::vector<ScissorRect>& scissors) {
    m_multiViewports = viewports;
    m_multiScissors = scissors;
}

void ViewportStateManager::ApplyMulti(ICommandList* cmd) const {
    // TODO: vkCmdSetViewport with count > 1
    // vkCmdSetScissor with count > 1
    for (u32 i = 0; i < static_cast<u32>(m_multiViewports.size()); ++i) {
        (void)cmd;
    }
}

ViewportScissorPair ViewportStateManager::Fullscreen(u32 width, u32 height) {
    ViewportScissorPair pair;
    pair.viewport = {0, 0, static_cast<f32>(width), static_cast<f32>(height), 0.0f, 1.0f};
    pair.scissor = {0, 0, width, height};
    return pair;
}

ViewportScissorPair ViewportStateManager::ShadowCascade(u32 atlasSize, u32 cascadeIndex, u32 cascadeCount) {
    u32 cascadesPerRow = (cascadeCount <= 2) ? cascadeCount : 2;
    u32 tileSize = atlasSize / cascadesPerRow;
    u32 col = cascadeIndex % cascadesPerRow;
    u32 row = cascadeIndex / cascadesPerRow;

    ViewportScissorPair pair;
    pair.viewport = {
        static_cast<f32>(col * tileSize),
        static_cast<f32>(row * tileSize),
        static_cast<f32>(tileSize),
        static_cast<f32>(tileSize),
        0.0f, 1.0f
    };
    pair.scissor = {
        static_cast<i32>(col * tileSize),
        static_cast<i32>(row * tileSize),
        tileSize, tileSize
    };
    return pair;
}

ViewportScissorPair ViewportStateManager::SplitScreenLeft(u32 width, u32 height) {
    u32 halfW = width / 2;
    ViewportScissorPair pair;
    pair.viewport = {0, 0, static_cast<f32>(halfW), static_cast<f32>(height), 0.0f, 1.0f};
    pair.scissor = {0, 0, halfW, height};
    return pair;
}

ViewportScissorPair ViewportStateManager::SplitScreenRight(u32 width, u32 height) {
    u32 halfW = width / 2;
    ViewportScissorPair pair;
    pair.viewport = {static_cast<f32>(halfW), 0, static_cast<f32>(halfW), static_cast<f32>(height), 0.0f, 1.0f};
    pair.scissor = {static_cast<i32>(halfW), 0, halfW, height};
    return pair;
}

ViewportScissorPair ViewportStateManager::CubemapFace(u32 resolution, u32 faceIndex) {
    // Cubemap atlas layout: 6 faces in a single row
    ViewportScissorPair pair;
    pair.viewport = {
        static_cast<f32>(faceIndex * resolution), 0,
        static_cast<f32>(resolution), static_cast<f32>(resolution),
        0.0f, 1.0f
    };
    pair.scissor = {
        static_cast<i32>(faceIndex * resolution), 0,
        resolution, resolution
    };
    return pair;
}

std::vector<ViewportScissorPair> ViewportStateManager::StereoVR(u32 eyeWidth, u32 eyeHeight) {
    std::vector<ViewportScissorPair> pairs(2);
    // Left eye
    pairs[0].viewport = {0, 0, static_cast<f32>(eyeWidth), static_cast<f32>(eyeHeight), 0.0f, 1.0f};
    pairs[0].scissor = {0, 0, eyeWidth, eyeHeight};
    // Right eye
    pairs[1].viewport = {static_cast<f32>(eyeWidth), 0, static_cast<f32>(eyeWidth), static_cast<f32>(eyeHeight), 0.0f, 1.0f};
    pairs[1].scissor = {static_cast<i32>(eyeWidth), 0, eyeWidth, eyeHeight};
    return pairs;
}

} // namespace nge::rhi
