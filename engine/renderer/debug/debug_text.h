#pragma once

#include "engine/core/types.h"
#include "engine/core/math/math_types.h"
#include "engine/rhi/common/rhi_device.h"
#include <string>
#include <vector>

namespace nge::renderer {

// ─── Debug Text Renderer ─────────────────────────────────────────────────
// Simple bitmap font text renderer for debug overlays.
// Uses a built-in 8×8 ASCII bitmap font (no external assets needed).
// Renders screen-space text with background boxes, colors, and alignment.

enum class TextAlign : u8 {
    Left,
    Center,
    Right,
};

struct DebugTextEntry {
    std::string text;
    f32         x, y;           // Screen position (pixels, top-left origin)
    math::Vec4  color;          // Text color
    math::Vec4  bgColor;        // Background color (alpha=0 for transparent)
    f32         scale;          // Font scale (1.0 = 8px)
    TextAlign   align;
};

struct DebugTextVertex {
    math::Vec2 position;
    math::Vec2 texcoord;
    math::Vec4 color;
};

class DebugTextRenderer {
public:
    bool Init(rhi::IDevice* device);
    void Shutdown();

    // ── Drawing API ──────────────────────────────────────────────────

    void DrawText(f32 x, f32 y, const std::string& text,
                   const math::Vec4& color = {1, 1, 1, 1},
                   f32 scale = 1.0f);

    void DrawText(f32 x, f32 y, const std::string& text,
                   const math::Vec4& color, const math::Vec4& bgColor,
                   f32 scale = 1.0f, TextAlign align = TextAlign::Left);

    // Printf-style (format string → DrawText)
    void DrawTextF(f32 x, f32 y, const math::Vec4& color, const char* fmt, ...);

    // World-space text (projected to screen)
    void DrawText3D(const math::Vec3& worldPos, const std::string& text,
                     const math::Mat4& viewProj, u32 screenW, u32 screenH,
                     const math::Vec4& color = {1, 1, 1, 1}, f32 scale = 1.0f);

    // ── Rendering ────────────────────────────────────────────────────

    void Flush(rhi::ICommandList* cmd, u32 screenWidth, u32 screenHeight);
    void Clear();

    // Stats
    u32 GetCharCount() const;

    // Config
    void SetEnabled(bool enabled) { m_enabled = enabled; }
    bool IsEnabled() const { return m_enabled; }

private:
    void GenerateFontTexture();
    void BuildVertices(u32 screenWidth, u32 screenHeight);

    rhi::IDevice* m_device = nullptr;

    std::vector<DebugTextEntry>  m_entries;
    std::vector<DebugTextVertex> m_vertices;
    std::vector<u32>             m_indices;

    // GPU resources
    rhi::TextureHandle  m_fontTexture;   // 128×64 R8 bitmap font atlas
    rhi::BufferHandle   m_vertexBuffer;
    rhi::BufferHandle   m_indexBuffer;
    rhi::PipelineHandle m_pipeline;

    static constexpr u32 GLYPH_WIDTH = 8;
    static constexpr u32 GLYPH_HEIGHT = 8;
    static constexpr u32 CHARS_PER_ROW = 16;
    static constexpr u32 ATLAS_WIDTH = 128;  // 16 * 8
    static constexpr u32 ATLAS_HEIGHT = 48;  // 6 * 8 (chars 32-127)
    static constexpr u32 MAX_CHARS = 4096;

    bool m_enabled = true;
};

} // namespace nge::renderer
