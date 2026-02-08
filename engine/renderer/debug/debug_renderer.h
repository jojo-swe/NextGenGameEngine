#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>

namespace nge::renderer {

// ─── Debug Renderer ──────────────────────────────────────────────────────
// Immediate-mode debug drawing for lines, boxes, spheres, text, etc.
// Collects primitives during the frame, then batch-renders them in one pass.
// Used for gizmos, physics debug visualization, navigation debug, etc.

struct DebugVertex {
    math::Vec3 position;
    math::Vec4 color;
};

struct DebugLine {
    DebugVertex a, b;
};

class DebugRenderer {
public:
    bool Init(rhi::IDevice* device, u32 maxVertices = 65536);
    void Shutdown();

    // ── Drawing API (call during frame, before Flush) ────────────────

    void DrawLine(const math::Vec3& from, const math::Vec3& to,
                   const math::Vec4& color = {1, 1, 1, 1});

    void DrawRay(const math::Vec3& origin, const math::Vec3& direction,
                  f32 length = 1.0f, const math::Vec4& color = {1, 1, 0, 1});

    void DrawBox(const math::Vec3& center, const math::Vec3& halfExtents,
                  const math::Vec4& color = {0, 1, 0, 1});

    void DrawOBB(const math::Vec3& center, const math::Vec3& halfExtents,
                  const math::Vec4& rotation, const math::Vec4& color = {0, 1, 0, 1});

    void DrawSphere(const math::Vec3& center, f32 radius,
                     const math::Vec4& color = {0, 0.5f, 1, 1}, u32 segments = 16);

    void DrawCapsule(const math::Vec3& a, const math::Vec3& b, f32 radius,
                      const math::Vec4& color = {0, 0.5f, 1, 1}, u32 segments = 12);

    void DrawCircle(const math::Vec3& center, const math::Vec3& normal, f32 radius,
                     const math::Vec4& color = {1, 1, 0, 1}, u32 segments = 32);

    void DrawFrustum(const math::Mat4& viewProj, const math::Vec4& color = {1, 0.5f, 0, 1});

    void DrawGrid(const math::Vec3& center, f32 size, u32 divisions,
                   const math::Vec4& color = {0.3f, 0.3f, 0.3f, 0.5f});

    void DrawArrow(const math::Vec3& from, const math::Vec3& to,
                    f32 headSize = 0.1f, const math::Vec4& color = {1, 0, 0, 1});

    void DrawCone(const math::Vec3& apex, const math::Vec3& direction,
                   f32 height, f32 angle, const math::Vec4& color = {1, 1, 0, 1},
                   u32 segments = 16);

    void DrawAxis(const math::Vec3& origin, f32 size = 1.0f);

    // Text (screen-space, requires font system)
    // void DrawText(const math::Vec3& worldPos, const std::string& text,
    //               const math::Vec4& color = {1, 1, 1, 1});

    // ── Persistent lines (survive across frames) ─────────────────────

    void DrawLinePersistent(const math::Vec3& from, const math::Vec3& to,
                             const math::Vec4& color, f32 duration);

    // ── Rendering ────────────────────────────────────────────────────

    void Flush(rhi::ICommandList* cmd, const math::Mat4& viewProj);
    void Clear();

    // Stats
    u32 GetLineCount() const { return static_cast<u32>(m_lines.size()); }
    u32 GetVertexCount() const { return static_cast<u32>(m_lines.size()) * 2; }

    // Enable/disable
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

    // Depth testing
    void SetDepthTest(bool enabled) { m_depthTest = enabled; }
    bool GetDepthTest() const { return m_depthTest; }

private:
    void AddLine(const math::Vec3& a, const math::Vec3& b, const math::Vec4& color);

    // Quaternion rotation helper
    math::Vec3 RotatePoint(const math::Vec3& point, const math::Vec4& quat) const;

    rhi::IDevice* m_device = nullptr;

    std::vector<DebugLine> m_lines;

    // Persistent lines with duration
    struct PersistentLine {
        DebugLine line;
        f32 remaining; // seconds
    };
    std::vector<PersistentLine> m_persistentLines;

    // GPU resources
    rhi::BufferHandle   m_vertexBuffer;
    rhi::PipelineHandle m_pipeline;
    rhi::PipelineHandle m_pipelineNoDepth;
    u32                 m_maxVertices = 0;

    bool m_enabled = true;
    bool m_depthTest = true;
};

} // namespace nge::renderer
