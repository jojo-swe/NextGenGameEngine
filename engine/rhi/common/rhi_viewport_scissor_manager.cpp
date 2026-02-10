#include "engine/rhi/common/rhi_viewport_scissor_manager.h"
#include "engine/core/logging/log.h"

namespace nge::rhi {

bool ViewportScissorManager::Init(const ViewportScissorConfig& config) {
    m_config = config;
    m_activeViewportCount = 0;

    m_current.viewports.resize(config.maxViewports, {0, 0, 0, 0, 0.0f, 1.0f});
    m_current.scissors.resize(config.maxViewports, {0, 0, 0, 0});

    m_viewportSets = 0;
    m_scissorSets = 0;
    m_viewportRedundant = 0;
    m_scissorRedundant = 0;
    m_pushCount = 0;
    m_popCount = 0;
    m_validationErrors = 0;

    NGE_LOG_INFO("Viewport/scissor manager initialized: maxStack={}, maxViewports={}, validate={}, trackRedundant={}",
                 config.maxStackDepth, config.maxViewports, config.validateDimensions, config.trackRedundantSets);
    return true;
}

void ViewportScissorManager::Shutdown() {
    m_stack.clear();
    m_current.viewports.clear();
    m_current.scissors.clear();
}

void ViewportScissorManager::SetViewport(const Viewport& vp, u32 index) {
    std::lock_guard lock(m_mutex);

    if (index >= m_config.maxViewports) return;

    if (m_config.trackRedundantSets && index < m_current.viewports.size() &&
        m_current.viewports[index] == vp) {
        m_viewportRedundant++;
    }

    if (index >= m_current.viewports.size()) {
        m_current.viewports.resize(index + 1, {0, 0, 0, 0, 0.0f, 1.0f});
    }

    m_current.viewports[index] = vp;
    m_viewportSets++;

    if (index >= m_activeViewportCount) {
        m_activeViewportCount = index + 1;
    }
}

void ViewportScissorManager::SetScissor(const ScissorRect& rect, u32 index) {
    std::lock_guard lock(m_mutex);

    if (index >= m_config.maxViewports) return;

    if (m_config.trackRedundantSets && index < m_current.scissors.size() &&
        m_current.scissors[index] == rect) {
        m_scissorRedundant++;
    }

    if (index >= m_current.scissors.size()) {
        m_current.scissors.resize(index + 1, {0, 0, 0, 0});
    }

    m_current.scissors[index] = rect;
    m_scissorSets++;
}

bool ViewportScissorManager::Push() {
    std::lock_guard lock(m_mutex);

    if (m_stack.size() >= m_config.maxStackDepth) {
        NGE_LOG_WARN("Viewport/scissor stack overflow (depth={})", m_config.maxStackDepth);
        return false;
    }

    m_stack.push_back(m_current);
    m_pushCount++;
    return true;
}

bool ViewportScissorManager::Pop() {
    std::lock_guard lock(m_mutex);

    if (m_stack.empty()) {
        NGE_LOG_WARN("Viewport/scissor stack underflow");
        return false;
    }

    m_current = m_stack.back();
    m_stack.pop_back();
    m_popCount++;
    return true;
}

Viewport ViewportScissorManager::GetViewport(u32 index) const {
    std::lock_guard lock(m_mutex);

    if (index >= m_current.viewports.size()) return {0, 0, 0, 0, 0.0f, 1.0f};
    return m_current.viewports[index];
}

ScissorRect ViewportScissorManager::GetScissor(u32 index) const {
    std::lock_guard lock(m_mutex);

    if (index >= m_current.scissors.size()) return {0, 0, 0, 0};
    return m_current.scissors[index];
}

u32 ViewportScissorManager::GetStackDepth() const {
    std::lock_guard lock(m_mutex);
    return static_cast<u32>(m_stack.size());
}

bool ViewportScissorManager::ValidateViewport(u32 index) const {
    std::lock_guard lock(m_mutex);

    if (!m_config.validateDimensions) return true;
    if (index >= m_current.viewports.size()) {
        m_validationErrors++;
        return false;
    }

    const auto& vp = m_current.viewports[index];

    if (vp.width <= 0.0f || vp.height <= 0.0f) {
        m_validationErrors++;
        return false;
    }

    if (vp.minDepth < 0.0f || vp.maxDepth > 1.0f || vp.minDepth > vp.maxDepth) {
        m_validationErrors++;
        return false;
    }

    return true;
}

bool ViewportScissorManager::ValidateScissor(u32 index) const {
    std::lock_guard lock(m_mutex);

    if (!m_config.validateDimensions) return true;
    if (index >= m_current.scissors.size()) {
        m_validationErrors++;
        return false;
    }

    const auto& sc = m_current.scissors[index];

    if (sc.width == 0 || sc.height == 0) {
        m_validationErrors++;
        return false;
    }

    return true;
}

void ViewportScissorManager::SetFullscreen(u32 width, u32 height, u32 index) {
    Viewport vp;
    vp.x = 0.0f;
    vp.y = 0.0f;
    vp.width = static_cast<float>(width);
    vp.height = static_cast<float>(height);
    vp.minDepth = 0.0f;
    vp.maxDepth = 1.0f;

    ScissorRect sc;
    sc.x = 0;
    sc.y = 0;
    sc.width = width;
    sc.height = height;

    SetViewport(vp, index);
    SetScissor(sc, index);
}

u32 ViewportScissorManager::GetActiveViewportCount() const {
    std::lock_guard lock(m_mutex);
    return m_activeViewportCount;
}

void ViewportScissorManager::ResetFrameCounters() {
    std::lock_guard lock(m_mutex);

    m_viewportSets = 0;
    m_scissorSets = 0;
    m_viewportRedundant = 0;
    m_scissorRedundant = 0;
}

void ViewportScissorManager::Reset() {
    std::lock_guard lock(m_mutex);

    m_stack.clear();
    m_current.viewports.assign(m_config.maxViewports, {0, 0, 0, 0, 0.0f, 1.0f});
    m_current.scissors.assign(m_config.maxViewports, {0, 0, 0, 0});
    m_activeViewportCount = 0;

    m_viewportSets = 0;
    m_scissorSets = 0;
    m_viewportRedundant = 0;
    m_scissorRedundant = 0;
    m_pushCount = 0;
    m_popCount = 0;
    m_validationErrors = 0;
}

ViewportScissorStats ViewportScissorManager::GetStats() const {
    std::lock_guard lock(m_mutex);

    ViewportScissorStats stats{};
    stats.viewportSets = m_viewportSets;
    stats.scissorSets = m_scissorSets;
    stats.viewportRedundant = m_viewportRedundant;
    stats.scissorRedundant = m_scissorRedundant;
    stats.pushCount = m_pushCount;
    stats.popCount = m_popCount;
    stats.validationErrors = m_validationErrors;
    stats.currentStackDepth = static_cast<u32>(m_stack.size());

    return stats;
}

} // namespace nge::rhi
