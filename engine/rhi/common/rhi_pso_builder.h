#pragma once

#include "engine/core/types.h"
#include "engine/rhi/common/rhi_device.h"
#include <vector>
#include <string>

namespace nge::rhi {

// ─── GPU Pipeline State Object (PSO) Builder ─────────────────────────────
// Fluent builder for graphics and compute pipeline state objects.
// Validates the configuration before creation and provides clear error
// messages for misconfigured pipelines.

enum class CullMode : u8 { None, Front, Back, FrontAndBack };
enum class FrontFace : u8 { CounterClockwise, Clockwise };
enum class PolygonMode : u8 { Fill, Line, Point };
enum class PrimitiveTopology : u8 { TriangleList, TriangleStrip, TriangleFan, LineList, LineStrip, PointList };
enum class BlendFactor : u8 { Zero, One, SrcAlpha, OneMinusSrcAlpha, DstAlpha, OneMinusDstAlpha, SrcColor, OneMinusSrcColor };
enum class BlendOp : u8 { Add, Subtract, ReverseSubtract, Min, Max };
enum class CompareFunc : u8 { Never, Less, LessEqual, Greater, GreaterEqual, Equal, NotEqual, Always };
enum class StencilOp : u8 { Keep, Zero, Replace, IncrClamp, DecrClamp, Invert, IncrWrap, DecrWrap };

struct VertexAttribute {
    u32         location;
    Format      format;
    u32         offset;
    u32         binding;
};

struct VertexBinding {
    u32  binding;
    u32  stride;
    bool perInstance = false;
};

struct BlendAttachment {
    bool        blendEnable = false;
    BlendFactor srcColor = BlendFactor::One;
    BlendFactor dstColor = BlendFactor::Zero;
    BlendOp     colorOp = BlendOp::Add;
    BlendFactor srcAlpha = BlendFactor::One;
    BlendFactor dstAlpha = BlendFactor::Zero;
    BlendOp     alphaOp = BlendOp::Add;
    u8          writeMask = 0xF; // RGBA
};

struct StencilState {
    StencilOp   failOp = StencilOp::Keep;
    StencilOp   passOp = StencilOp::Keep;
    StencilOp   depthFailOp = StencilOp::Keep;
    CompareFunc compareFunc = CompareFunc::Always;
    u32         compareMask = 0xFF;
    u32         writeMask = 0xFF;
    u32         reference = 0;
};

struct GraphicsPSODesc {
    // Shaders
    std::string         vertexShader;
    std::string         fragmentShader;
    std::string         geometryShader;
    std::string         tessControlShader;
    std::string         tessEvalShader;
    std::string         meshShader;
    std::string         taskShader;

    // Vertex input
    std::vector<VertexAttribute> vertexAttributes;
    std::vector<VertexBinding>   vertexBindings;

    // Input assembly
    PrimitiveTopology   topology = PrimitiveTopology::TriangleList;
    bool                primitiveRestart = false;

    // Rasterization
    PolygonMode         polygonMode = PolygonMode::Fill;
    CullMode            cullMode = CullMode::Back;
    FrontFace           frontFace = FrontFace::CounterClockwise;
    bool                depthClamp = false;
    bool                rasterizerDiscard = false;
    f32                 depthBiasConstant = 0.0f;
    f32                 depthBiasSlope = 0.0f;
    f32                 depthBiasClamp = 0.0f;
    f32                 lineWidth = 1.0f;

    // Depth/stencil
    bool                depthTestEnable = true;
    bool                depthWriteEnable = true;
    CompareFunc         depthCompareFunc = CompareFunc::Less;
    bool                stencilTestEnable = false;
    StencilState        stencilFront;
    StencilState        stencilBack;

    // Multisampling
    u32                 sampleCount = 1;
    bool                sampleShading = false;
    f32                 minSampleShading = 1.0f;

    // Color blend
    std::vector<BlendAttachment> blendAttachments;
    f32                 blendConstants[4] = {0, 0, 0, 0};

    // Render targets
    std::vector<Format> colorFormats;
    Format              depthFormat = Format::Undefined;

    // Dynamic state
    bool                dynamicViewport = true;
    bool                dynamicScissor = true;
    bool                dynamicLineWidth = false;
    bool                dynamicDepthBias = false;
    bool                dynamicBlendConstants = false;
    bool                dynamicStencilReference = false;

    // Pipeline layout
    u64                 layoutHandle = 0;

    std::string         debugName;
};

struct ComputePSODesc {
    std::string shader;
    std::string entryPoint = "CSMain";
    u64         layoutHandle = 0;
    std::string debugName;
};

struct PSOValidationResult {
    bool valid = true;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
};

class GraphicsPSOBuilder {
public:
    GraphicsPSOBuilder& SetVertexShader(const std::string& path);
    GraphicsPSOBuilder& SetFragmentShader(const std::string& path);
    GraphicsPSOBuilder& SetMeshShader(const std::string& path);
    GraphicsPSOBuilder& SetTaskShader(const std::string& path);

    GraphicsPSOBuilder& AddVertexAttribute(u32 location, Format format, u32 offset, u32 binding = 0);
    GraphicsPSOBuilder& AddVertexBinding(u32 binding, u32 stride, bool perInstance = false);

    GraphicsPSOBuilder& SetTopology(PrimitiveTopology topology);
    GraphicsPSOBuilder& SetCullMode(CullMode mode);
    GraphicsPSOBuilder& SetFrontFace(FrontFace face);
    GraphicsPSOBuilder& SetPolygonMode(PolygonMode mode);
    GraphicsPSOBuilder& SetDepthBias(f32 constant, f32 slope, f32 clamp = 0.0f);

    GraphicsPSOBuilder& EnableDepthTest(bool enable = true);
    GraphicsPSOBuilder& EnableDepthWrite(bool enable = true);
    GraphicsPSOBuilder& SetDepthCompare(CompareFunc func);
    GraphicsPSOBuilder& EnableStencilTest(bool enable = true);
    GraphicsPSOBuilder& SetStencilFront(const StencilState& state);
    GraphicsPSOBuilder& SetStencilBack(const StencilState& state);

    GraphicsPSOBuilder& SetSampleCount(u32 count);
    GraphicsPSOBuilder& EnableSampleShading(f32 minFraction = 1.0f);

    GraphicsPSOBuilder& AddBlendAttachment(const BlendAttachment& attachment);
    GraphicsPSOBuilder& AddAlphaBlendAttachment();
    GraphicsPSOBuilder& AddOpaqueAttachment();

    GraphicsPSOBuilder& AddColorFormat(Format format);
    GraphicsPSOBuilder& SetDepthFormat(Format format);

    GraphicsPSOBuilder& SetLayout(u64 layoutHandle);
    GraphicsPSOBuilder& SetName(const std::string& name);

    // Validate the pipeline description
    PSOValidationResult Validate() const;

    // Build the description (does not create the actual pipeline)
    GraphicsPSODesc Build() const;

    // Presets
    static GraphicsPSOBuilder Opaque();
    static GraphicsPSOBuilder Transparent();
    static GraphicsPSOBuilder ShadowMap();
    static GraphicsPSOBuilder Wireframe();
    static GraphicsPSOBuilder FullscreenTriangle();

private:
    GraphicsPSODesc m_desc;
};

class ComputePSOBuilder {
public:
    ComputePSOBuilder& SetShader(const std::string& path);
    ComputePSOBuilder& SetEntryPoint(const std::string& entry);
    ComputePSOBuilder& SetLayout(u64 layoutHandle);
    ComputePSOBuilder& SetName(const std::string& name);

    PSOValidationResult Validate() const;
    ComputePSODesc Build() const;

private:
    ComputePSODesc m_desc;
};

} // namespace nge::rhi
