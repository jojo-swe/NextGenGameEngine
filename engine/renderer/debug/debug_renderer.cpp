#include "engine/renderer/debug/debug_renderer.h"
#include "engine/core/logging/log.h"
#include <cmath>
#include <algorithm>

namespace nge::renderer {

bool DebugRenderer::Init(rhi::IDevice* device, u32 maxVertices) {
    m_device = device;
    m_maxVertices = maxVertices;

    rhi::BufferDesc desc;
    desc.size = maxVertices * sizeof(DebugVertex);
    desc.usage = rhi::BufferUsage::Vertex | rhi::BufferUsage::TransferDst;
    desc.memoryUsage = rhi::MemoryUsage::CPU_To_GPU;
    desc.debugName = "Debug_VertexBuffer";
    m_vertexBuffer = device->CreateBuffer(desc);

    // TODO: Create line rendering pipelines (with and without depth test)

    NGE_LOG_INFO("Debug renderer initialized: max {} vertices", maxVertices);
    return true;
}

void DebugRenderer::Shutdown() {
    if (m_device && m_vertexBuffer.IsValid()) {
        m_device->DestroyBuffer(m_vertexBuffer);
        m_vertexBuffer = {};
    }
    m_lines.clear();
    m_persistentLines.clear();
}

void DebugRenderer::AddLine(const math::Vec3& a, const math::Vec3& b, const math::Vec4& color) {
    if (m_lines.size() * 2 >= m_maxVertices) return;
    m_lines.push_back({{a, color}, {b, color}});
}

void DebugRenderer::DrawLine(const math::Vec3& from, const math::Vec3& to, const math::Vec4& color) {
    if (!m_enabled) return;
    AddLine(from, to, color);
}

void DebugRenderer::DrawRay(const math::Vec3& origin, const math::Vec3& direction,
                              f32 length, const math::Vec4& color) {
    DrawLine(origin, origin + direction * length, color);
}

void DebugRenderer::DrawBox(const math::Vec3& center, const math::Vec3& h,
                              const math::Vec4& color) {
    math::Vec3 corners[8] = {
        {center.x - h.x, center.y - h.y, center.z - h.z},
        {center.x + h.x, center.y - h.y, center.z - h.z},
        {center.x + h.x, center.y + h.y, center.z - h.z},
        {center.x - h.x, center.y + h.y, center.z - h.z},
        {center.x - h.x, center.y - h.y, center.z + h.z},
        {center.x + h.x, center.y - h.y, center.z + h.z},
        {center.x + h.x, center.y + h.y, center.z + h.z},
        {center.x - h.x, center.y + h.y, center.z + h.z},
    };

    // Bottom face
    AddLine(corners[0], corners[1], color);
    AddLine(corners[1], corners[2], color);
    AddLine(corners[2], corners[3], color);
    AddLine(corners[3], corners[0], color);
    // Top face
    AddLine(corners[4], corners[5], color);
    AddLine(corners[5], corners[6], color);
    AddLine(corners[6], corners[7], color);
    AddLine(corners[7], corners[4], color);
    // Verticals
    AddLine(corners[0], corners[4], color);
    AddLine(corners[1], corners[5], color);
    AddLine(corners[2], corners[6], color);
    AddLine(corners[3], corners[7], color);
}

math::Vec3 DebugRenderer::RotatePoint(const math::Vec3& p, const math::Vec4& q) const {
    // Quaternion rotation: v' = q * v * q^-1
    f32 qx = q.x, qy = q.y, qz = q.z, qw = q.w;
    f32 tx = 2.0f * (qy * p.z - qz * p.y);
    f32 ty = 2.0f * (qz * p.x - qx * p.z);
    f32 tz = 2.0f * (qx * p.y - qy * p.x);
    return {
        p.x + qw * tx + (qy * tz - qz * ty),
        p.y + qw * ty + (qz * tx - qx * tz),
        p.z + qw * tz + (qx * ty - qy * tx)
    };
}

void DebugRenderer::DrawOBB(const math::Vec3& center, const math::Vec3& h,
                              const math::Vec4& rotation, const math::Vec4& color) {
    math::Vec3 localCorners[8] = {
        {-h.x, -h.y, -h.z}, { h.x, -h.y, -h.z},
        { h.x,  h.y, -h.z}, {-h.x,  h.y, -h.z},
        {-h.x, -h.y,  h.z}, { h.x, -h.y,  h.z},
        { h.x,  h.y,  h.z}, {-h.x,  h.y,  h.z},
    };

    math::Vec3 corners[8];
    for (u32 i = 0; i < 8; ++i) {
        corners[i] = center + RotatePoint(localCorners[i], rotation);
    }

    AddLine(corners[0], corners[1], color); AddLine(corners[1], corners[2], color);
    AddLine(corners[2], corners[3], color); AddLine(corners[3], corners[0], color);
    AddLine(corners[4], corners[5], color); AddLine(corners[5], corners[6], color);
    AddLine(corners[6], corners[7], color); AddLine(corners[7], corners[4], color);
    AddLine(corners[0], corners[4], color); AddLine(corners[1], corners[5], color);
    AddLine(corners[2], corners[6], color); AddLine(corners[3], corners[7], color);
}

void DebugRenderer::DrawSphere(const math::Vec3& center, f32 radius,
                                 const math::Vec4& color, u32 segments) {
    f32 step = 2.0f * 3.14159265f / static_cast<f32>(segments);

    // XY circle
    for (u32 i = 0; i < segments; ++i) {
        f32 a0 = step * static_cast<f32>(i);
        f32 a1 = step * static_cast<f32>(i + 1);
        AddLine(
            center + math::Vec3{std::cos(a0) * radius, std::sin(a0) * radius, 0},
            center + math::Vec3{std::cos(a1) * radius, std::sin(a1) * radius, 0},
            color);
    }
    // XZ circle
    for (u32 i = 0; i < segments; ++i) {
        f32 a0 = step * static_cast<f32>(i);
        f32 a1 = step * static_cast<f32>(i + 1);
        AddLine(
            center + math::Vec3{std::cos(a0) * radius, 0, std::sin(a0) * radius},
            center + math::Vec3{std::cos(a1) * radius, 0, std::sin(a1) * radius},
            color);
    }
    // YZ circle
    for (u32 i = 0; i < segments; ++i) {
        f32 a0 = step * static_cast<f32>(i);
        f32 a1 = step * static_cast<f32>(i + 1);
        AddLine(
            center + math::Vec3{0, std::cos(a0) * radius, std::sin(a0) * radius},
            center + math::Vec3{0, std::cos(a1) * radius, std::sin(a1) * radius},
            color);
    }
}

void DebugRenderer::DrawCapsule(const math::Vec3& a, const math::Vec3& b, f32 radius,
                                  const math::Vec4& color, u32 segments) {
    DrawSphere(a, radius, color, segments);
    DrawSphere(b, radius, color, segments);

    // Connecting lines along the capsule axis
    math::Vec3 axis = b - a;
    f32 len = axis.Length();
    if (len < 0.001f) return;
    axis = axis * (1.0f / len);

    // Find perpendicular vectors
    math::Vec3 up = std::abs(axis.y) < 0.999f ? math::Vec3{0, 1, 0} : math::Vec3{1, 0, 0};
    math::Vec3 right = axis.Cross(up).Normalized();
    math::Vec3 forward = axis.Cross(right);

    for (u32 i = 0; i < 4; ++i) {
        f32 angle = static_cast<f32>(i) * 3.14159265f * 0.5f;
        math::Vec3 offset = (right * std::cos(angle) + forward * std::sin(angle)) * radius;
        AddLine(a + offset, b + offset, color);
    }
}

void DebugRenderer::DrawCircle(const math::Vec3& center, const math::Vec3& normal, f32 radius,
                                 const math::Vec4& color, u32 segments) {
    math::Vec3 n = normal.Normalized();
    math::Vec3 up = std::abs(n.y) < 0.999f ? math::Vec3{0, 1, 0} : math::Vec3{1, 0, 0};
    math::Vec3 right = n.Cross(up).Normalized();
    math::Vec3 forward = n.Cross(right);

    f32 step = 2.0f * 3.14159265f / static_cast<f32>(segments);
    for (u32 i = 0; i < segments; ++i) {
        f32 a0 = step * static_cast<f32>(i);
        f32 a1 = step * static_cast<f32>(i + 1);
        math::Vec3 p0 = center + (right * std::cos(a0) + forward * std::sin(a0)) * radius;
        math::Vec3 p1 = center + (right * std::cos(a1) + forward * std::sin(a1)) * radius;
        AddLine(p0, p1, color);
    }
}

void DebugRenderer::DrawFrustum(const math::Mat4& viewProj, const math::Vec4& color) {
    // Invert viewProj to get frustum corners in world space
    // For now, draw a simple placeholder box
    // TODO: Proper frustum extraction from invViewProj
    (void)viewProj;
    DrawBox({0, 0, -5}, {2, 1, 5}, color);
}

void DebugRenderer::DrawGrid(const math::Vec3& center, f32 size, u32 divisions,
                               const math::Vec4& color) {
    f32 half = size * 0.5f;
    f32 step = size / static_cast<f32>(divisions);

    for (u32 i = 0; i <= divisions; ++i) {
        f32 t = -half + step * static_cast<f32>(i);
        // X-axis lines
        AddLine(center + math::Vec3{-half, 0, t}, center + math::Vec3{half, 0, t}, color);
        // Z-axis lines
        AddLine(center + math::Vec3{t, 0, -half}, center + math::Vec3{t, 0, half}, color);
    }
}

void DebugRenderer::DrawArrow(const math::Vec3& from, const math::Vec3& to,
                                f32 headSize, const math::Vec4& color) {
    AddLine(from, to, color);

    math::Vec3 dir = to - from;
    f32 len = dir.Length();
    if (len < 0.001f) return;
    dir = dir * (1.0f / len);

    math::Vec3 up = std::abs(dir.y) < 0.999f ? math::Vec3{0, 1, 0} : math::Vec3{1, 0, 0};
    math::Vec3 right = dir.Cross(up).Normalized();
    math::Vec3 forward = dir.Cross(right);

    math::Vec3 base = to - dir * headSize;
    AddLine(to, base + right * headSize * 0.5f, color);
    AddLine(to, base - right * headSize * 0.5f, color);
    AddLine(to, base + forward * headSize * 0.5f, color);
    AddLine(to, base - forward * headSize * 0.5f, color);
}

void DebugRenderer::DrawCone(const math::Vec3& apex, const math::Vec3& direction,
                               f32 height, f32 angle, const math::Vec4& color, u32 segments) {
    math::Vec3 dir = direction.Normalized();
    math::Vec3 baseCenter = apex + dir * height;
    f32 baseRadius = height * std::tan(angle);

    DrawCircle(baseCenter, dir, baseRadius, color, segments);

    // Lines from apex to base circle
    math::Vec3 up = std::abs(dir.y) < 0.999f ? math::Vec3{0, 1, 0} : math::Vec3{1, 0, 0};
    math::Vec3 right = dir.Cross(up).Normalized();
    math::Vec3 forward = dir.Cross(right);

    for (u32 i = 0; i < 4; ++i) {
        f32 a = static_cast<f32>(i) * 3.14159265f * 0.5f;
        math::Vec3 basePoint = baseCenter + (right * std::cos(a) + forward * std::sin(a)) * baseRadius;
        AddLine(apex, basePoint, color);
    }
}

void DebugRenderer::DrawAxis(const math::Vec3& origin, f32 size) {
    DrawArrow(origin, origin + math::Vec3{size, 0, 0}, size * 0.1f, {1, 0, 0, 1}); // X = Red
    DrawArrow(origin, origin + math::Vec3{0, size, 0}, size * 0.1f, {0, 1, 0, 1}); // Y = Green
    DrawArrow(origin, origin + math::Vec3{0, 0, size}, size * 0.1f, {0, 0, 1, 1}); // Z = Blue
}

void DebugRenderer::DrawLinePersistent(const math::Vec3& from, const math::Vec3& to,
                                         const math::Vec4& color, f32 duration) {
    m_persistentLines.push_back({{{from, color}, {to, color}}, duration});
}

void DebugRenderer::Flush(rhi::ICommandList* cmd, const math::Mat4& /*viewProj*/) {
    if (!m_enabled) { Clear(); return; }
    if (m_lines.empty() && m_persistentLines.empty()) return;

    // Collect all lines (frame + persistent)
    std::vector<DebugVertex> vertices;
    vertices.reserve((m_lines.size() + m_persistentLines.size()) * 2);

    for (const auto& line : m_lines) {
        vertices.push_back(line.a);
        vertices.push_back(line.b);
    }
    for (const auto& pl : m_persistentLines) {
        vertices.push_back(pl.line.a);
        vertices.push_back(pl.line.b);
    }

    if (vertices.empty()) { Clear(); return; }

    u32 vertCount = math::Min(static_cast<u32>(vertices.size()), m_maxVertices);

    // Upload vertices to GPU
    // TODO: Map m_vertexBuffer and memcpy vertices
    // cmd->BindGraphicsPipeline(m_depthTest ? m_pipeline : m_pipelineNoDepth);
    // cmd->BindVertexBuffer(m_vertexBuffer, 0);
    // cmd->Draw(vertCount, 1, 0, 0);

    cmd->BeginDebugLabel("Debug Lines", 1.0f, 1.0f, 0.0f);
    // Actual draw call would go here
    (void)vertCount;
    cmd->EndDebugLabel();

    Clear();
}

void DebugRenderer::Clear() {
    m_lines.clear();

    // Age persistent lines and remove expired
    m_persistentLines.erase(
        std::remove_if(m_persistentLines.begin(), m_persistentLines.end(),
            [](PersistentLine& pl) {
                pl.remaining -= 1.0f / 60.0f; // Approximate
                return pl.remaining <= 0;
            }),
        m_persistentLines.end());
}

} // namespace nge::renderer
